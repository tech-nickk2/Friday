#pragma once

#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <functional>

enum class FridayOrientation {
    kUnknown,
    kUpright,     // standing normally
    kUpsideDown,
    kOnLeftSide,  // lying on her left side
    kOnRightSide,
    kOnBack,      // screen facing the ceiling
    kFaceDown,    // screen facing the desk
};

enum class FridayImuEvent {
    kPickedUp,
    kPutDown,
    kShaken,
    kOrientationChanged,
};

// Driver + gesture detection for the K10's on-board 3-axis accelerometer
// (SC7A20H, LIS3DH-compatible register map).
class FridayImu {
public:
    using Callback = std::function<void(FridayImuEvent event, FridayOrientation orientation)>;

    explicit FridayImu(i2c_master_bus_handle_t bus);
    ~FridayImu();

    bool IsReady() const { return dev_ != nullptr; }
    void OnEvent(Callback callback) { callback_ = callback; }
    // Raise motion thresholds (e.g. while the ear servos shake the body)
    void SetDampened(bool dampened) { dampened_ = dampened; }

    FridayOrientation orientation() const { return orientation_.load(); }
    bool held() const { return held_.load(); }
    // Latest reading in Friday's body frame, in g
    void GetGravity(float& up, float& right, float& front) const;

    static const char* OrientationName(FridayOrientation orientation);

private:
    i2c_master_dev_handle_t dev_ = nullptr;
    TaskHandle_t task_ = nullptr;
    Callback callback_;
    std::atomic<bool> dampened_{false};
    std::atomic<FridayOrientation> orientation_{FridayOrientation::kUnknown};
    std::atomic<bool> held_{false};
    float body_[3] = {0, 0, 0};

    bool Probe(i2c_master_bus_handle_t bus, uint8_t address);
    bool WriteRegister(uint8_t reg, uint8_t value);
    bool ReadRegisters(uint8_t reg, uint8_t* data, size_t length);
    bool ReadAcceleration(float raw[3]);
    void Loop();
    void Emit(FridayImuEvent event);
};
