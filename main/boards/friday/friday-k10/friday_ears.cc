#include "friday_ears.h"
#include "config.h"

#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>

#include <algorithm>
#include <cmath>

#define TAG "FridayEars"

static constexpr ledc_mode_t kSpeedMode = LEDC_LOW_SPEED_MODE;
// Timer 0 is commonly used by camera XCLK drivers, so stay out of its way
static constexpr ledc_timer_t kTimer = LEDC_TIMER_2;
static constexpr ledc_timer_bit_t kResolution = LEDC_TIMER_14_BIT;
static constexpr int kServoFreqHz = 50;
static constexpr int kPeriodUs = 1000000 / kServoFreqHz;
static constexpr int kLoopMs = 20;
// Ears go limp after resting this long (stops servo hum and jitter)
static constexpr int64_t kDetachAfterUs = 400 * 1000;

static int RandomRange(int min_value, int max_value) {
    return min_value + (int)(esp_random() % (uint32_t)(max_value - min_value + 1));
}

FridayEars::FridayEars(gpio_num_t left_gpio, gpio_num_t right_gpio) {
    gpio_[0] = left_gpio;
    gpio_[1] = right_gpio;

    ledc_timer_config_t timer_config = {};
    timer_config.speed_mode = kSpeedMode;
    timer_config.duty_resolution = kResolution;
    timer_config.timer_num = kTimer;
    timer_config.freq_hz = kServoFreqHz;
    timer_config.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));

    for (int i = 0; i < 2; i++) {
        ledc_channel_config_t channel_config = {};
        channel_config.gpio_num = gpio_[i];
        channel_config.speed_mode = kSpeedMode;
        channel_config.channel = channel_[i];
        channel_config.timer_sel = kTimer;
        channel_config.duty = 0;
        channel_config.hpoint = 0;
        ESP_ERROR_CHECK(ledc_channel_config(&channel_config));
    }

    // Start with a little "hello" so you can see the ears work after boot
    Play(EarGesture::kWiggle);

    xTaskCreate([](void* arg) {
        static_cast<FridayEars*>(arg)->Loop();
    }, "friday_ears", 3072, this, 4, &task_);
}

FridayEars::~FridayEars() {
    if (task_ != nullptr) {
        vTaskDelete(task_);
    }
    Detach();
}

void FridayEars::SetBasePose(int left, int right) {
    std::lock_guard<std::mutex> lock(mutex_);
    base_[0] = std::clamp(left, 0, 100);
    base_[1] = std::clamp(right, 0, 100);
}

void FridayEars::SetTalking(bool talking) {
    std::lock_guard<std::mutex> lock(mutex_);
    talking_ = talking;
    if (!talking) {
        talk_offset_[0] = talk_offset_[1] = 0;
    }
}

void FridayEars::SetQuiet(bool quiet) {
    std::lock_guard<std::mutex> lock(mutex_);
    quiet_ = quiet;
}

void FridayEars::Play(EarGesture gesture) {
    std::lock_guard<std::mutex> lock(mutex_);
    gesture_ = BuildGesture(gesture);
    gesture_start_us_ = esp_timer_get_time();
}

bool FridayEars::GestureFromName(const std::string& name, EarGesture& gesture) {
    static const struct {
        const char* name;
        EarGesture gesture;
    } kNames[] = {
        {"wiggle", EarGesture::kWiggle},
        {"flap", EarGesture::kFlap},
        {"perk", EarGesture::kPerk},
        {"twitch_left", EarGesture::kTwitchLeft},
        {"twitch_right", EarGesture::kTwitchRight},
        {"wobble", EarGesture::kWobble},
        {"shiver", EarGesture::kShiver},
    };
    for (const auto& entry : kNames) {
        if (name == entry.name) {
            gesture = entry.gesture;
            return true;
        }
    }
    return false;
}

std::vector<FridayEars::Keyframe> FridayEars::BuildGesture(EarGesture gesture) {
    std::vector<Keyframe> frames;
    switch (gesture) {
        case EarGesture::kWiggle:
            for (int i = 0; i < 3; i++) {
                frames.push_back({i * 260, 22, -22});
                frames.push_back({i * 260 + 130, -22, 22});
            }
            frames.push_back({780, 0, 0});
            break;
        case EarGesture::kFlap:
            for (int i = 0; i < 3; i++) {
                frames.push_back({i * 220, 30, 30});
                frames.push_back({i * 220 + 110, -15, -15});
            }
            frames.push_back({660, 0, 0});
            break;
        case EarGesture::kPerk:
            frames.push_back({0, 45, 45});
            frames.push_back({500, 35, 35});
            frames.push_back({900, 0, 0});
            break;
        case EarGesture::kTwitchLeft:
            frames.push_back({0, -30, 0});
            frames.push_back({120, 0, 0});
            break;
        case EarGesture::kTwitchRight:
            frames.push_back({0, 0, -30});
            frames.push_back({120, 0, 0});
            break;
        case EarGesture::kWobble:
            for (int i = 0; i <= 16; i++) {
                float s = sinf(i * 0.8f);
                frames.push_back({i * 120, (int)(25 * s), (int)(-25 * s)});
            }
            frames.push_back({2040, 0, 0});
            break;
        case EarGesture::kShiver:
            for (int i = 0; i < 10; i++) {
                int amount = (i % 2 == 0) ? 8 : -8;
                frames.push_back({i * 60, amount, -amount});
            }
            frames.push_back({600, 0, 0});
            break;
    }
    return frames;
}

void FridayEars::Loop() {
    while (true) {
        int64_t now = esp_timer_get_time();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            float offset[2] = {0, 0};

            if (!gesture_.empty()) {
                // Hold the most recent keyframe; the last one ends the gesture
                int elapsed_ms = (int)((now - gesture_start_us_) / 1000);
                const Keyframe* frame = &gesture_.front();
                for (const auto& kf : gesture_) {
                    if (kf.time_ms <= elapsed_ms) {
                        frame = &kf;
                    }
                }
                offset[0] = frame->left;
                offset[1] = frame->right;
                if (elapsed_ms > gesture_.back().time_ms + 150) {
                    gesture_.clear();
                }
            } else if (talking_ && !quiet_) {
                if (now >= next_talk_move_us_) {
                    talk_offset_[0] = RandomRange(-14, 14);
                    talk_offset_[1] = RandomRange(-14, 14);
                    next_talk_move_us_ = now + RandomRange(250, 650) * 1000LL;
                }
                offset[0] = talk_offset_[0];
                offset[1] = talk_offset_[1];
            }

            for (int i = 0; i < 2; i++) {
                target_[i] = std::clamp(base_[i] + offset[i], 0.0f, 100.0f);
            }
        }

        // Ease towards the target so the ears feel springy, not robotic
        bool settled = true;
        for (int i = 0; i < 2; i++) {
            float diff = target_[i] - current_[i];
            if (fabsf(diff) > 0.5f) {
                current_[i] += diff * 0.35f;
                settled = false;
            } else {
                current_[i] = target_[i];
            }
        }

        if (!settled) {
            settled_since_us_ = now;
            WriteServo(0, current_[0]);
            WriteServo(1, current_[1]);
            attached_ = true;
            moving_ = true;
        } else if (attached_ && now - settled_since_us_ > kDetachAfterUs) {
            Detach();
            moving_ = false;
        } else if (!attached_) {
            moving_ = false;
        }

        vTaskDelay(pdMS_TO_TICKS(kLoopMs));
    }
}

void FridayEars::WriteServo(int index, float ear_units) {
    float down = index == 0 ? FRIDAY_EAR_LEFT_DOWN_ANGLE : FRIDAY_EAR_RIGHT_DOWN_ANGLE;
    float up = index == 0 ? FRIDAY_EAR_LEFT_UP_ANGLE : FRIDAY_EAR_RIGHT_UP_ANGLE;
    float angle = down + (up - down) * ear_units / 100.0f;
    float pulse_us = FRIDAY_SERVO_MIN_PULSE_US +
                     (FRIDAY_SERVO_MAX_PULSE_US - FRIDAY_SERVO_MIN_PULSE_US) * angle / 180.0f;
    uint32_t duty = (uint32_t)(pulse_us * (1 << kResolution) / kPeriodUs);
    ledc_set_duty(kSpeedMode, channel_[index], duty);
    ledc_update_duty(kSpeedMode, channel_[index]);
}

void FridayEars::Detach() {
    for (int i = 0; i < 2; i++) {
        ledc_set_duty(kSpeedMode, channel_[i], 0);
        ledc_update_duty(kSpeedMode, channel_[i]);
    }
    attached_ = false;
}
