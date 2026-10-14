#pragma once

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

// Ear positions are in "ear units": 0 = fully droopy, 100 = fully perked.
enum class EarGesture {
    kWiggle,       // alternate left/right a few times
    kFlap,         // happy up/down flapping
    kPerk,         // quick "huh?" pop up
    kTwitchLeft,   // single left flick
    kTwitchRight,  // single right flick
    kWobble,       // slow dizzy see-saw
    kShiver,       // small fast trembling
};

class FridayEars {
public:
    FridayEars(gpio_num_t left_gpio, gpio_num_t right_gpio);
    ~FridayEars();

    // Resting pose the ears return to after gestures
    void SetBasePose(int left, int right);
    // Small random wiggles around the base pose while Friday talks
    void SetTalking(bool talking);
    // While quiet, ears only move to reach the pose and then go limp,
    // so servo noise doesn't leak into the microphones
    void SetQuiet(bool quiet);
    void Play(EarGesture gesture);
    bool IsMoving() const { return moving_.load(); }

    static bool GestureFromName(const std::string& name, EarGesture& gesture);

private:
    struct Keyframe {
        int time_ms;  // time since gesture start
        int left;     // offset from base pose
        int right;
    };

    gpio_num_t gpio_[2];
    ledc_channel_t channel_[2] = {LEDC_CHANNEL_4, LEDC_CHANNEL_5};
    TaskHandle_t task_ = nullptr;

    std::mutex mutex_;
    int base_[2] = {55, 55};
    bool talking_ = false;
    bool quiet_ = false;
    std::vector<Keyframe> gesture_;
    int64_t gesture_start_us_ = 0;

    float current_[2] = {55, 55};
    float target_[2] = {55, 55};
    float talk_offset_[2] = {0, 0};
    int64_t next_talk_move_us_ = 0;
    int64_t settled_since_us_ = 0;
    bool attached_ = false;
    std::atomic<bool> moving_{false};

    void Loop();
    void WriteServo(int index, float ear_units);
    void Detach();
    static std::vector<Keyframe> BuildGesture(EarGesture gesture);
};
