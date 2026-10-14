#include "friday_face.h"

#include "display/lvgl_display/lvgl_theme.h"

#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

#define TAG "FridayFace"

// Palette: cream puppy face, glossy brown eyes, pink blush
static const lv_color_t kFaceColor = lv_color_hex(0xFFEBD6);
static const lv_color_t kEyeColor = lv_color_hex(0x3B2417);
static const lv_color_t kShineColor = lv_color_hex(0xFFFFFF);
static const lv_color_t kBrowColor = lv_color_hex(0x6B4430);
static const lv_color_t kCheekColor = lv_color_hex(0xFF8FA3);
static const lv_color_t kMouthInsideColor = lv_color_hex(0xA8394F);
static const lv_color_t kTongueColor = lv_color_hex(0xFF8FA3);
static const lv_color_t kTearColor = lv_color_hex(0x7CC8FF);

// Layout for the 240x320 portrait screen
static constexpr int kCenterX = 120;
static constexpr int kEyeOffsetX = 56;
static constexpr int kEyeY = 140;
static constexpr int kEyeW = 80;
static constexpr int kEyeH = 100;
static constexpr int kNoseY = 208;
static constexpr int kMouthY = 222;
static constexpr int kMouthArcRadius = 10;
static constexpr int kCheekY = 204;
static constexpr int kCheekOffsetX = 84;

static constexpr int kFrameMs = 33;
static constexpr int kBlinkMs = 180;

// clang-format off
//  name           lid   lower  sx    sy    tilt  raise brow  cheek mouth lookx looky pout   wink   tear   zzz    dizzy
static const FridayFace::Expression kExpressions[] = {
    {"neutral",     0.05f, 0.0f, 1.0f, 1.0f,  0.0f, 0.0f, 0.0f, 0.35f, 0.0f, 0.0f, 0.0f, false, false, false, false, false},
    {"happy",       0.0f, 0.55f, 1.0f, 1.0f,  0.0f, 0.0f, 0.0f, 0.8f, 0.25f, 0.0f, 0.0f, false, false, false, false, false},
    {"laughing",    0.0f, 0.85f, 1.0f, 1.0f,  0.0f, 0.5f, 0.0f, 1.0f, 0.7f,  0.0f, 0.0f, false, false, false, false, false},
    {"funny",       0.0f, 0.6f,  1.0f, 1.0f,  0.0f, 0.3f, 0.0f, 0.8f, 0.45f, 0.0f, 0.0f, false, true,  false, false, false},
    {"sad",         0.3f, 0.0f,  1.0f, 1.05f,-1.0f, 0.0f, 1.0f, 0.1f, 0.0f,  0.0f, 0.4f, true,  false, false, false, false},
    {"angry",       0.25f,0.0f,  0.95f,0.85f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f,  0.0f, 0.0f, true,  false, false, false, false},
    {"crying",      0.3f, 0.0f,  1.0f, 1.05f,-1.0f, 0.0f, 1.0f, 0.2f, 0.3f,  0.0f, 0.3f, true,  false, true,  false, false},
    {"loving",      0.0f, 0.25f, 1.12f,1.12f, 0.0f, 0.0f, 0.0f, 1.0f, 0.1f,  0.0f, 0.0f, false, false, false, false, false},
    {"embarrassed", 0.1f, 0.3f,  1.0f, 1.0f, -0.4f, 0.0f, 0.6f, 1.0f, 0.0f, -0.6f, 0.4f, false, false, false, false, false},
    {"surprised",   0.0f, 0.0f,  1.08f,1.18f, 0.0f, 1.0f, 0.8f, 0.3f, 0.55f, 0.0f, 0.0f, false, false, false, false, false},
    {"shocked",     0.0f, 0.0f,  1.15f,1.25f, 0.0f, 1.0f, 0.9f, 0.1f, 0.85f, 0.0f, 0.0f, false, false, false, false, false},
    {"thinking",    0.15f,0.0f,  1.0f, 1.0f, -0.3f, 0.3f, 0.6f, 0.3f, 0.0f, -0.5f,-0.7f, false, false, false, false, false},
    {"winking",     0.0f, 0.3f,  1.0f, 1.0f,  0.0f, 0.0f, 0.0f, 0.7f, 0.2f,  0.0f, 0.0f, false, true,  false, false, false},
    {"cool",        0.45f,0.2f,  1.0f, 1.0f,  0.4f, 0.0f, 0.5f, 0.3f, 0.0f,  0.0f, 0.0f, false, false, false, false, false},
    {"relaxed",     0.35f,0.3f,  1.0f, 1.0f,  0.0f, 0.0f, 0.0f, 0.5f, 0.0f,  0.0f, 0.0f, false, false, false, false, false},
    {"delicious",   0.0f, 0.6f,  1.0f, 1.0f,  0.0f, 0.0f, 0.0f, 0.8f, 0.4f,  0.0f, 0.0f, false, false, false, false, false},
    {"kissy",       0.0f, 0.5f,  1.0f, 1.0f,  0.0f, 0.0f, 0.0f, 1.0f, 0.0f,  0.0f, 0.0f, true,  false, false, false, false},
    {"confident",   0.2f, 0.2f,  1.0f, 1.0f,  0.3f, 0.0f, 0.6f, 0.4f, 0.0f,  0.0f, 0.0f, false, false, false, false, false},
    {"sleepy",      0.75f,0.0f,  1.0f, 1.0f,  0.0f, 0.0f, 0.0f, 0.3f, 0.0f,  0.0f, 0.2f, false, false, false, true,  false},
    {"silly",       0.0f, 0.2f,  1.05f,1.0f,  0.0f, 0.4f, 0.0f, 0.6f, 0.5f,  0.4f, 0.0f, false, true,  false, false, false},
    {"confused",    0.1f, 0.0f,  1.0f, 1.0f, -0.5f, 0.0f, 0.8f, 0.3f, 0.0f, -0.3f, 0.0f, false, false, false, false, false},
    // Reactions used by the motion sensor
    {"dizzy",       0.1f, 0.0f,  1.0f, 1.0f,  0.0f, 0.0f, 0.0f, 0.5f, 0.3f,  0.0f, 0.0f, true,  false, false, false, true},
    {"scared",      0.0f, 0.0f,  1.1f, 1.2f, -1.0f, 1.0f, 1.0f, 0.1f, 0.4f,  0.0f, 0.0f, true,  false, false, false, false},
    {"curious_left",0.0f, 0.0f,  1.05f,1.05f,-0.4f, 0.4f, 0.7f, 0.4f, 0.0f, -0.9f, 0.0f, false, false, false, false, false},
    {"curious_right",0.0f,0.0f,  1.05f,1.05f,-0.4f, 0.4f, 0.7f, 0.4f, 0.0f,  0.9f, 0.0f, false, false, false, false, false},
};
// clang-format on

static int RandomRange(int min_value, int max_value) {
    return min_value + (int)(esp_random() % (uint32_t)(max_value - min_value + 1));
}

static float RandomFloat(float min_value, float max_value) {
    return min_value + (max_value - min_value) * (esp_random() % 1000) / 1000.0f;
}

static lv_obj_t* MakeShape(lv_obj_t* parent, lv_color_t color) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static lv_obj_t* MakeArc(lv_obj_t* parent, lv_color_t color, int width) {
    lv_obj_t* arc = lv_arc_create(parent);
    lv_obj_remove_style_all(arc);
    lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, color, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    return arc;
}

static void SetHidden(lv_obj_t* obj, bool hidden) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) != hidden) {
        if (hidden) {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

FridayFace::FridayFace(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                       int width, int height, int offset_x, int offset_y, bool mirror_x,
                       bool mirror_y, bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y,
                    swap_xy) {
    expression_ = &kExpressions[0];
    const auto& e = *expression_;
    current_ = {e.lid, e.lid, e.lower, e.scale_x, e.scale_y, e.brow_tilt, e.brow_raise,
                e.brow_opa, e.cheek, e.mouth_open, e.look_x, e.look_y};
}

FridayFace::~FridayFace() {
    DisplayLockGuard lock(this);
    if (timer_ != nullptr) {
        lv_timer_delete(timer_);
    }
    if (face_ != nullptr) {
        lv_obj_delete(face_);
    }
}

void FridayFace::SetupUI() {
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI() called multiple times, skipping duplicate call");
        return;
    }
    SpiLcdDisplay::SetupUI();

    // Dark status text reads best on the cream face
    auto* light_theme = LvglThemeManager::GetInstance().GetTheme("light");
    if (light_theme != nullptr) {
        SetTheme(light_theme);
    }

    DisplayLockGuard lock(this);
    if (emoji_box_ != nullptr) {
        lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    }
    CreateFace();
    timer_ = lv_timer_create([](lv_timer_t* timer) {
        static_cast<FridayFace*>(lv_timer_get_user_data(timer))->Tick();
    }, kFrameMs, this);
}

void FridayFace::CreateFace() {
    auto screen = lv_screen_active();
    face_ = MakeShape(screen, kFaceColor);
    lv_obj_set_style_radius(face_, 0, 0);
    lv_obj_set_size(face_, width_, height_);
    lv_obj_set_pos(face_, 0, 0);
    // Above the stock background container, below status bar and subtitles
    lv_obj_move_to_index(face_, 1);

    for (int i = 0; i < 2; i++) {
        cheeks_[i] = MakeShape(face_, kCheekColor);
        lv_obj_set_size(cheeks_[i], 44, 24);
        lv_obj_set_pos(cheeks_[i], kCenterX + (i == 0 ? -kCheekOffsetX : kCheekOffsetX) - 22,
                       kCheekY - 12);
        lv_obj_set_style_bg_opa(cheeks_[i], LV_OPA_TRANSP, 0);
    }

    for (int i = 0; i < 2; i++) {
        Eye& eye = eyes_[i];
        eye.ball = MakeShape(face_, kEyeColor);
        eye.shine_big = MakeShape(eye.ball, kShineColor);
        eye.shine_small = MakeShape(eye.ball, kShineColor);
        // Lids are face-coloured shapes clipped by the eyeball
        eye.lid = MakeShape(eye.ball, kFaceColor);
        lv_obj_set_style_radius(eye.lid, 0, 0);
        eye.lower_lid = MakeShape(eye.ball, kFaceColor);

        eye.closed = MakeArc(face_, kEyeColor, 6);
        lv_arc_set_bg_angles(eye.closed, 20, 160);
        lv_obj_add_flag(eye.closed, LV_OBJ_FLAG_HIDDEN);

        eye.brow = lv_line_create(face_);
        lv_obj_set_style_line_width(eye.brow, 7, 0);
        lv_obj_set_style_line_color(eye.brow, kBrowColor, 0);
        lv_obj_set_style_line_rounded(eye.brow, true, 0);
        lv_obj_set_style_line_opa(eye.brow, LV_OPA_TRANSP, 0);
        eye.brow_points[0] = {0, 0};
        eye.brow_points[1] = {0, 0};
        lv_line_set_points(eye.brow, eye.brow_points, 2);
    }

    nose_ = MakeShape(face_, kEyeColor);
    lv_obj_set_size(nose_, 22, 14);
    lv_obj_set_pos(nose_, kCenterX - 11, kNoseY - 7);

    mouth_open_ = MakeShape(face_, kMouthInsideColor);
    lv_obj_set_style_radius(mouth_open_, 12, 0);
    tongue_ = MakeShape(mouth_open_, kTongueColor);
    lv_obj_add_flag(mouth_open_, LV_OBJ_FLAG_HIDDEN);

    int arc_size = kMouthArcRadius * 2 + 4;
    for (int i = 0; i < 2; i++) {
        mouth_arcs_[i] = MakeArc(face_, kEyeColor, 4);
        lv_obj_set_size(mouth_arcs_[i], arc_size, arc_size);
        int cx = kCenterX + (i == 0 ? -kMouthArcRadius : kMouthArcRadius);
        lv_obj_set_pos(mouth_arcs_[i], cx - arc_size / 2, kMouthY - arc_size / 2);
    }

    tear_ = MakeShape(face_, kTearColor);
    lv_obj_set_size(tear_, 12, 18);
    lv_obj_add_flag(tear_, LV_OBJ_FLAG_HIDDEN);

    zzz_ = lv_label_create(face_);
    lv_label_set_text(zzz_, "z z Z");
    lv_obj_set_style_text_color(zzz_, kBrowColor, 0);
    lv_obj_set_pos(zzz_, 170, 48);
    lv_obj_add_flag(zzz_, LV_OBJ_FLAG_HIDDEN);
}

const FridayFace::Expression* FridayFace::FindExpression(const std::string& name) {
    for (const auto& e : kExpressions) {
        if (name == e.name) {
            return &e;
        }
    }
    return nullptr;
}

bool FridayFace::IsKnownExpression(const std::string& name) {
    return FindExpression(name) != nullptr;
}

void FridayFace::SetEmotion(const char* emotion) {
    std::string name = emotion != nullptr ? emotion : "neutral";
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    const Expression* expression = FindExpression(name);
    if (expression == nullptr) {
        // Icon names used for alerts and status ("link", "wifi", "triangle_exclamation"...)
        bool trouble = name.find("exclamation") != std::string::npos ||
                       name.find("xmark") != std::string::npos;
        expression = FindExpression(trouble ? "confused" : "neutral");
    }

    {
        DisplayLockGuard lock(this);
        expression_ = expression;
    }
    if (on_emotion_) {
        on_emotion_(expression->name);
    }
}

std::string FridayFace::emotion() {
    DisplayLockGuard lock(this);
    return expression_->name;
}

void FridayFace::SetActivity(FaceActivity activity) {
    DisplayLockGuard lock(this);
    activity_ = activity;
}

void FridayFace::React(const std::string& expression, int duration_ms) {
    const Expression* e = FindExpression(expression);
    if (e == nullptr) {
        return;
    }
    DisplayLockGuard lock(this);
    reaction_ = e;
    reaction_until_us_ = esp_timer_get_time() + duration_ms * 1000LL;
}

float FridayFace::BlinkAmount(int64_t now) {
    if (next_blink_us_ == 0) {
        next_blink_us_ = now + RandomRange(1500, 4000) * 1000LL;
    }
    if (blink_start_us_ == 0 && now >= next_blink_us_) {
        blink_start_us_ = now;
    }
    if (blink_start_us_ == 0) {
        return 0;
    }
    int t = (int)((now - blink_start_us_) / 1000);
    if (t >= kBlinkMs) {
        if (double_blink_) {
            double_blink_ = false;
            blink_start_us_ = now + 60 * 1000;  // short pause, then blink again
            return 0;
        }
        blink_start_us_ = 0;
        double_blink_ = RandomRange(0, 4) == 0;
        next_blink_us_ = now + RandomRange(2000, 6000) * 1000LL;
        return 0;
    }
    if (t < 0) {
        return 0;
    }
    return 1.0f - fabsf(t / (kBlinkMs / 2.0f) - 1.0f);
}

void FridayFace::Tick() {
    if (face_ == nullptr) {
        return;
    }
    int64_t now = esp_timer_get_time();

    if (reaction_ != nullptr && now >= reaction_until_us_) {
        reaction_ = nullptr;
    }
    const Expression& e = reaction_ != nullptr ? *reaction_ : *expression_;
    bool sleeping = activity_ == FaceActivity::kSleeping && reaction_ == nullptr;

    Params target = {e.wink ? 1.0f : e.lid, e.lid, e.lower, e.scale_x, e.scale_y,
                     e.brow_tilt, e.brow_raise, e.brow_opa, e.cheek, e.mouth_open,
                     e.look_x, e.look_y};

    // Glance around now and then so Friday looks alive
    if (now >= next_glance_us_) {
        bool center = RandomRange(0, 9) < 4;
        float range = activity_ == FaceActivity::kListening ? 0.15f : 0.5f;
        glance_x_ = center ? 0 : RandomFloat(-range, range);
        glance_y_ = center ? 0 : RandomFloat(-range * 0.6f, range * 0.6f);
        next_glance_us_ = now + RandomRange(1200, 4000) * 1000LL;
    }
    if (reaction_ == nullptr) {
        target.look_x += glance_x_;
        target.look_y += glance_y_;
    }

    switch (activity_) {
        case FaceActivity::kListening:
            // Attentive puppy: slightly bigger eyes, fully open
            target.scale_x += 0.06f;
            target.scale_y += 0.06f;
            target.lid_r = std::min(target.lid_r, 0.02f);
            if (!e.wink) {
                target.lid_l = target.lid_r;
            }
            break;
        case FaceActivity::kThinking:
            if (reaction_ == nullptr && strcmp(e.name, "neutral") == 0) {
                target.look_x = -0.5f;
                target.look_y = -0.6f;
                target.brow_opa = 0.5f;
                target.brow_tilt = -0.2f;
            }
            break;
        case FaceActivity::kSpeaking:
            if (now >= next_mouth_us_) {
                talk_mouth_ = RandomFloat(0.1f, 0.8f);
                next_mouth_us_ = now + RandomRange(90, 160) * 1000LL;
            }
            target.mouth_open = std::max(e.mouth_open * 0.5f, talk_mouth_);
            break;
        default:
            break;
    }
    if (sleeping) {
        target.lid_l = target.lid_r = 0.95f;
        target.lower = 0;
        target.mouth_open = 0;
        target.look_x = 0;
        target.look_y = 0.2f;
    }
    if (e.dizzy) {
        float t = now / 1000000.0f;
        target.look_x = cosf(t * 7) * 0.8f;
        target.look_y = sinf(t * 7) * 0.8f;
    }

    // Ease every parameter towards its target
    float* cur = &current_.lid_l;
    const float* tgt = &target.lid_l;
    for (size_t i = 0; i < sizeof(Params) / sizeof(float); i++) {
        cur[i] += (tgt[i] - cur[i]) * 0.3f;
    }

    float blink = sleeping ? 0 : BlinkAmount(now);
    ApplyEye(0, std::max(current_.lid_l, blink), current_);
    ApplyEye(1, std::max(current_.lid_r, blink), current_);

    int cheek_opa = (int)(current_.cheek * (int)LV_OPA_70);
    if (cheek_opa != cheek_opa_) {
        cheek_opa_ = cheek_opa;
        for (auto cheek : cheeks_) {
            lv_obj_set_style_bg_opa(cheek, cheek_opa, 0);
        }
    }

    pout_ = e.pout && !sleeping;
    ApplyMouth(current_);

    SetHidden(zzz_, !(sleeping || e.zzz));

    SetHidden(tear_, !e.tear);
    if (e.tear) {
        float phase = (now / 1000 % 1400) / 1400.0f;
        int x = kCenterX - kEyeOffsetX - 34;
        lv_obj_set_pos(tear_, x, 178 + (int)(phase * 40));
        lv_obj_set_style_bg_opa(tear_, (lv_opa_t)(255 * (1.0f - phase)), 0);
    }
}

void FridayFace::ApplyEye(int index, float lid, const Params& p) {
    Eye& eye = eyes_[index];
    int w = (int)lroundf(kEyeW * p.scale_x);
    int h = (int)lroundf(kEyeH * p.scale_y);
    int cx = kCenterX + (index == 0 ? -kEyeOffsetX : kEyeOffsetX) + (int)lroundf(p.look_x * 12);
    int cy = kEyeY + (int)lroundf(p.look_y * 10);

    lv_obj_set_size(eye.ball, w, h);
    lv_obj_set_pos(eye.ball, cx - w / 2, cy - h / 2);

    // Big and small highlights give the glossy puppy look
    int big = (int)(w * 0.34f);
    lv_obj_set_size(eye.shine_big, big, big);
    lv_obj_set_pos(eye.shine_big, (int)(w * 0.50f), (int)(h * 0.12f));
    int small = (int)(w * 0.15f);
    lv_obj_set_size(eye.shine_small, small, small);
    lv_obj_set_pos(eye.shine_small, (int)(w * 0.22f), (int)(h * 0.60f));

    lid = std::clamp(lid, 0.0f, 1.0f);
    lv_obj_set_size(eye.lid, w, (int)(lid * h));
    lv_obj_set_pos(eye.lid, 0, 0);

    // A big face-coloured circle rising from below makes the ^^ happy squint
    int lower_d = (int)(w * 1.6f);
    lv_obj_set_size(eye.lower_lid, lower_d, lower_d);
    lv_obj_set_pos(eye.lower_lid, (w - lower_d) / 2, h - (int)(p.lower * h * 0.75f));

    // Fully shut eyes show a little curved line instead of nothing
    bool closed = lid > 0.9f;
    if (closed != eye.closed_visible) {
        eye.closed_visible = closed;
        SetHidden(eye.closed, !closed);
    }
    if (closed) {
        int d = (int)(w * 0.7f);
        lv_obj_set_size(eye.closed, d, d);
        lv_obj_set_pos(eye.closed, cx - d / 2, cy - (int)(d * 0.8f));
    }

    // Brows: tilt up in the middle when sad, down when angry
    int brow_opa = (int)(std::clamp(p.brow_opa, 0.0f, 1.0f) * 255);
    if (brow_opa != eye.brow_opa) {
        eye.brow_opa = brow_opa;
        lv_obj_set_style_line_opa(eye.brow, brow_opa, 0);
    }
    if (brow_opa > 10) {
        int base_y = cy - h / 2 - 16 - (int)(p.brow_raise * 8);
        int half = (int)(w * 0.35f);
        int inner_x = index == 0 ? cx + half : cx - half;
        int outer_x = index == 0 ? cx - half : cx + half;
        int inner_y = base_y + (int)(p.brow_tilt * 9);
        int outer_y = base_y - (int)(p.brow_tilt * 5);
        int coords[4] = {outer_x, outer_y, inner_x, inner_y};
        if (memcmp(coords, eye.last_brow, sizeof(coords)) != 0) {
            memcpy(eye.last_brow, coords, sizeof(coords));
            eye.brow_points[0] = {(lv_value_precise_t)outer_x, (lv_value_precise_t)outer_y};
            eye.brow_points[1] = {(lv_value_precise_t)inner_x, (lv_value_precise_t)inner_y};
            lv_line_set_points(eye.brow, eye.brow_points, 2);
        }
    }
}

void FridayFace::ApplyMouth(const Params& p) {
    if (pout_ != pout_applied_) {
        pout_applied_ = pout_;
        for (auto arc : mouth_arcs_) {
            // Two small arcs make the puppy "ω" mouth, flipped for a pout
            if (pout_) {
                lv_arc_set_bg_angles(arc, 200, 340);
            } else {
                lv_arc_set_bg_angles(arc, 0, 180);
            }
        }
    }

    int h = (int)(std::clamp(p.mouth_open, 0.0f, 1.0f) * 26);
    if (h < 4) {
        SetHidden(mouth_open_, true);
        return;
    }
    SetHidden(mouth_open_, false);
    int w = 26;
    lv_obj_set_size(mouth_open_, w, h);
    lv_obj_set_pos(mouth_open_, kCenterX - w / 2, kMouthY + 6);
    lv_obj_set_size(tongue_, 18, 18);
    lv_obj_set_pos(tongue_, (w - 18) / 2, h - 9);
}
