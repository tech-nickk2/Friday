#pragma once

#include "friday_ears.h"
#include "friday_face.h"
#include "friday_imu.h"
#include "device_state.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <mutex>
#include <string>

// Ties Friday's personality together: device state, emotions from the server
// and motion events drive the face and the ears. Also exposes MCP tools so the
// AI can wiggle the ears or pull faces on purpose.
class FridayBrain {
public:
    FridayBrain(FridayFace* face, FridayEars* ears, FridayImu* imu);

private:
    struct ImuMessage {
        FridayImuEvent event;
        FridayOrientation orientation;
    };

    FridayFace* face_;
    FridayEars* ears_;
    FridayImu* imu_;
    QueueHandle_t imu_queue_;
    TaskHandle_t task_ = nullptr;

    std::mutex mutex_;
    std::string emotion_ = "neutral";
    bool emotion_changed_ = false;
    int tool_pose_[2] = {-1, -1};  // set by the AI, cleared on the next emotion

    DeviceState last_state_ = kDeviceStateUnknown;
    int64_t idle_since_us_ = 0;
    bool sleeping_ = false;
    int mood_pose_[2] = {55, 55};
    int override_pose_[2] = {0, 0};
    int64_t override_until_us_ = 0;
    int64_t next_twitch_us_ = 0;

    // A follow-up action scheduled after a reaction (e.g. surprised -> happy)
    std::string followup_expression_;
    int64_t followup_at_us_ = 0;
    bool wake_pending_ = false;
    int64_t wake_at_us_ = 0;

    void Loop();
    void OnStateChanged(DeviceState state, int64_t now);
    void OnEmotion(const std::string& emotion);
    void OnImu(const ImuMessage& message, int64_t now);
    void UpdateEars(DeviceState state, int64_t now);
    void OverrideEars(int left, int right, int duration_ms);
    void RegisterMcpTools();
};
