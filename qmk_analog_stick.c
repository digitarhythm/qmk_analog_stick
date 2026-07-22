// Copyright 2024 Hajime Oh-yake (@digitarhythm)
// SPDX-License-Identifier: MIT
//
// QMK Analog Stick Library

#include "qmk_analog_stick.h"
#include "print.h"

#ifndef constrain
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#endif

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

// ADC レンジ（モデル選択により compile 時に決定）
static uint16_t runtime_x_min;
static uint16_t runtime_x_max;
static uint16_t runtime_y_min;
static uint16_t runtime_y_max;

#ifdef JOYSTICK_ADAPTIVE_RANGE

#if JOYSTICK_RANGE_SAVE
#include "eeprom.h"
#ifndef JOYSTICK_EEPROM_ADDR
// VIA のカスタム設定領域を使用
// config.h に VIA_EEPROM_CUSTOM_CONFIG_SIZE 10 の定義が必要
#include "via.h"
#include "quantum/nvm/eeprom/nvm_eeprom_eeconfig_internal.h"
#include "quantum/nvm/eeprom/nvm_eeprom_via_internal.h"
#define JOYSTICK_EEPROM_ADDR VIA_EEPROM_CUSTOM_CONFIG_ADDR
// カスタム設定領域の予約が足りないと、この直後から始まる
// ダイナミックキーマップ領域を侵食してしまうためコンパイル時に検出する
STATIC_ASSERT(VIA_EEPROM_CUSTOM_CONFIG_SIZE >= 10,
              "Define VIA_EEPROM_CUSTOM_CONFIG_SIZE >= 10 in config.h to reserve EEPROM space for the joystick range");
#endif
// 保存レイアウト: magic(2) + x_min(2) + x_max(2) + y_min(2) + y_max(2) = 10バイト
#define JOYSTICK_RANGE_MAGIC 0xAD01

static bool     range_dirty      = false;
static uint16_t range_save_timer = 0;

static void save_range(void) {
    uint16_t *addr = (uint16_t *)(uintptr_t)JOYSTICK_EEPROM_ADDR;
    eeprom_update_word(addr + 0, JOYSTICK_RANGE_MAGIC);
    eeprom_update_word(addr + 1, runtime_x_min);
    eeprom_update_word(addr + 2, runtime_x_max);
    eeprom_update_word(addr + 3, runtime_y_min);
    eeprom_update_word(addr + 4, runtime_y_max);
#if JOYSTICK_DEBUG
    uprintf("AnalogStick range saved: X=%u~%u Y=%u~%u\n",
            runtime_x_min, runtime_x_max, runtime_y_min, runtime_y_max);
#endif
}

// 保存済みレンジを読み込み、現在の初期レンジと統合する（広い方を採用）
static void load_range(void) {
    uint16_t *addr = (uint16_t *)(uintptr_t)JOYSTICK_EEPROM_ADDR;
    if (eeprom_read_word(addr) != JOYSTICK_RANGE_MAGIC) return;

    uint16_t x_min = eeprom_read_word(addr + 1);
    uint16_t x_max = eeprom_read_word(addr + 2);
    uint16_t y_min = eeprom_read_word(addr + 3);
    uint16_t y_max = eeprom_read_word(addr + 4);

    // 妥当性チェック: 現在の中心値がレンジ内に収まっていること
    if (x_min < center_x && center_x < x_max && x_max <= 1023 &&
        y_min < center_y && center_y < y_max && y_max <= 1023) {
        if (x_min < runtime_x_min) runtime_x_min = x_min;
        if (x_max > runtime_x_max) runtime_x_max = x_max;
        if (y_min < runtime_y_min) runtime_y_min = y_min;
        if (y_max > runtime_y_max) runtime_y_max = y_max;
    }
}
#endif // JOYSTICK_RANGE_SAVE

// 自動レンジ学習: 実測値（平滑化済み）がレンジ外に出たら拡張する
// 拡張が止まって一定時間経過したら EEPROM へ保存（有効時）
static void adapt_range(uint16_t smooth_x, uint16_t smooth_y) {
    bool changed = false;
    if (smooth_x < runtime_x_min) { runtime_x_min = smooth_x; changed = true; }
    if (smooth_x > runtime_x_max) { runtime_x_max = smooth_x; changed = true; }
    if (smooth_y < runtime_y_min) { runtime_y_min = smooth_y; changed = true; }
    if (smooth_y > runtime_y_max) { runtime_y_max = smooth_y; changed = true; }

#if JOYSTICK_RANGE_SAVE
    if (changed) {
        range_dirty      = true;
        range_save_timer = timer_read();
    } else if (range_dirty && timer_elapsed(range_save_timer) >= JOYSTICK_RANGE_SAVE_DELAY_MS) {
        range_dirty = false;
        save_range();
    }
#else
    (void)changed;
#endif
}
#endif

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

// 取り付け向きの補正（正規化済みの norm_x/norm_y を 90°単位で回転）
//   0:標準  1:時計回り90°(上→右)  2:180°(上→下)  3:反時計回り90°(上→左)
static void apply_orientation(int16_t *x, int16_t *y) {
#if JOYSTICK_ORIENTATION == 1
    int16_t tx = *x;
    *x = -(*y);
    *y = tx;
#elif JOYSTICK_ORIENTATION == 2
    *x = -(*x);
    *y = -(*y);
#elif JOYSTICK_ORIENTATION == 3
    int16_t tx = *x;
    *x = *y;
    *y = -tx;
#else
    (void)x;
    (void)y;   // JOYSTICK_ORIENTATION == 0: 何もしない
#endif
}

void analog_stick_init(void) {
    // TMRセンサーが安定するまで待機
    wait_ms(JOYSTICK_WARMUP_MS);

    // 中心値を複数回サンプリングして平均化（接続時自動センタリング）
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

#ifdef JOYSTICK_ADAPTIVE_RANGE
    // 自動レンジ学習: 中心±初期レンジから開始し、使用中に実測値で拡張
    runtime_x_min = (center_x > JOYSTICK_INITIAL_RANGE) ? center_x - JOYSTICK_INITIAL_RANGE : 0;
    runtime_x_max = (center_x + JOYSTICK_INITIAL_RANGE < 1023) ? center_x + JOYSTICK_INITIAL_RANGE : 1023;
    runtime_y_min = (center_y > JOYSTICK_INITIAL_RANGE) ? center_y - JOYSTICK_INITIAL_RANGE : 0;
    runtime_y_max = (center_y + JOYSTICK_INITIAL_RANGE < 1023) ? center_y + JOYSTICK_INITIAL_RANGE : 1023;
#if JOYSTICK_RANGE_SAVE
    // 前回保存した学習レンジがあれば初期値として読み込む
    load_range();
#endif
#else
    // ADC レンジをモデル定数で設定
    runtime_x_min = JOYSTICK_ADC_X_MIN;
    runtime_x_max = JOYSTICK_ADC_X_MAX;
    runtime_y_min = JOYSTICK_ADC_Y_MIN;
    runtime_y_max = JOYSTICK_ADC_Y_MAX;
#endif

#if JOYSTICK_DEBUG
    uprintf("AnalogStick center: X=%u Y=%u\n", center_x, center_y);
    uprintf("AnalogStick range: X=%u~%u Y=%u~%u\n",
            runtime_x_min, runtime_x_max, runtime_y_min, runtime_y_max);
#endif
}

report_mouse_t analog_stick_update(report_mouse_t mouse_report) {
    uint16_t smooth_x = read_smoothed(JOYSTICK_X_PIN, buf_x);
    uint16_t smooth_y = read_smoothed(JOYSTICK_Y_PIN, buf_y);
    buf_idx = (buf_idx + 1) % JOYSTICK_SMOOTHING;

#ifdef JOYSTICK_ADAPTIVE_RANGE
    adapt_range(smooth_x, smooth_y);
#endif

    // 各軸を -1000〜+1000 に正規化
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

#ifdef JOYSTICK_ACCEL_THRESHOLD
        if (adjusted_magnitude <= JOYSTICK_ACCEL_THRESHOLD) {
            // 直接ゾーン: 傾き量に比例した速度をそのまま使用（時間加速なし）
            int32_t target = (int32_t)adjusted_magnitude * JOYSTICK_DIRECT_SPEED / JOYSTICK_ACCEL_THRESHOLD;
            if (current_speed > target) {
                // 加速ゾーンから戻ったときは目標速度へ滑らかに減速
                int32_t decel = (current_speed - target) * JOYSTICK_DECEL_RATE / 100;
                if (decel < 1) decel = 1;
                current_speed -= decel;
                if (current_speed < target) current_speed = target;
            } else {
                current_speed = target;
            }
            accel_accum = 0;
        } else {
            // 加速ゾーン: しきい値超過分（0〜1000 に正規化）の二乗を加速度に変換
            int32_t excess = (int32_t)(adjusted_magnitude - JOYSTICK_ACCEL_THRESHOLD) * 1000
                             / (1000 - JOYSTICK_ACCEL_THRESHOLD);
            accel_accum += excess * excess * JOYSTICK_ACCEL_RATE;
            current_speed += accel_accum / 1000000;
            accel_accum %= 1000000;
            if (current_speed < JOYSTICK_DIRECT_SPEED) current_speed = JOYSTICK_DIRECT_SPEED;
            if (current_speed > JOYSTICK_MAX_SPEED) current_speed = JOYSTICK_MAX_SPEED;
        }
#else
        // 現在の傾き量に基づく速度上限（JOYSTICK_CURVE_POWER 乗カーブ）
        int32_t curved = (int32_t)adjusted_magnitude;
        for (uint8_t p = 1; p < JOYSTICK_CURVE_POWER; p++) {
            curved = curved * (int32_t)adjusted_magnitude / 1000;
        }
        // 倒し始めの移動量を JOYSTICK_CURVE_LOW_GAIN/1000 倍に抑え、
        // 全倒しで従来と同じ最高速に到達するようブレンドする
        curved = ((int32_t)JOYSTICK_CURVE_LOW_GAIN * curved +
                  (1000 - (int32_t)JOYSTICK_CURVE_LOW_GAIN) * (curved * curved / 1000)) / 1000;
        int32_t speed_limit = curved * JOYSTICK_MAX_SPEED / 1000;

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
#endif

        last_norm_x    = norm_x;
        last_norm_y    = norm_y;
        last_magnitude = magnitude;

        // 取り付け向きの補正
        apply_orientation(&norm_x, &norm_y);

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
        uprintf("Xr=%u cx=%u dx=%d | Yr=%u cy=%u dy=%d | spd=%ld\n",
                smooth_x, center_x, mouse_report.x,
                smooth_y, center_y, mouse_report.y,
                current_speed);
    }
#endif

    return mouse_report;
}

void analog_stick_get_scroll_values(int16_t *out_x, int16_t *out_y) {
    uint16_t smooth_x = read_smoothed(JOYSTICK_X_PIN, buf_x);
    uint16_t smooth_y = read_smoothed(JOYSTICK_Y_PIN, buf_y);
    buf_idx = (buf_idx + 1) % JOYSTICK_SMOOTHING;

#ifdef JOYSTICK_ADAPTIVE_RANGE
    adapt_range(smooth_x, smooth_y);
#endif

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

        // 取り付け向きの補正
        apply_orientation(&norm_x, &norm_y);

        *out_x = (int16_t)((int32_t)norm_x * scale / mag);
        *out_y = (int16_t)((int32_t)norm_y * scale / mag);
    }

    // カーソルモードに戻ったとき跳ばないよう加速状態をリセット
    current_speed = 0;
    accel_accum   = 0;
    subpx_x       = 0;
    subpx_y       = 0;
}
