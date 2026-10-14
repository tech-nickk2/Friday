#include "friday_brain.h"
#include "application.h"
#include "config.h"
#include "mcp_server.h"

#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#define TAG "FridayBrain"

static constexpr int kLoopMs = 50;

struct EarMood {
    const char* name;
    int left;
    int right;
    const char* gesture;  // played once when the emotion arrives, may be null
};

// How the ears react to each expression (0 = droopy, 100 = perked)
static const EarMood kEarMoods[] = {
    {"neutral", 55, 55, nullptr},
    {"happy", 85, 85, "flap"},
    {"laughing", 90, 90, "flap"},
    {"funny", 80, 60, "wiggle"},
    {"sad", 10, 10, nullptr},
    {"angry", 20, 20, nullptr},
    {"crying", 5, 5, nullptr},
    {"loving", 70, 70, "wiggle"},
    {"embarrassed", 30, 35, nullptr},
    {"surprised", 100, 100, "perk"},
    {"shocked", 100, 100, "perk"},
    {"thinking", 90, 40, nullptr},
    {"winking", 85, 50, "twitch_right"},
    {"cool", 60, 60, nullptr},
    {"relaxed", 45, 45, nullptr},
    {"delicious", 80, 80, "flap"},
    {"kissy", 75, 75, "wiggle"},
    {"confident", 90, 90, nullptr},
    {"sleepy", 15, 15, nullptr},
    {"silly", 20, 100, "wiggle"},
    {"confused", 100, 30, nullptr},
};

static int RandomRange(int min_value, int max_value) {
    return min_value + (int)(esp_random() % (uint32_t)(max_value - min_value + 1));
}

static void PlayGesture(FridayEars* ears, const char* name) {
    EarGesture gesture;
    if (name != nullptr && FridayEars::GestureFromName(name, gesture)) {
        ears->Play(gesture);
    }
}

FridayBrain::FridayBrain(FridayFace* face, FridayEars* ears, FridayImu* imu)
    : face_(face), ears_(ears), imu_(imu) {
    imu_queue_ = xQueueCreate(8, sizeof(ImuMessage));

    face_->OnEmotionChanged([this](const std::string& emotion) {
        std::lock_guard<std::mutex> lock(mutex_);
        emotion_ = emotion;
        emotion_changed_ = true;
        tool_pose_[0] = tool_pose_[1] = -1;
    });

    imu_->OnEvent([this](FridayImuEvent event, FridayOrientation orientation) {
        ImuMessage message = {event, orientation};
        xQueueSend(imu_queue_, &message, 0);
    });

    RegisterMcpTools();

    xTaskCreate([](void* arg) {
        static_cast<FridayBrain*>(arg)->Loop();
    }, "friday_brain", 4096, this, 3, &task_);
}

void FridayBrain::Loop() {
    auto& app = Application::GetInstance();
    while (true) {
        int64_t now = esp_timer_get_time();

        DeviceState state = app.GetDeviceState();
        if (state != last_state_) {
            OnStateChanged(state, now);
            last_state_ = state;
        }

        std::string emotion;
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (emotion_changed_) {
                emotion_changed_ = false;
                changed = true;
                emotion = emotion_;
            }
        }
        if (changed) {
            OnEmotion(emotion);
        }

        ImuMessage message;
        while (xQueueReceive(imu_queue_, &message, 0) == pdTRUE) {
            OnImu(message, now);
        }

        if (!followup_expression_.empty() && now >= followup_at_us_) {
            face_->React(followup_expression_, 2500);
            followup_expression_.clear();
        }
        if (wake_pending_ && now >= wake_at_us_) {
            wake_pending_ = false;
            if (app.GetDeviceState() == kDeviceStateIdle) {
                app.WakeWordInvoke(FRIDAY_PICKUP_WAKE_WORD);
            }
        }

        if (state == kDeviceStateIdle) {
            // Doze off after a while, unless someone is holding her
            if (!sleeping_ && !imu_->held() && now - idle_since_us_ > FRIDAY_SLEEP_AFTER_MS * 1000LL) {
                sleeping_ = true;
                face_->SetActivity(FaceActivity::kSleeping);
            }
            // Random ear twitches keep her looking alive on the desk
            if (!sleeping_ && now >= next_twitch_us_) {
                static const char* kIdleGestures[] = {"twitch_left", "twitch_right", "twitch_left",
                                                      "twitch_right", "wiggle"};
                PlayGesture(ears_, kIdleGestures[RandomRange(0, 4)]);
                next_twitch_us_ = now + RandomRange(8000, 25000) * 1000LL;
            }
        }

        UpdateEars(state, now);
        // The servos shake the body a little, don't mistake that for a pickup
        imu_->SetDampened(ears_->IsMoving());

        vTaskDelay(pdMS_TO_TICKS(kLoopMs));
    }
}

void FridayBrain::OnStateChanged(DeviceState state, int64_t now) {
    if (state != kDeviceStateIdle) {
        sleeping_ = false;
    }
    switch (state) {
        case kDeviceStateIdle:
            idle_since_us_ = now;
            next_twitch_us_ = now + RandomRange(5000, 15000) * 1000LL;
            face_->SetActivity(sleeping_ ? FaceActivity::kSleeping : FaceActivity::kIdle);
            ears_->SetTalking(false);
            ears_->SetQuiet(false);
            break;
        case kDeviceStateConnecting:
            face_->SetActivity(FaceActivity::kThinking);
            ears_->Play(EarGesture::kPerk);
            break;
        case kDeviceStateListening:
            // Perk up and then hold still: servo noise would leak into the mics
            face_->SetActivity(FaceActivity::kListening);
            ears_->SetTalking(false);
            ears_->SetQuiet(true);
            break;
        case kDeviceStateSpeaking:
            face_->SetActivity(FaceActivity::kSpeaking);
            ears_->SetQuiet(false);
            ears_->SetTalking(true);
            break;
        case kDeviceStateWifiConfiguring:
        case kDeviceStateActivating:
        case kDeviceStateUpgrading:
            face_->SetActivity(FaceActivity::kThinking);
            break;
        default:
            face_->SetActivity(FaceActivity::kIdle);
            break;
    }
}

void FridayBrain::OnEmotion(const std::string& emotion) {
    for (const auto& mood : kEarMoods) {
        if (emotion == mood.name) {
            mood_pose_[0] = mood.left;
            mood_pose_[1] = mood.right;
            PlayGesture(ears_, mood.gesture);
            return;
        }
    }
    mood_pose_[0] = mood_pose_[1] = 55;
}

void FridayBrain::OverrideEars(int left, int right, int duration_ms) {
    override_pose_[0] = left;
    override_pose_[1] = right;
    override_until_us_ = esp_timer_get_time() + duration_ms * 1000LL;
}

void FridayBrain::UpdateEars(DeviceState state, int64_t now) {
    int left, right;
    int tool_left, tool_right;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tool_left = tool_pose_[0];
        tool_right = tool_pose_[1];
    }

    if (now < override_until_us_) {
        left = override_pose_[0];
        right = override_pose_[1];
    } else if (sleeping_) {
        left = right = 15;
    } else if (tool_left >= 0) {
        left = tool_left;
        right = tool_right;
    } else {
        left = mood_pose_[0];
        right = mood_pose_[1];
        if (state == kDeviceStateListening) {
            left = std::max(left, 80);
            right = std::max(right, 80);
        }
    }
    ears_->SetBasePose(left, right);
}

void FridayBrain::OnImu(const ImuMessage& message, int64_t now) {
    auto& app = Application::GetInstance();
    bool was_sleeping = sleeping_;
    if (message.event != FridayImuEvent::kPutDown &&
        message.orientation != FridayOrientation::kFaceDown) {
        sleeping_ = false;
        idle_since_us_ = now;
        if (was_sleeping && app.GetDeviceState() == kDeviceStateIdle) {
            face_->SetActivity(FaceActivity::kIdle);
        }
    }

    switch (message.event) {
        case FridayImuEvent::kPickedUp:
            ESP_LOGI(TAG, "Picked up");
            face_->React("surprised", 700);
            followup_expression_ = "happy";
            followup_at_us_ = now + 700 * 1000LL;
            ears_->Play(EarGesture::kFlap);
            OverrideEars(90, 90, 2500);
#if FRIDAY_WAKE_ON_PICKUP
            if (app.GetDeviceState() == kDeviceStateIdle) {
                wake_pending_ = true;
                wake_at_us_ = now + 900 * 1000LL;
            }
#endif
            break;

        case FridayImuEvent::kPutDown:
            ESP_LOGI(TAG, "Put down");
            face_->React("relaxed", 1500);
            ears_->Play(EarGesture::kTwitchLeft);
            break;

        case FridayImuEvent::kShaken:
            ESP_LOGI(TAG, "Shaken");
            face_->React("dizzy", 3000);
            followup_expression_.clear();
            ears_->Play(EarGesture::kWobble);
            break;

        case FridayImuEvent::kOrientationChanged:
            switch (message.orientation) {
                case FridayOrientation::kUpsideDown:
                    face_->React("scared", 4000);
                    ears_->Play(EarGesture::kShiver);
                    OverrideEars(25, 25, 4000);
                    break;
                case FridayOrientation::kOnLeftSide:
                    face_->React("curious_left", 3000);
                    OverrideEars(90, 40, 3000);
                    break;
                case FridayOrientation::kOnRightSide:
                    face_->React("curious_right", 3000);
                    OverrideEars(40, 90, 3000);
                    break;
                case FridayOrientation::kOnBack:
                    // Belly up! Time for rubs
                    face_->React("loving", 3000);
                    ears_->Play(EarGesture::kWiggle);
                    break;
                case FridayOrientation::kFaceDown: {
#if FRIDAY_FACE_DOWN_HUSHES
                    DeviceState state = app.GetDeviceState();
                    if (state == kDeviceStateSpeaking) {
                        app.Schedule([&app]() { app.AbortSpeaking(kAbortReasonNone); });
                    } else if (state == kDeviceStateListening) {
                        app.ToggleChatState();
                    }
#endif
                    sleeping_ = true;
                    face_->SetActivity(FaceActivity::kSleeping);
                    break;
                }
                case FridayOrientation::kUpright:
                    if (was_sleeping) {
                        face_->React("happy", 1500);
                        ears_->Play(EarGesture::kPerk);
                    }
                    break;
                default:
                    break;
            }
            break;
    }
}

void FridayBrain::RegisterMcpTools() {
    auto& mcp_server = McpServer::GetInstance();

    mcp_server.AddTool("self.friday.ears.gesture",
        "Move your two puppy ears to show feelings. Use it often while chatting to be expressive. "
        "gesture: wiggle (playful), flap (happy/excited), perk (curious or surprised), "
        "twitch_left, twitch_right (a little flick), wobble (dizzy/silly), shiver (scared/cold).",
        PropertyList({
            Property("gesture", kPropertyTypeString),
        }),
        [this](const PropertyList& properties) -> ToolResult {
            auto name = properties["gesture"].value<std::string>();
            EarGesture gesture;
            if (!FridayEars::GestureFromName(name, gesture)) {
                return std::unexpected("Unknown gesture: " + name);
            }
            ears_->Play(gesture);
            return true;
        });

    mcp_server.AddTool("self.friday.ears.set_pose",
        "Hold your ears in a pose until your next emotion. 0 = fully droopy, 100 = fully perked up. "
        "e.g. both 10 for sad, both 100 for alert, one up one down for a curious head tilt.",
        PropertyList({
            Property("left", kPropertyTypeInteger, 0, 100),
            Property("right", kPropertyTypeInteger, 0, 100),
        }),
        [this](const PropertyList& properties) -> ToolResult {
            std::lock_guard<std::mutex> lock(mutex_);
            tool_pose_[0] = properties["left"].value<int>();
            tool_pose_[1] = properties["right"].value<int>();
            return true;
        });

    mcp_server.AddTool("self.friday.face.set_expression",
        "Change your facial expression. expression: neutral, happy, laughing, funny, sad, angry, "
        "crying, loving, embarrassed, surprised, shocked, thinking, winking, cool, relaxed, "
        "delicious, kissy, confident, sleepy, silly, confused.",
        PropertyList({
            Property("expression", kPropertyTypeString),
        }),
        [this](const PropertyList& properties) -> ToolResult {
            auto expression = properties["expression"].value<std::string>();
            if (!FridayFace::IsKnownExpression(expression)) {
                return std::unexpected("Unknown expression: " + expression);
            }
            face_->SetEmotion(expression.c_str());
            return true;
        });

    mcp_server.AddTool("self.friday.get_posture",
        "Find out how your body is positioned right now: orientation (upright, upside_down, "
        "on_left_side, on_right_side, on_back, face_down) and whether someone is holding you.",
        PropertyList(),
        [this](const PropertyList& properties) -> ToolResult {
            float up, right, front;
            imu_->GetGravity(up, right, front);
            char buffer[160];
            snprintf(buffer, sizeof(buffer),
                     "{\"orientation\":\"%s\",\"held\":%s,\"gravity\":{\"up\":%.2f,\"right\":%.2f,\"front\":%.2f}}",
                     FridayImu::OrientationName(imu_->orientation()),
                     imu_->held() ? "true" : "false", up, right, front);
            return std::string(buffer);
        });
}
