// Copyright 2024 Hajime Oh-yake (@digitarhythm)
// SPDX-License-Identifier: MIT
//
// QMK Analog Stick Library
// アナログジョイスティックをマウスカーソル操作に使用するライブラリ
//
// 使い方:
//   1. config.h で必須パラメータ（JOYSTICK_X_PIN, JOYSTICK_Y_PIN）を定義
//   2. config.h でジョイスティックモデル（#define JH16 または #define JS16）を定義
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

// ===== ジョイスティックモデル選択（config.h でいずれか一方を定義） =====
// #define JH16   // X: 8〜1023 / Y: 8〜782
// #define JS16   // X: 0〜1023 / Y: 0〜1023

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
#else
#  error "config.h に #define JH16 または #define JS16 を追加してください"
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

// 速度設定（x1000スケール: 1000 = 1.0ピクセル/サイクル）
#ifndef JOYSTICK_MAX_SPEED
#define JOYSTICK_MAX_SPEED  6000   // 最大速度（8.0）
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
