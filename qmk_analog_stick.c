// Copyright 2024 Hajime Oh-yake (@digitarhythm)
// SPDX-License-Identifier: MIT
//
// QMK Analog Stick Library

#include "qmk_analog_stick.h"
#include "print.h"
#include "eeprom.h"

#ifndef constrain
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#endif

#define CALIB_MAGIC  0xA55A

// EEPROM レイアウト（各 2 バイト, 計 10 バイト）
#define EEPROM_ADDR_MAGIC ((uint16_t *)(uintptr_t)(JOYSTICK_CALIB_EEPROM_ADDR + 0))
#define EEPROM_ADDR_X_MIN ((uint16_t *)(uintptr_t)(JOYSTICK_CALIB_EEPROM_ADDR + 2))
#define EEPROM_ADDR_X_MAX ((uint16_t *)(uintptr_t)(JOYSTICK_CALIB_EEPROM_ADDR + 4))
#define EEPROM_ADDR_Y_MIN ((uint16_t *)(uintptr_t)(JOYSTICK_CALIB_EEPROM_ADDR + 6))
#define EEPROM_ADDR_Y_MAX ((uint16_t *)(uintptr_t)(JOYSTICK_CALIB_EEPROM_ADDR + 8))

static uint16_t center_x = 512;
static uint16_t center_y = 512;

// 移動平均フィルタ用バッファ
static uint16_t buf_x[JOYSTICK_SMOOTHING];
static uint16_t buf_y[JOYSTICK_SMOOTHING];
static uint8_t  buf_idx = 0;

// サブピクセル蓄積用
static int32_t subpx_x = 0;
static int32_t subpx_y = 0;

// 加速用
static int32_t current_speed = 0;
static int32_t accel_accum = 0;

// 離し時の減速継続用（最後の有効方向を保持）
static int16_t  last_norm_x    = 0;
static int16_t  last_norm_y    = 0;
static uint32_t last_magnitude = 1;

// デバッグタイマー
static uint16_t debug_timer = 0;

// ADC レンジ（EEPROM またはデフォルト値で初期化される）
static uint16_t runtime_x_min;
static uint16_t runtime_x_max;
static uint16_t runtime_y_min;
static uint16_t runtime_y_max;

// キャリブレーション中フラグ・作業用変数
static bool     is_calibrating = false;
static uint16_t calib_x_min;
static uint16_t calib_x_max;
static uint16_t calib_y_min;
static uint16_t calib_y_max;

static void load_calibration(void) {
    if (eeprom_read_word(EEPROM_ADDR_MAGIC) == CALIB_MAGIC) {
        runtime_x_min = eeprom_read_word(EEPROM_ADDR_X_MIN);
        runtime_x_max = eeprom_read_word(EEPROM_ADDR_X_MAX);
        runtime_y_min = eeprom_read_word(EEPROM_ADDR_Y_MIN);
        runtime_y_max = eeprom_read_word(EEPROM_ADDR_Y_MAX);
#if JOYSTICK_DEBUG
        uprintf("AnalogStick EEPROM calib: X=%u~%u Y=%u~%u\n",
                runtime_x_min, runtime_x_max, runtime_y_min, runtime_y_max);
#endif
    } else {
        runtime_x_min = JOYSTICK_ADC_X_MIN;
        runtime_x_max = JOYSTICK_ADC_X_MAX;
        runtime_y_min = JOYSTICK_ADC_Y_MIN;
        runtime_y_max = JOYSTICK_ADC_Y_MAX;
#if JOYSTICK_DEBUG
        uprintf("AnalogStick calib: default X=%u~%u Y=%u~%u\n",
                runtime_x_min, runtime_x_max, runtime_y_min, runtime_y_max);
#endif
    }

}

static uint16_t read_smoothed(pin_t pin, uint16_t *buf) {
    buf[buf_idx] = analogReadPin(pin);
    uint32_t sum = 0;
    for (uint8_t i = 0; i < JOYSTICK_SMOOTHING; i++) {
        sum += buf[i];
    }
    return sum / JOYSTICK_SMOOTHING;
}

// 軸ごとの正規化（-1000〜+1000 にスケーリング、デッドゾーンなし）
static int16_t normalize_axis(uint16_t val, uint16_t center, uint16_t adc_min, uint16_t adc_max) {
    int16_t delta = (int16_t)val - (int16_t)center;
    if (delta == 0) return 0;

    int16_t range = (delta > 0)
        ? (adc_max - center)
        : (center - adc_min);
    if (range < 1) range = 1;

    int32_t scaled = (int32_t)delta * 1000 / range;
    return (int16_t)constrain(scaled, -1000, 1000);
}

// 簡易整数平方根（ニュートン法）
static uint32_t isqrt(uint32_t n) {
    if (n == 0) return 0;
    uint32_t x = n;
    uint32_t y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

void analog_stick_init(void) {
    // TMRセンサーが安定するまで待機
    wait_ms(JOYSTICK_WARMUP_MS);

    // 中心値を複数回サンプリングして平均化
    uint32_t sum_x = 0;
    uint32_t sum_y = 0;
    for (uint8_t i = 0; i < 64; i++) {
        sum_x += analogReadPin(JOYSTICK_X_PIN);
        sum_y += analogReadPin(JOYSTICK_Y_PIN);
        wait_ms(2);
    }
    center_x = sum_x / 64;
    center_y = sum_y / 64;

    // スムージングバッファを中心値で初期化
    for (uint8_t i = 0; i < JOYSTICK_SMOOTHING; i++) {
        buf_x[i] = center_x;
        buf_y[i] = center_y;
    }

#ifdef JOYSTICK_SW_PIN
    setPinInputHigh(JOYSTICK_SW_PIN);
#endif

    // EEPROM からレンジキャリブレーションを読み込む
    load_calibration();

#if JOYSTICK_DEBUG
    uprintf("AnalogStick center: X=%u Y=%u\n", center_x, center_y);
#endif
}

report_mouse_t analog_stick_update(report_mouse_t mouse_report) {
    uint16_t smooth_x = read_smoothed(JOYSTICK_X_PIN, buf_x);
    uint16_t smooth_y = read_smoothed(JOYSTICK_Y_PIN, buf_y);
    buf_idx = (buf_idx + 1) % JOYSTICK_SMOOTHING;

    // キャリブレーション中: raw 値で min/max を更新
    if (is_calibrating) {
        uint16_t raw_x = analogReadPin(JOYSTICK_X_PIN);
        uint16_t raw_y = analogReadPin(JOYSTICK_Y_PIN);
        if (raw_x < calib_x_min) calib_x_min = raw_x;
        if (raw_x > calib_x_max) calib_x_max = raw_x;
        if (raw_y < calib_y_min) calib_y_min = raw_y;
        if (raw_y > calib_y_max) calib_y_max = raw_y;
    }

    // 各軸を -1000〜+1000 に正規化（ランタイム変数を使用）
    int16_t norm_x = normalize_axis(smooth_x, center_x, runtime_x_min, runtime_x_max);
    int16_t norm_y = normalize_axis(smooth_y, center_y, runtime_y_min, runtime_y_max);

    // 合成ベクトルの大きさ（0〜1000）を計算
    uint32_t magnitude = isqrt((uint32_t)((int32_t)norm_x * norm_x + (int32_t)norm_y * norm_y));
    if (magnitude > 1000) magnitude = 1000;

    // 円形デッドゾーン: ランタイムレンジから正規化スケールに変換
    uint32_t adc_half_range = ((uint32_t)(runtime_x_max - runtime_x_min) +
                               (uint32_t)(runtime_y_max - runtime_y_min)) / 4;
    if (adc_half_range < 1) adc_half_range = 1;
    uint32_t deadzone_normalized = (uint32_t)JOYSTICK_DEADZONE * 1000 / adc_half_range;

    if (magnitude > deadzone_normalized) {
        // デッドゾーン分を差し引いてスケーリング
        int32_t effective = (int32_t)magnitude - (int32_t)deadzone_normalized;
        int32_t effective_max = 1000 - (int32_t)deadzone_normalized;
        if (effective_max < 1) effective_max = 1;
        uint32_t adjusted_magnitude = (uint32_t)effective * 1000 / effective_max;
        if (adjusted_magnitude > 1000) adjusted_magnitude = 1000;

        // 現在の傾き量に基づく速度上限（二乗カーブ）
        int32_t speed_limit = (int32_t)(adjusted_magnitude * adjusted_magnitude / 1000) * JOYSTICK_MAX_SPEED / 1000;

        if (current_speed < speed_limit) {
            accel_accum += (int32_t)adjusted_magnitude * adjusted_magnitude * JOYSTICK_ACCEL_RATE;
            current_speed += accel_accum / 1000000;
            accel_accum %= 1000000;
            if (current_speed > speed_limit) current_speed = speed_limit;
        } else if (current_speed > speed_limit) {
            int32_t decel = (current_speed - speed_limit) * JOYSTICK_DECEL_RATE / 100;
            if (decel < 1) decel = 1;
            current_speed -= decel;
            if (current_speed < speed_limit) current_speed = speed_limit;
            accel_accum = 0;
        }

        last_norm_x    = norm_x;
        last_norm_y    = norm_y;
        last_magnitude = magnitude;

        int32_t speed_x = current_speed * (int32_t)norm_x / (int32_t)magnitude;
        int32_t speed_y = current_speed * (int32_t)norm_y / (int32_t)magnitude;
        subpx_x += speed_x;
        subpx_y += speed_y;
    } else {
        accel_accum = 0;
        if (current_speed > 0) {
            int32_t decel = current_speed * JOYSTICK_DECEL_RATE / 100;
            if (decel < 1) decel = 1;
            current_speed -= decel;
            if (current_speed < 0) current_speed = 0;

            int32_t speed_x = current_speed * (int32_t)last_norm_x / (int32_t)last_magnitude;
            int32_t speed_y = current_speed * (int32_t)last_norm_y / (int32_t)last_magnitude;
            subpx_x += speed_x;
            subpx_y += speed_y;
        } else {
            subpx_x = 0;
            subpx_y = 0;
        }
    }

    mouse_report.x = (int8_t)constrain(subpx_x / 1000, -127, 127);
    mouse_report.y = (int8_t)constrain(subpx_y / 1000, -127, 127);
    subpx_x -= mouse_report.x * 1000;
    subpx_y -= mouse_report.y * 1000;

#ifdef JOYSTICK_SW_PIN
    if (readPin(JOYSTICK_SW_PIN) == 0) {
        mouse_report.buttons |= JOYSTICK_SW_BUTTON;
    } else {
        mouse_report.buttons &= ~JOYSTICK_SW_BUTTON;
    }
#endif

#if JOYSTICK_DEBUG
    if (++debug_timer >= 100) {
        debug_timer = 0;
        if (is_calibrating) {
            uprintf("CALIB: X=%u~%u Y=%u~%u\n",
                    calib_x_min, calib_x_max, calib_y_min, calib_y_max);
        } else {
            uprintf("Xr=%u cx=%u dx=%d | Yr=%u cy=%u dy=%d | spd=%ld\n",
                    smooth_x, center_x, mouse_report.x,
                    smooth_y, center_y, mouse_report.y,
                    current_speed);
        }
    }
#endif

    return mouse_report;
}

// ===== キャリブレーション API =====

void analog_stick_calibration_start(void) {
    // 初期値: ありえない極値にしておき、実測値で上書きさせる
    calib_x_min = 0xFFFF;
    calib_x_max = 0;
    calib_y_min = 0xFFFF;
    calib_y_max = 0;
    is_calibrating = true;
#if JOYSTICK_DEBUG
    uprintf("AnalogStick: calibration started\n");
#endif
}

void analog_stick_calibration_end(void) {
    if (!is_calibrating) return;
    is_calibrating = false;

    // 範囲が小さすぎる場合は無効
    if ((calib_x_max - calib_x_min) < 100 || (calib_y_max - calib_y_min) < 100) {
#if JOYSTICK_DEBUG
        uprintf("AnalogStick: calibration failed (range too small)\n");
#endif
        return;
    }

    runtime_x_min = calib_x_min;
    runtime_x_max = calib_x_max;
    runtime_y_min = calib_y_min;
    runtime_y_max = calib_y_max;

    eeprom_write_word(EEPROM_ADDR_MAGIC, CALIB_MAGIC);
    eeprom_write_word(EEPROM_ADDR_X_MIN, runtime_x_min);
    eeprom_write_word(EEPROM_ADDR_X_MAX, runtime_x_max);
    eeprom_write_word(EEPROM_ADDR_Y_MIN, runtime_y_min);
    eeprom_write_word(EEPROM_ADDR_Y_MAX, runtime_y_max);

#if JOYSTICK_DEBUG
    uprintf("AnalogStick: calibration saved X=%u~%u Y=%u~%u\n",
            runtime_x_min, runtime_x_max, runtime_y_min, runtime_y_max);
#endif
}

void analog_stick_calibration_reset(void) {
    eeprom_write_word(EEPROM_ADDR_MAGIC, 0x0000);
    runtime_x_min = JOYSTICK_ADC_X_MIN;
    runtime_x_max = JOYSTICK_ADC_X_MAX;
    runtime_y_min = JOYSTICK_ADC_Y_MIN;
    runtime_y_max = JOYSTICK_ADC_Y_MAX;
#if JOYSTICK_DEBUG
    uprintf("AnalogStick: calibration reset to defaults\n");
#endif
}

bool analog_stick_is_calibrating(void) {
    return is_calibrating;
}

void analog_stick_get_scroll_values(int16_t *out_x, int16_t *out_y) {
    uint16_t smooth_x = read_smoothed(JOYSTICK_X_PIN, buf_x);
    uint16_t smooth_y = read_smoothed(JOYSTICK_Y_PIN, buf_y);
    buf_idx = (buf_idx + 1) % JOYSTICK_SMOOTHING;

    int16_t norm_x = normalize_axis(smooth_x, center_x, runtime_x_min, runtime_x_max);
    int16_t norm_y = normalize_axis(smooth_y, center_y, runtime_y_min, runtime_y_max);

    uint32_t magnitude = isqrt((uint32_t)((int32_t)norm_x * norm_x + (int32_t)norm_y * norm_y));
    if (magnitude > 1000) magnitude = 1000;

    uint32_t adc_half_range = ((uint32_t)(runtime_x_max - runtime_x_min) +
                               (uint32_t)(runtime_y_max - runtime_y_min)) / 4;
    if (adc_half_range < 1) adc_half_range = 1;
    uint32_t deadzone_normalized = (uint32_t)JOYSTICK_DEADZONE * 1000 / adc_half_range;

    if (magnitude <= deadzone_normalized) {
        *out_x = 0;
        *out_y = 0;
    } else {
        int32_t effective     = (int32_t)magnitude - (int32_t)deadzone_normalized;
        int32_t effective_max = 1000 - (int32_t)deadzone_normalized;
        if (effective_max < 1) effective_max = 1;
        int32_t scale = effective * 1000 / effective_max;
        int32_t mag   = (int32_t)(magnitude > 0 ? magnitude : 1);
        *out_x = (int16_t)((int32_t)norm_x * scale / mag);
        *out_y = (int16_t)((int32_t)norm_y * scale / mag);
    }

    // カーソルモードに戻ったとき跳ばないよう加速状態をリセット
    current_speed = 0;
    accel_accum   = 0;
    subpx_x       = 0;
    subpx_y       = 0;
}
