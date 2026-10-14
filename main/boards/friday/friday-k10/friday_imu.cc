#include "friday_imu.h"
#include "config.h"

#include <esp_log.h>
#include <esp_timer.h>

#include <cmath>

#define TAG "FridayImu"

// LIS3DH-compatible registers (SC7A20H)
static constexpr uint8_t kRegWhoAmI = 0x0F;
static constexpr uint8_t kRegCtrl1 = 0x20;
static constexpr uint8_t kRegCtrl4 = 0x23;
static constexpr uint8_t kRegOutXL = 0x28;
static constexpr uint8_t kAutoIncrement = 0x80;

static constexpr int kLoopMs = 20;  // 50 Hz

// Motion thresholds, in g (tune on the real robot)
static constexpr float kStillThreshold = 0.03f;
static constexpr float kPickupThreshold = 0.12f;
static constexpr float kPickupThresholdDampened = 0.25f;
static constexpr float kShakeJerk = 0.9f;  // |a| - 1g spike counted as a shake jerk
static constexpr int kShakeJerksNeeded = 4;
static constexpr int64_t kShakeWindowUs = 1500 * 1000;
static constexpr int64_t kShakeCooldownUs = 3000 * 1000;
static constexpr int kRestAfterMs = 1500;
static constexpr int kPutDownAfterMs = 1200;
static constexpr int kOrientationStableMs = 500;
static constexpr float kOrientationThreshold = 0.8f;

FridayImu::FridayImu(i2c_master_bus_handle_t bus) {
    if (!Probe(bus, 0x19) && !Probe(bus, 0x18)) {
        ESP_LOGE(TAG, "Accelerometer not found at 0x18/0x19, motion reactions disabled");
        return;
    }

    // 50 Hz, normal mode, X/Y/Z enabled
    WriteRegister(kRegCtrl1, 0x47);
    // Block data update, +-2g, high resolution
    WriteRegister(kRegCtrl4, 0x88);

    xTaskCreate([](void* arg) {
        static_cast<FridayImu*>(arg)->Loop();
    }, "friday_imu", 4096, this, 3, &task_);
}

FridayImu::~FridayImu() {
    if (task_ != nullptr) {
        vTaskDelete(task_);
    }
    if (dev_ != nullptr) {
        i2c_master_bus_rm_device(dev_);
    }
}

bool FridayImu::Probe(i2c_master_bus_handle_t bus, uint8_t address) {
    if (i2c_master_probe(bus, address, 100) != ESP_OK) {
        return false;
    }
    i2c_device_config_t dev_config = {};
    dev_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_config.device_address = address;
    dev_config.scl_speed_hz = 400000;
    if (i2c_master_bus_add_device(bus, &dev_config, &dev_) != ESP_OK) {
        dev_ = nullptr;
        return false;
    }

    uint8_t who_am_i = 0;
    if (!ReadRegisters(kRegWhoAmI, &who_am_i, 1)) {
        i2c_master_bus_rm_device(dev_);
        dev_ = nullptr;
        return false;
    }
    // SC7A20H reports 0x11, LIS3DH reports 0x33
    ESP_LOGI(TAG, "Accelerometer at 0x%02x, WHO_AM_I=0x%02x", address, who_am_i);
    return true;
}

bool FridayImu::WriteRegister(uint8_t reg, uint8_t value) {
    uint8_t data[2] = {reg, value};
    return i2c_master_transmit(dev_, data, sizeof(data), 100) == ESP_OK;
}

bool FridayImu::ReadRegisters(uint8_t reg, uint8_t* data, size_t length) {
    if (length > 1) {
        reg |= kAutoIncrement;
    }
    return i2c_master_transmit_receive(dev_, &reg, 1, data, length, 100) == ESP_OK;
}

bool FridayImu::ReadAcceleration(float raw[3]) {
    uint8_t data[6];
    if (!ReadRegisters(kRegOutXL, data, sizeof(data))) {
        return false;
    }
    for (int i = 0; i < 3; i++) {
        // 12-bit left-justified, 1 mg/digit at +-2g high resolution
        int16_t value = (int16_t)((data[i * 2 + 1] << 8) | data[i * 2]);
        raw[i] = (value >> 4) / 1000.0f;
    }
    return true;
}

void FridayImu::GetGravity(float& up, float& right, float& front) const {
    up = body_[0];
    right = body_[1];
    front = body_[2];
}

const char* FridayImu::OrientationName(FridayOrientation orientation) {
    switch (orientation) {
        case FridayOrientation::kUpright: return "upright";
        case FridayOrientation::kUpsideDown: return "upside_down";
        case FridayOrientation::kOnLeftSide: return "on_left_side";
        case FridayOrientation::kOnRightSide: return "on_right_side";
        case FridayOrientation::kOnBack: return "on_back";
        case FridayOrientation::kFaceDown: return "face_down";
        default: return "unknown";
    }
}

void FridayImu::Emit(FridayImuEvent event) {
    if (callback_) {
        callback_(event, orientation_.load());
    }
}

static FridayOrientation Classify(const float g[3]) {
    float norm = sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
    if (norm < 0.5f) {
        return FridayOrientation::kUnknown;  // free fall or garbage
    }
    float up = g[0] / norm, right = g[1] / norm, front = g[2] / norm;
    if (up > kOrientationThreshold) return FridayOrientation::kUpright;
    if (up < -kOrientationThreshold) return FridayOrientation::kUpsideDown;
    if (right > kOrientationThreshold) return FridayOrientation::kOnLeftSide;
    if (right < -kOrientationThreshold) return FridayOrientation::kOnRightSide;
    if (front > kOrientationThreshold) return FridayOrientation::kOnBack;
    if (front < -kOrientationThreshold) return FridayOrientation::kFaceDown;
    return FridayOrientation::kUnknown;  // somewhere in between
}

void FridayImu::Loop() {
    float gravity[3] = {0, 0, 0};
    bool gravity_init = false;
    float activity = 0;
    int still_ms = 0;
    bool resting = false;
    int pickup_samples = 0;

    int shake_jerks = 0;
    bool in_jerk = false;
    int64_t shake_window_start = 0;
    int64_t last_shake = 0;

    FridayOrientation candidate = FridayOrientation::kUnknown;
    int candidate_ms = 0;
    int log_countdown = 0;

    static const int kAxis[3] = {FRIDAY_IMU_UP_AXIS, FRIDAY_IMU_RIGHT_AXIS, FRIDAY_IMU_FRONT_AXIS};
    static const float kSign[3] = {FRIDAY_IMU_UP_SIGN, FRIDAY_IMU_RIGHT_SIGN, FRIDAY_IMU_FRONT_SIGN};

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(kLoopMs));

        float raw[3];
        if (!ReadAcceleration(raw)) {
            continue;
        }

        // Log raw axes for the first ~10 s after boot to help calibration
        if (log_countdown < 5 && (esp_timer_get_time() / 2000000) > log_countdown) {
            ESP_LOGI(TAG, "raw x=%.2f y=%.2f z=%.2f", raw[0], raw[1], raw[2]);
            log_countdown++;
        }

        float a[3];
        for (int i = 0; i < 3; i++) {
            a[i] = kSign[i] * raw[kAxis[i]];
            body_[i] = a[i];
        }

        if (!gravity_init) {
            for (int i = 0; i < 3; i++) gravity[i] = a[i];
            gravity_init = true;
        }
        for (int i = 0; i < 3; i++) {
            gravity[i] = gravity[i] * 0.9f + a[i] * 0.1f;
        }

        // How much the reading deviates from the slow gravity estimate
        float dx = a[0] - gravity[0], dy = a[1] - gravity[1], dz = a[2] - gravity[2];
        float dynamic = sqrtf(dx * dx + dy * dy + dz * dz);
        activity = activity * 0.8f + dynamic * 0.2f;
        float magnitude = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
        int64_t now = esp_timer_get_time();

        // --- Picked up / put down ---
        still_ms = activity < kStillThreshold ? still_ms + kLoopMs : 0;
        float pickup_threshold = dampened_ ? kPickupThresholdDampened : kPickupThreshold;

        if (held_) {
            if (still_ms > kPutDownAfterMs) {
                held_ = false;
                resting = true;
                Emit(FridayImuEvent::kPutDown);
            }
        } else if (resting) {
            // Require a few consecutive samples so a desk bump doesn't count
            pickup_samples = activity > pickup_threshold ? pickup_samples + 1 : 0;
            if (pickup_samples >= 3) {
                held_ = true;
                resting = false;
                pickup_samples = 0;
                Emit(FridayImuEvent::kPickedUp);
            }
        } else if (still_ms > kRestAfterMs) {
            resting = true;
        }

        // --- Shake: several hard jerks in a short window ---
        bool jerk = fabsf(magnitude - 1.0f) > kShakeJerk;
        if (jerk && !in_jerk) {
            if (now - shake_window_start > kShakeWindowUs) {
                shake_window_start = now;
                shake_jerks = 0;
            }
            shake_jerks++;
            if (shake_jerks >= kShakeJerksNeeded && now - last_shake > kShakeCooldownUs) {
                last_shake = now;
                shake_jerks = 0;
                Emit(FridayImuEvent::kShaken);
            }
        }
        in_jerk = jerk;

        // --- Orientation: only judge it while fairly calm ---
        if (activity < 0.2f) {
            FridayOrientation current = Classify(gravity);
            if (current != candidate) {
                candidate = current;
                candidate_ms = 0;
            } else if (candidate != FridayOrientation::kUnknown) {
                candidate_ms += kLoopMs;
                if (candidate_ms >= kOrientationStableMs && candidate != orientation_.load()) {
                    orientation_ = candidate;
                    ESP_LOGI(TAG, "Orientation: %s", OrientationName(candidate));
                    Emit(FridayImuEvent::kOrientationChanged);
                }
            }
        }
    }
}
