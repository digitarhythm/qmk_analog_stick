// Copyright 2024 Hajime Oh-yake (@digitarhythm)
// SPDX-License-Identifier: MIT
//
// QMK Analog Stick Library
// アナログジョイスティックをマウスカーソル操作に使用するライブラリ
//
// 使い方:
//   1. config.h で必須パラメータ（JOYSTICK_X_PIN, JOYSTICK_Y_PIN）を定義
//   2. config.h でジョイスティックモデル（#define JH16 または #define JS16）を定義
//      （省略すると自動レンジ学習モードで動作）
//   3. config.h で任意のパラメータを上書き定義（デフォルト値あり）
//   4. keymap.c で #include "qmk_analog_stick.h" する
//   5. keyboard_post_init_user() 内で analog_stick_init() を呼ぶ
//   6. pointing_device_task_user() 内で analog_stick_update() を呼ぶ
//   7. rules.mk に SRC += qmk_analog_stick.c を追加
//   8. rules.mk に POINTING_DEVICE_ENABLE = yes / POINTING_DEVICE_DRIVER = custom を追加
//   9. halconf.h に HAL_USE_ADC TRUE を定義
//  10. mcuconf.h に RP_ADC_USE_ADC1 TRUE を定義

#pragma once

#include "quantum.h"
#include "pointing_device.h"
#include "analog.h"

// ===== 必須パラメータ（config.h で定義必須） =====
// #define JOYSTICK_X_PIN GP28
// #define JOYSTICK_Y_PIN GP29

// ===== ジョイスティックモデル選択（config.h で定義、省略可） =====
// #define JH16   // X: 8〜1023 / Y: 8〜782
// #define JS16   // X: 0〜1023 / Y: 0〜1023
//
// どちらも定義しない場合は「自動レンジ学習モード」になる:
//   起動時に 中心±JOYSTICK_INITIAL_RANGE の控えめなレンジから開始し、
//   スティックを倒すたびに実測値で ADC レンジを自動拡張する。
//   数回全倒しすると実機のレンジに収束するため、モデルの区別が不要になる。

// ADC レンジ（モデルに応じて決定、個別に上書きも可能）
#if defined(JH16)
#  ifndef JOYSTICK_ADC_X_MIN
#    define JOYSTICK_ADC_X_MIN    8
#  endif
#  ifndef JOYSTICK_ADC_X_MAX
#    define JOYSTICK_ADC_X_MAX 1023
#  endif
#  ifndef JOYSTICK_ADC_Y_MIN
#    define JOYSTICK_ADC_Y_MIN    8
#  endif
#  ifndef JOYSTICK_ADC_Y_MAX
#    define JOYSTICK_ADC_Y_MAX  782
#  endif
#elif defined(JS16)
#  ifndef JOYSTICK_ADC_X_MIN
#    define JOYSTICK_ADC_X_MIN    0
#  endif
#  ifndef JOYSTICK_ADC_X_MAX
#    define JOYSTICK_ADC_X_MAX 1023
#  endif
#  ifndef JOYSTICK_ADC_Y_MIN
#    define JOYSTICK_ADC_Y_MIN    0
#  endif
#  ifndef JOYSTICK_ADC_Y_MAX
#    define JOYSTICK_ADC_Y_MAX 1023
#  endif
#elif defined(JOYSTICK_ADC_X_MIN) && defined(JOYSTICK_ADC_X_MAX) && \
      defined(JOYSTICK_ADC_Y_MIN) && defined(JOYSTICK_ADC_Y_MAX)
// モデル未指定でも 4 つの ADC レンジが手動定義されていれば固定レンジで動作
#else
// 自動レンジ学習モード
#  define JOYSTICK_ADAPTIVE_RANGE 1
// 自動学習の初期半レンジ（中心±この値から開始し、実測値で拡張される）
// 実機の最小半レンジより小さい値にすること（大きいと最高速に到達できなくなる）
#  ifndef JOYSTICK_INITIAL_RANGE
#    define JOYSTICK_INITIAL_RANGE 250
#  endif
// 学習レンジの不揮発保存
//   1: レンジ拡張が止まってから JOYSTICK_RANGE_SAVE_DELAY_MS 後に EEPROM へ保存し、
//      次回起動時に初期レンジとして読み込む（起動ごとの再学習が不要になる）
//   0: 保存しない（起動ごとに再学習）
// VIA/Vial 環境ではカスタム設定領域を使用するため、config.h に
//   #define VIA_EEPROM_CUSTOM_CONFIG_SIZE 10
// の定義が必要。VIA 無効環境では JOYSTICK_EEPROM_ADDR を手動定義すること。
#  ifndef JOYSTICK_RANGE_SAVE
#    if defined(VIA_ENABLE) || defined(JOYSTICK_EEPROM_ADDR)
#      define JOYSTICK_RANGE_SAVE 1
#    else
#      define JOYSTICK_RANGE_SAVE 0
#    endif
#  endif
// レンジ拡張が止まってから保存するまでの待ち時間（ms）
// スティックを回している間の細かい拡張を 1 回の書き込みにまとめる
#  ifndef JOYSTICK_RANGE_SAVE_DELAY_MS
#    define JOYSTICK_RANGE_SAVE_DELAY_MS 3000
#  endif
#endif

// ===== オプションパラメータ（config.h で上書き可能） =====

// ボタンピン（定義するとマウスクリック機能が有効になる）
// #define JOYSTICK_SW_PIN GP13

// ボタンに割り当てるマウスボタン
#ifndef JOYSTICK_SW_BUTTON
#define JOYSTICK_SW_BUTTON MOUSE_BTN1   // 左クリック
#endif

// 取り付け向きの補正（出力を90°単位で回転）
//   0: 標準（現状のまま）
//   1: 現在の「上」を「右」にする（時計回り90°）
//   2: 現在の「上」を「下」にする（180°）
//   3: 現在の「上」を「左」にする（反時計回り90°）
#ifndef JOYSTICK_ORIENTATION
#define JOYSTICK_ORIENTATION 0
#endif

// 中心付近の不感帯（ADC値）
#ifndef JOYSTICK_DEADZONE
#define JOYSTICK_DEADZONE    30
#endif

// レンジ端マージン（%、0〜50）
// ADC レンジの端から手前この割合の位置で正規化値が ±1000（最大傾き）に達する。
// レンジ端まで物理的に倒しきれない個体や、一度きりの深押し・スパイクで
// 学習レンジが広がった場合でも、全方向で確実に最高速へ到達できるようにする
#ifndef JOYSTICK_RANGE_MARGIN
#define JOYSTICK_RANGE_MARGIN 10
#endif
#if JOYSTICK_RANGE_MARGIN < 0 || JOYSTICK_RANGE_MARGIN > 50
#  error "JOYSTICK_RANGE_MARGIN は 0〜50 の範囲で定義してください"
#endif

// 速度設定（x1000スケール: 1000 = 1.0ピクセル/サイクル）
#ifndef JOYSTICK_MAX_SPEED
#define JOYSTICK_MAX_SPEED  6000   // 最大速度（8.0）
#endif

// ===== ハイブリッド速度モード（デフォルト有効） =====
// 傾き量がしきい値以下: 傾きに比例した速度をそのまま出力（時間加速なし・即応答）
//   → 倒し始めからしきい値までは緩やかに速度が変化する
// 傾き量がしきい値超え: 超過分の二乗に比例した加速度で MAX_SPEED まで加速
//   → 深く倒し込むと加速して素早く移動できる
// このモード中は JOYSTICK_CURVE_POWER / JOYSTICK_CURVE_LOW_GAIN は使用されない
// config.h で 0 を定義すると無効になり、従来のカーブ方式で動作する
#ifndef JOYSTICK_ACCEL_THRESHOLD
#define JOYSTICK_ACCEL_THRESHOLD 900   // しきい値（0〜999、90% = 900、0 で無効）
#endif
#if JOYSTICK_ACCEL_THRESHOLD < 0 || JOYSTICK_ACCEL_THRESHOLD > 999
#  error "JOYSTICK_ACCEL_THRESHOLD は 0〜999 の範囲で定義してください（0 で無効）"
#endif
// 直接ゾーンの最高速（しきい値ちょうどの傾きでの速度、x1000スケール）
#ifndef JOYSTICK_DIRECT_SPEED
#define JOYSTICK_DIRECT_SPEED 600
#endif

// 速度カーブの指数（傾き量→速度の変化曲線）
//   1: リニア
//   2: 二次関数（デフォルト）
//   3: 三次関数（小さい傾きがより緻密になり、大きく倒すと急激に速くなる）
#ifndef JOYSTICK_CURVE_POWER
#define JOYSTICK_CURVE_POWER 2
#endif

// 倒し始めの移動量の倍率（0〜1000）
//   1000: 従来どおり
//    500: 倒し始めの移動量が従来の半分になり、そこから緩やかに増えて
//         全倒しで従来と同じ最高速に到達する
#ifndef JOYSTICK_CURVE_LOW_GAIN
#define JOYSTICK_CURVE_LOW_GAIN 1000
#endif

// 加速度設定
// 傾き量の二乗 × この値が毎サイクルの加速度になる
// 大きいほど速く加速する
#ifndef JOYSTICK_ACCEL_RATE
#define JOYSTICK_ACCEL_RATE   16   // 全倒しで約5秒でMAX_SPEEDに到達
#endif

// 減速率（%/サイクル）
// スティックを戻したとき、目標速度との差のこの割合ずつ減速する
// 大きいほどスティックの戻し量に素早く追従する（0〜100）
#ifndef JOYSTICK_DECEL_RATE
#define JOYSTICK_DECEL_RATE   8
#endif

// 移動平均サンプル数
#ifndef JOYSTICK_SMOOTHING
#define JOYSTICK_SMOOTHING    4
#endif

// TMRセンサー安定待ち時間
#ifndef JOYSTICK_WARMUP_MS
#define JOYSTICK_WARMUP_MS 1000
#endif

// デバッグ出力の有効/無効
#ifndef JOYSTICK_DEBUG
#define JOYSTICK_DEBUG        1    // 1:有効 0:無効
#endif

// ===== API =====

// 初期化（keyboard_post_init_user 内で呼ぶ）
// ウォームアップ待機 → 中心値計測 を実行
void analog_stick_init(void);

// スクロールモード用: 加速カーブなしの正規化傾き量を返す
// out_x, out_y: -1000〜+1000（デッドゾーン処理済み、加速なし）
// 加速状態もリセットするため、スクロールモード中は analog_stick_update() の代わりにこちらを呼ぶ
void analog_stick_get_scroll_values(int16_t *out_x, int16_t *out_y);

// マウスレポート更新（pointing_device_task_user 内で呼ぶ）
// スムージング、デッドゾーン、加速カーブ、サブピクセル処理を適用
report_mouse_t analog_stick_update(report_mouse_t mouse_report);
