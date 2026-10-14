#pragma once

#include "display/lcd_display.h"

#include <functional>
#include <string>

enum class FaceActivity {
    kIdle,
    kListening,
    kSpeaking,
    kThinking,
    kSleeping,
};

// Animated puppy face drawn with plain LVGL shapes (no image assets), replacing
// the stock emoji. Emotions from the server pick an expression preset, the
// device state adds activity (listening, mouth flapping while speaking...), and
// short "reactions" (picked up, shaken...) override both for a moment.
class FridayFace : public SpiLcdDisplay {
public:
    FridayFace(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
               int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
               bool swap_xy);
    virtual ~FridayFace();

    virtual void SetupUI() override;
    virtual void SetEmotion(const char* emotion) override;

    void SetActivity(FaceActivity activity);
    // Temporarily show an expression preset (also "dizzy", "scared",
    // "curious_left", "curious_right") regardless of the current emotion
    void React(const std::string& expression, int duration_ms);
    std::string emotion();
    // Called with the expression name whenever the server or a tool sets one
    void OnEmotionChanged(std::function<void(const std::string&)> callback) {
        on_emotion_ = callback;
    }

    static bool IsKnownExpression(const std::string& name);

    struct Expression {
        const char* name;
        float lid;          // upper eyelid, 0 open .. 1 closed
        float lower;        // happy squint from below, 0 .. 1
        float scale_x;      // eye size
        float scale_y;
        float brow_tilt;    // -1 sad (inner end up) .. +1 angry (inner end down)
        float brow_raise;   // -1 .. 1
        float brow_opa;     // 0 hidden .. 1
        float cheek;        // blush 0 .. 1
        float mouth_open;   // 0 closed .. 1 wide
        float look_x;       // -1 left .. 1 right
        float look_y;       // -1 up .. 1 down
        bool pout;          // upside-down mouth
        bool wink;          // left eye closed
        bool tear;
        bool zzz;
        bool dizzy;         // eyes roll around
    };

private:
    struct Params {
        float lid_l, lid_r, lower, scale_x, scale_y, brow_tilt, brow_raise, brow_opa;
        float cheek, mouth_open, look_x, look_y;
    };

    struct Eye {
        lv_obj_t* ball = nullptr;
        lv_obj_t* shine_big = nullptr;
        lv_obj_t* shine_small = nullptr;
        lv_obj_t* lid = nullptr;
        lv_obj_t* lower_lid = nullptr;
        lv_obj_t* closed = nullptr;
        lv_obj_t* brow = nullptr;
        lv_point_precise_t brow_points[2];
        int last_brow[4] = {-1, -1, -1, -1};
        int brow_opa = -1;
        bool closed_visible = false;
    };

    lv_obj_t* face_ = nullptr;
    Eye eyes_[2];
    lv_obj_t* cheeks_[2] = {nullptr, nullptr};
    lv_obj_t* nose_ = nullptr;
    lv_obj_t* mouth_arcs_[2] = {nullptr, nullptr};
    lv_obj_t* mouth_open_ = nullptr;
    lv_obj_t* tongue_ = nullptr;
    lv_obj_t* tear_ = nullptr;
    lv_obj_t* zzz_ = nullptr;
    lv_timer_t* timer_ = nullptr;

    const Expression* expression_;
    const Expression* reaction_ = nullptr;
    int64_t reaction_until_us_ = 0;
    FaceActivity activity_ = FaceActivity::kIdle;
    std::function<void(const std::string&)> on_emotion_;

    Params current_;
    int cheek_opa_ = -1;
    bool pout_ = false;
    bool pout_applied_ = true;

    // Idle life: blinking, glancing around, talking mouth
    int64_t next_blink_us_ = 0;
    int64_t blink_start_us_ = 0;
    bool double_blink_ = false;
    int64_t next_glance_us_ = 0;
    float glance_x_ = 0, glance_y_ = 0;
    int64_t next_mouth_us_ = 0;
    float talk_mouth_ = 0;

    void CreateFace();
    void Tick();
    float BlinkAmount(int64_t now);
    void ApplyEye(int index, float lid, const Params& p);
    void ApplyMouth(const Params& p);
    static const Expression* FindExpression(const std::string& name);
};
