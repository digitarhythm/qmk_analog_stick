[日本語](#日本語) | [English](#english)

---

<a name="日本語"></a>

# QMK Analog Stick Library

アナログジョイスティックを QMK/Vial ファームウェアでマウスカーソル操作に使用するためのライブラリです。K-SILVER JS16 (TMR) / JH16 (Hall Effect) 等のアナログ出力ジョイスティックに対応しています。

## Features

- **サブピクセル処理**: x1000 スケールの内部精度で、速度 1.0 未満でも滑らかなカーソル移動
- **ベクトル合成**: X/Y 軸を合成ベクトルとして処理し、斜め方向も均一な速度で移動
- **円形デッドゾーン**: 合成ベクトルの大きさで判定。全方向均一なデッドゾーン
- **二乗加速**: スティックの傾き量の二乗に比例した加速度。傾け始めはゆっくり、大きく倒すほど速く加速
- **比例減速**: スティックを戻すと、戻した量に応じて速度が比例的に減速。現在の傾き量に対応した速度上限へ向かって指数的に収束
- **移動平均フィルタ**: ADC ノイズの除去
- **起動時自動センタリング**: TMR センサーのウォームアップ待機と中心値の自動取得
- **非対称レンジ補正**: 中心値が ADC レンジの中央にない場合でも方向ごとに正規化
- **モデル選択**: `config.h` に `#define JH16` または `#define JS16` を記述するだけで各モデルの ADC レンジを適用
- **自動レンジ学習**: モデル未定義なら実測値から ADC レンジを自動学習。モデルを問わず同一ファームウェアで動作し、学習結果は EEPROM に自動保存
- **取り付け向き補正**: `JOYSTICK_ORIENTATION` で 90° 単位の回転補正に対応
- **スクロールモード対応**: `analog_stick_get_scroll_values()` で加速なしの傾き量を取得可能
- **ボタン対応**: SW ピンによるマウスクリック（GPIO 直結、オプション）
- **全パラメータカスタマイズ可能**: `config.h` の `#define` で上書き

## Requirements

- **MCU**: RP2040 (RP2040-Zero 等)
- **QMK Firmware** (Vial 対応版含む)
- **ChibiOS** (RP2040 の ADC ドライバを使用)

## Files

| File | Description |
|---|---|
| `qmk_analog_stick.h` | ヘッダファイル (デフォルトパラメータ定義 + API 宣言) |
| `qmk_analog_stick.c` | 実装ファイル |
| `halconf.h` | ChibiOS HAL 設定 (ADC 有効化) |
| `mcuconf.h` | ChibiOS MCU 設定 (RP2040 ADC ドライバ有効化) |

## Quick Start

### 1. ファイルの配置

`qmk_analog_stick.h`、`qmk_analog_stick.c`、`halconf.h`、`mcuconf.h` をキーボードディレクトリにコピーします。既に `halconf.h` や `mcuconf.h` が存在する場合は、手順 2・3 の内容を既存ファイルに追記してください。

```
keyboards/your_keyboard/
  ├── qmk_analog_stick.h
  ├── qmk_analog_stick.c
  ├── halconf.h
  ├── mcuconf.h
  └── keymaps/default/
      ├── config.h
      ├── keymap.c
      └── rules.mk
```

### 2. halconf.h

```c
#pragma once

#define HAL_USE_ADC TRUE

#include_next <halconf.h>
```

### 3. mcuconf.h

```c
#pragma once

#include_next <mcuconf.h>

#undef RP_ADC_USE_ADC1
#define RP_ADC_USE_ADC1 TRUE
```

### 4. config.h

ジョイスティックの接続ピンとモデルを定義します。

```c
#define JOYSTICK_X_PIN GP28
#define JOYSTICK_Y_PIN GP29

// ジョイスティックモデルを選択（省略すると自動レンジ学習モード）
#define JH16   // X: 8〜1023 / Y: 8〜782
// #define JS16   // X: 0〜1023 / Y: 0〜1023

// 自動レンジ学習モード + VIA/Vial 環境で学習レンジを保存する場合は必須
// #define VIA_EEPROM_CUSTOM_CONFIG_SIZE 10

// ボタン機能を使う場合（オプション）
// #define JOYSTICK_SW_PIN GP13
```

### 5. keymap.c

```c
#include QMK_KEYBOARD_H
#include "qmk_analog_stick.h"

// ... キーマップ定義 ...

void keyboard_post_init_user(void) {
    analog_stick_init();
}

report_mouse_t pointing_device_task_user(report_mouse_t mouse_report) {
    return analog_stick_update(mouse_report);
}
```

### 6. rules.mk

```makefile
POINTING_DEVICE_ENABLE = yes
POINTING_DEVICE_DRIVER = custom
SRC += analog.c qmk_analog_stick.c
```

---

## スクロールモード

`analog_stick_get_scroll_values()` を使うと、加速カーブなしのリニアな傾き量（-1000〜+1000）を取得できます。特定レイヤーでスクロール動作に切り替える場合の実装例:

```c
#define SCROLL_INTERVAL_MS  8      // スクロール蓄積の更新間隔（ms）
#define SCROLL_SPEED_DIV    6000   // 大きくすると遅く、小さくすると速い
#define SCROLL_MAX_SPEED    600    // 最高速度の上限（1〜1000）

static int32_t  scroll_accum_h = 0;
static int32_t  scroll_accum_v = 0;
static uint16_t scroll_timer   = 0;

report_mouse_t pointing_device_task_user(report_mouse_t mouse_report) {
    if (IS_LAYER_ON(_SCROLL_LAYER)) {
        int16_t stick_x, stick_y;
        analog_stick_get_scroll_values(&stick_x, &stick_y);

        if (stick_x >  SCROLL_MAX_SPEED) stick_x =  SCROLL_MAX_SPEED;
        if (stick_x < -SCROLL_MAX_SPEED) stick_x = -SCROLL_MAX_SPEED;
        if (stick_y >  SCROLL_MAX_SPEED) stick_y =  SCROLL_MAX_SPEED;
        if (stick_y < -SCROLL_MAX_SPEED) stick_y = -SCROLL_MAX_SPEED;

        if (timer_elapsed(scroll_timer) >= SCROLL_INTERVAL_MS) {
            scroll_timer = timer_read();
            scroll_accum_h += stick_x;
            scroll_accum_v += stick_y;
        }

        mouse_report.x = 0;
        mouse_report.y = 0;
        mouse_report.h = (int8_t)(scroll_accum_h / SCROLL_SPEED_DIV);
        mouse_report.v = (int8_t)(scroll_accum_v / SCROLL_SPEED_DIV);
        scroll_accum_h %= SCROLL_SPEED_DIV;
        scroll_accum_v %= SCROLL_SPEED_DIV;
    } else {
        mouse_report = analog_stick_update(mouse_report);
        scroll_accum_h = 0;
        scroll_accum_v = 0;
        scroll_timer   = timer_read();
    }
    return mouse_report;
}
```

- `SCROLL_SPEED_DIV` で全体速度を調整し、`SCROLL_MAX_SPEED` で最高速度を抑制します
- カーソルモードは `analog_stick_update()` をそのまま使用するため、スロットルの影響を受けません

---

## API Reference

### `void analog_stick_init(void)`

ジョイスティックを初期化します。`keyboard_post_init_user()` 内で呼び出してください。

**処理内容:**

1. `JOYSTICK_WARMUP_MS` ミリ秒待機 (TMR センサー安定化)
2. 64 回のサンプリングで X/Y 軸の中心値を自動取得（接続時センタリング）
3. 移動平均バッファを中心値で初期化
4. `JOYSTICK_SW_PIN` 定義時、ボタンピンを入力プルアップに設定
5. ADC レンジを設定（モデル定義があれば固定値、なければ 中心±`JOYSTICK_INITIAL_RANGE` から自動学習開始）
6. 自動レンジ学習モードで保存機能有効時、EEPROM の保存済みレンジを読み込んで統合
7. デバッグ有効時、中心値と ADC レンジをコンソール出力

**注意:** 初期化中はスティックに触れないでください。触れた状態の値が中心値として記録されます。

---

### `report_mouse_t analog_stick_update(report_mouse_t mouse_report)`

毎スキャンサイクルでマウスレポートを更新します。`pointing_device_task_user()` 内で呼び出してください。

**引数:**

| Name | Type | Description |
|---|---|---|
| `mouse_report` | `report_mouse_t` | 現在のマウスレポート |

**戻り値:**

更新されたマウスレポート (`mouse_report.x`、`mouse_report.y`、`JOYSTICK_SW_PIN` 定義時は `mouse_report.buttons` が設定される)

**処理内容:**

1. ADC 読み取り + 移動平均フィルタ
2. 各軸を -1000〜+1000 に正規化（非対称レンジ補正）
3. X/Y の合成ベクトル（magnitude）を計算
4. 円形デッドゾーン判定
5. 現在の傾き量から速度上限 (speed_limit) を計算（`JOYSTICK_CURVE_POWER` 乗カーブ + `JOYSTICK_CURVE_LOW_GAIN` による低速域ブレンド）
6. `current_speed < speed_limit` なら加速、`current_speed > speed_limit` なら比例減速
7. 取り付け向き補正を適用（`JOYSTICK_ORIENTATION`）
8. 合成速度を X/Y 方向比率で分配
9. サブピクセル蓄積 + 整数ピクセル変換
10. ボタン状態読み取り (`JOYSTICK_SW_PIN` 定義時)

---

### `void analog_stick_get_scroll_values(int16_t *out_x, int16_t *out_y)`

スクロールモード用に、加速カーブなしの正規化傾き量を返します。

**引数:**

| Name | Type | Description |
|---|---|---|
| `*out_x` | `int16_t *` | X 軸の傾き量（-1000〜+1000） |
| `*out_y` | `int16_t *` | Y 軸の傾き量（-1000〜+1000） |

**処理内容:**

- デッドゾーン処理済みの線形傾き量を返す
- 取り付け向き補正を適用
- `analog_stick_update()` の加速状態（`current_speed`、`accel_accum`、サブピクセル）をリセット

**注意:** スクロールモード中は `analog_stick_update()` の代わりにこちらを呼んでください。カーソルモードに戻った際に速度が跳ばないようにするため、毎サイクル加速状態をリセットします。

---

## Architecture

### 速度決定の流れ

```
スティック傾き
     ↓
各軸を正規化 (-1000〜+1000)
     ↓
合成ベクトル magnitude = √(x² + y²)
     ↓
円形デッドゾーン判定 (magnitude > deadzone?)
     ↓  YES
デッドゾーン差し引き → adjusted_magnitude (0〜1000)
     ↓
カーブ適用: curved = (adjusted_magnitude / 1000)^CURVE_POWER × 1000
低速域ブレンド: curved = (LOW_GAIN × curved + (1000 - LOW_GAIN) × curved²/1000) / 1000
速度上限: speed_limit = curved × MAX_SPEED / 1000
     ↓
current_speed < speed_limit?  → 加速: current_speed += adjusted_magnitude² × ACCEL_RATE / 1000000
current_speed > speed_limit?  → 減速: current_speed -= (current_speed - speed_limit) × DECEL_RATE / 100
     ↓
取り付け向き補正 (JOYSTICK_ORIENTATION)
     ↓
方向分配: speed_x = current_speed × norm_x / magnitude
          speed_y = current_speed × norm_y / magnitude
     ↓
サブピクセル蓄積 → mouse_report.x, mouse_report.y
```

### 加速の仕組み

スティックの傾き量の**二乗**が毎サイクルの加速度になります。倒し続けると速度が蓄積されていきます。

```
加速度/サイクル = (adjusted_magnitude / 1000)² × ACCEL_RATE
速度上限 = (adjusted_magnitude / 1000)² × MAX_SPEED
```

- **少し倒す**: 加速度が小さい → ゆっくり加速 → 精密操作
- **大きく倒す**: 加速度が大きい → 速く加速 → 素早い移動
- **どの傾きでも** 傾き量に対応した速度上限まで到達可能
- **スティックをデッドゾーンまで戻す**: 速度が即座にリセット

| 傾き | 加速度/cycle | 速度上限 (MAX_SPEED=6000) |
|---|---|---|
| 30% | 0.00144 | 540 (0.54 px/cycle) |
| 50% | 0.004 | 1500 (1.5 px/cycle) |
| 70% | 0.00784 | 2940 (2.94 px/cycle) |
| 100% | 0.016 | 6000 (6.0 px/cycle) |

### 速度カーブの調整

傾き量→速度上限のカーブは 2 つのパラメータで調整できます。

- **`JOYSTICK_CURVE_POWER`**（デフォルト `2`）: カーブの指数。`1` でリニア、`2` で二次関数、`3` で三次関数。大きいほど浅い傾きが緻密になり、深い傾きで急激に速くなる
- **`JOYSTICK_CURVE_LOW_GAIN`**（デフォルト `1000`）: 倒し始めの移動量の倍率（0〜1000）。`500` にすると倒し始めの速度が従来の半分になり、深く倒すほど緩やかに従来カーブへ近づいて、全倒しでは同じ最高速に到達する

`JOYSTICK_CURVE_LOW_GAIN 500` のときの従来比:

| 傾き | 従来比の速度 |
|---|---|
| 倒し始め | 約 50% |
| 50% | 約 56% |
| 70% | 約 62% |
| 100% | 100%（最高速は変わらず） |

### 減速の仕組み

スティックを戻すと `speed_limit` が下がり、`current_speed` がそれを超えた状態になります。その差分の `DECEL_RATE%` ずつ毎サイクル削減されます（指数的減衰）。

```
減速量/サイクル = (current_speed - speed_limit) × DECEL_RATE / 100
```

- **ちょっと戻す**: speed_limit が少し下がる → 緩やかな減速
- **大きく戻す**: speed_limit が大きく下がる → 急減速
- **デッドゾーンまで戻す**: 最後の方向でゆっくり停止

### 円形デッドゾーン

デッドゾーンは X/Y 軸の合成ベクトルの大きさで判定します。

```
変更前（四角形）     本ライブラリ（円形）
+---+---+              .---.
|   |   |             /     \
+---+---+            |   o   |
|   |   |             \     /
+---+---+              '---'
```

四角形デッドゾーンでは斜め方向で片方の軸だけ先に反応し、4方向に引っ張られる現象が発生します。円形デッドゾーンは全方向均一なデッドゾーンを提供します。

### ベクトル合成

X/Y 軸を独立に処理すると、斜め方向で速度が不均一になります。本ライブラリでは合成ベクトルの大きさに対して加速度を計算し、方向比率で X/Y に分配します。

```
magnitude = √(norm_x² + norm_y²)
speed_x = current_speed × norm_x / magnitude
speed_y = current_speed × norm_y / magnitude
```

---

## Parameter Reference

すべてのパラメータは `config.h` で `#define` することで上書きできます。

### Required Parameters

| Parameter | Description | Example |
|---|---|---|
| `JOYSTICK_X_PIN` | X 軸の ADC ピン | `GP28` |
| `JOYSTICK_Y_PIN` | Y 軸の ADC ピン | `GP29` |

### Model Selection

`config.h` でモデルを定義すると固定 ADC レンジで動作します。

| Define | X 軸レンジ | Y 軸レンジ | 対象モデル |
|---|---|---|---|
| `#define JH16` | 8〜1023 | 8〜782 | K-SILVER JH16 (Hall Effect) |
| `#define JS16` | 0〜1023 | 0〜1023 | K-SILVER JS16 (TMR) |
| （未定義） | 自動学習 | 自動学習 | 全モデル対応（自動レンジ学習モード） |

個別の軸のレンジを上書きしたい場合は、`JOYSTICK_ADC_X_MIN` 等を `config.h` で定義してください。

```c
// モデル定義後に個別上書きも可能
#define JH16
#define JOYSTICK_ADC_Y_MAX  800   // Y軸最大値だけ変更
```

#### 自動レンジ学習モード

モデルを何も定義しない場合、ADC レンジを実測で自動学習します。モデルを問わず同一ファームウェアで動作させたい場合に使用してください。

- 起動時の中心値から `中心±JOYSTICK_INITIAL_RANGE`（デフォルト `250`）の控えめなレンジで開始
- スティックを倒して実測値がレンジ外に出るたびに、その方向のレンジを自動拡張
- 数回全倒しすると実機のレンジに収束し、非対称なレンジ（JH16 の Y 軸など）も方向ごとに正しく学習される

**特性:**

- 学習が完了するまでは浅い傾きで最高速に達する（速度が出すぎる方向に誤差が出るだけで、最高速自体は変わらない）
- 学習結果は EEPROM に自動保存され、次回起動時の初期レンジとして読み込まれる（下記参照）

**学習レンジの不揮発保存:**

VIA/Vial 環境では、学習したレンジがデフォルトで EEPROM に自動保存されます。

- レンジ拡張が止まってから `JOYSTICK_RANGE_SAVE_DELAY_MS`（デフォルト 3 秒）後に 1 回だけ書き込む（フラッシュ摩耗対策）
- 起動時に保存値を読み込み、初期レンジと統合（広い方を採用）してから学習を再開する
- 保存先は VIA のカスタム設定領域（10 バイト）。`config.h` に以下の定義が**必須**:
  ```c
  #define VIA_EEPROM_CUSTOM_CONFIG_SIZE 10
  ```
  この定義によりダイナミックキーマップ領域は 10 バイト後ろへずれて確保されるため、キーマップ領域を侵食することはない（予約が不足している場合はコンパイルエラーで検出される）
- 広いレンジのスティックから狭いレンジのスティックへ交換した場合は、保存された広いレンジが残って最高速に到達できなくなるため、Vial の EEPROM リセット（または `QK_CLEAR_EEPROM`）で再学習させること

| Parameter | Default | Description |
|---|---|---|
| `JOYSTICK_INITIAL_RANGE` | `250` | 自動学習の初期半レンジ。実機の最小半レンジより小さい値にすること（大きいと最高速に到達できなくなる） |
| `JOYSTICK_RANGE_SAVE` | `1`（VIA 有効時） | 学習レンジの EEPROM 保存の有効/無効。VIA 無効環境ではデフォルト `0` |
| `JOYSTICK_RANGE_SAVE_DELAY_MS` | `3000` | レンジ拡張が止まってから保存するまでの待ち時間（ms） |
| `JOYSTICK_EEPROM_ADDR` | VIA カスタム設定領域 | 保存先アドレスの手動指定（VIA 無効環境で保存を使う場合に定義） |

### Button Parameters

`JOYSTICK_SW_PIN` を定義するとボタン機能が有効になります。未定義の場合、ボタン関連の処理は一切含まれません。

| Parameter | Default | Description |
|---|---|---|
| `JOYSTICK_SW_PIN` | (未定義) | SW ピンの GPIO。定義するとボタン機能有効 |
| `JOYSTICK_SW_BUTTON` | `MOUSE_BTN1` | ボタンに割り当てるマウスボタン |

**使用可能なマウスボタン定数:**

| Constant | Description |
|---|---|
| `MOUSE_BTN1` | 左クリック (デフォルト) |
| `MOUSE_BTN2` | 右クリック |
| `MOUSE_BTN3` | 中クリック (ホイールクリック) |
| `MOUSE_BTN4` | 戻る |
| `MOUSE_BTN5` | 進む |

**配線:**

ジョイスティックの SW ピンは押下時に内部 GND に接続される（アクティブ LOW）ため、GPIO に直接接続するだけで動作します。外部プルアップ抵抗は不要です（MCU 内部プルアップを使用）。

```
Joystick SW -----> GPIOピン（内部プルアップ有効）
```

> **注意:** ジョイスティックの SW ピンはキーマトリクスには直接接続できません。SW は押下時に内部 GND に短絡する 1 ピン出力のため、マトリクスの ROW-COL 間接続として機能しません。GPIO 直結で使用してください。

### Orientation Parameter

| Parameter | Default | Description |
|---|---|---|
| `JOYSTICK_ORIENTATION` | `0` | 取り付け向き補正（0: 標準、1: 時計回り90°、2: 180°、3: 反時計回り90°） |

### Speed / Acceleration Parameters

| Parameter | Default | Description |
|---|---|---|
| `JOYSTICK_MAX_SPEED` | `6000` | 最大速度 (x1000 スケール、6000 = 6.0 px/cycle) |
| `JOYSTICK_ACCEL_RATE` | `16` | 加速度係数。大きいほど速く加速する |
| `JOYSTICK_DECEL_RATE` | `8` | 減速率 (%)。スティックを戻したとき速度上限との差のこの割合ずつ減速する |
| `JOYSTICK_CURVE_POWER` | `2` | 速度カーブの指数 (`1`: リニア, `2`: 二次, `3`: 三次) |
| `JOYSTICK_CURVE_LOW_GAIN` | `1000` | 倒し始めの移動量の倍率 (0〜1000)。`500` で倒し始めが半分になる。最高速は変わらない |

**ACCEL_RATE の目安:**

| ACCEL_RATE | 全倒し時の速度上限到達時間 |
|---|---|
| `8` | 約10秒 |
| `16` | 約5秒 (デフォルト) |
| `32` | 約2.5秒 |
| `64` | 約1.25秒 |

### ADC Parameters

モデル選択（JH16/JS16）で自動設定されます。個別に上書きする場合のみ定義が必要です。

| Parameter | Description |
|---|---|
| `JOYSTICK_ADC_X_MIN` | X 軸の ADC 最小値 |
| `JOYSTICK_ADC_X_MAX` | X 軸の ADC 最大値 |
| `JOYSTICK_ADC_Y_MIN` | Y 軸の ADC 最小値 |
| `JOYSTICK_ADC_Y_MAX` | Y 軸の ADC 最大値 |

### Filter Parameters

| Parameter | Default | Description |
|---|---|---|
| `JOYSTICK_DEADZONE` | `30` | 中心付近のデッドゾーン (ADC 値) |
| `JOYSTICK_SMOOTHING` | `4` | 移動平均のサンプル数 |

### Startup Parameters

| Parameter | Default | Description |
|---|---|---|
| `JOYSTICK_WARMUP_MS` | `1000` | TMR センサー安定待ち時間 (ms) |

### Debug Parameters

| Parameter | Default | Description |
|---|---|---|
| `JOYSTICK_DEBUG` | `1` | デバッグ出力 (`1`: 有効, `0`: 無効) |

有効時、コンソールに約 1 秒ごとに以下の情報を出力します:

```
AnalogStick center: X=512 Y=498
AnalogStick range: X=8~1023 Y=8~782
Xr=450 cx=449 dx=0 | Yr=520 cy=519 dy=0 | spd=0
```

| Field | Description |
|---|---|
| `Xr` / `Yr` | スムージング後の ADC raw 値 |
| `cx` / `cy` | 起動時に計測した中心値 |
| `dx` / `dy` | 出力されたマウス移動量 (ピクセル) |
| `spd` | 現在の速度 (x1000 スケール) |

自動レンジ学習モードで学習レンジが EEPROM に保存されたときは、次の行が出力されます:

```
AnalogStick range saved: X=8~1023 Y=8~782
```

デバッグ出力を確認するには:
- **QMK Toolbox**: 接続するとコンソール出力が表示される
- **qmk console**: ターミナルで `qmk console` を実行

---

## Configuration Examples

### 精密操作重視 (CAD / デザイン作業向け)

```c
#define JOYSTICK_X_PIN       GP28
#define JOYSTICK_Y_PIN       GP29
#define JH16
#define JOYSTICK_DEADZONE      60
#define JOYSTICK_MAX_SPEED   3000
#define JOYSTICK_ACCEL_RATE     4
#define JOYSTICK_DECEL_RATE    30
```

### 高速操作重視 (ブラウジング / ゲーム向け)

```c
#define JOYSTICK_X_PIN       GP28
#define JOYSTICK_Y_PIN       GP29
#define JH16
#define JOYSTICK_DEADZONE      20
#define JOYSTICK_MAX_SPEED  12000
#define JOYSTICK_ACCEL_RATE    64
#define JOYSTICK_DECEL_RATE    80
```

### ボタン付き（左クリック）

```c
#define JOYSTICK_X_PIN       GP28
#define JOYSTICK_Y_PIN       GP29
#define JH16
#define JOYSTICK_SW_PIN      GP13
```

### JS16 モデルを使用する場合

```c
#define JOYSTICK_X_PIN GP28
#define JOYSTICK_Y_PIN GP29
#define JS16
```

### 自動レンジ学習モード（モデルを問わず同一ファームウェアで動作）

```c
#define JOYSTICK_X_PIN GP28
#define JOYSTICK_Y_PIN GP29
// JH16 / JS16 を定義しない → 自動レンジ学習モード
// 学習レンジの EEPROM 保存用（VIA/Vial 環境で必須）
#define VIA_EEPROM_CUSTOM_CONFIG_SIZE 10
```

---

## Hardware Notes

### 対応ジョイスティック

本ライブラリは ADC でアナログ電圧を読み取る方式のジョイスティックに広く対応しています。動作確認済みのモデル:

- **K-SILVER JS16** (TMR 方式)
- **K-SILVER JH16** (Hall Effect 方式)

上記以外のアナログ出力ジョイスティックも、自動レンジ学習モードを使えばレンジ設定なしで動作します。ただしデジタル出力（SPI/I2C 等）のモデルには対応していません（例: 静電容量式の K-SILVER JL16 は出力方式が未確認のため、アナログ電圧出力であることを確認してから使用してください）。

### K-SILVER JS16 / JH16

- **センシング方式**: TMR (トンネル磁気抵抗効果) / Hall Effect
- **動作電圧**: 1.8V 〜 3.3V
- **消費電力**: 約 210 〜 215 uA
- **耐久性**: 約 500 万回転
- **ピン**: VCC, GND, X 出力, Y 出力, SW (ボタン)

### RP2040 ADC ピン

RP2040 で ADC として使用可能なピンは以下の 4 つです:

| Pin | ADC Channel |
|---|---|
| GP26 | ADC0 |
| GP27 | ADC1 |
| GP28 | ADC2 |
| GP29 | ADC3 |

**注意**: 一部のボードでは GP29 が VSYS 電圧計測用に使用されている場合があります。その場合は GP26/GP27/GP28 を使用してください。

### 配線例

```
Joystick      RP2040-Zero
----------    -----------
VCC    ----->  3.3V
GND    ----->  GND
X out  ----->  GP28  (ADC ピン)
Y out  ----->  GP29  (ADC ピン)
SW     ----->  GP13  (任意の GPIO、オプション)
```

### EC12 エンコーダーフットプリント流用時の配線

K-SILVER JS16/JH16 は EC12/EC11 ロータリーエンコーダーのフットプリントに実装できますが、ピンの役割が異なるため配線に注意が必要です。

```
EC12 ピン      Joystick ピン  接続先
---------      ------------   ------
Encoder A  --> X output   --> ADC ピン (GP28 等)
Encoder GND -> GND        --> GND
Encoder B  --> Y output   --> ADC ピン (GP29 等)
Switch 1   --> VCC        --> 3.3V 電源ライン (※)
Switch 2   --> SW         --> GPIO ピン (※)
```

> **(※) 注意:** EC12 のスイッチピンは通常キーマトリクスの ROW/COL に接続されています。ジョイスティックの VCC は常時 3.3V 給電が必要なため、Switch 1 のパッドが 3.3V 電源ラインに接続されていることを確認してください。Switch 2 (SW) はマトリクスではなく GPIO に直結する必要があります。

---

## Troubleshooting

### カーソルが勝手に動く

- `JOYSTICK_DEADZONE` を大きくしてください (例: `80`)
- `JOYSTICK_WARMUP_MS` を大きくしてセンタリング精度を上げてください (例: `3000`)
- 起動時にスティックに触れないでください

### カーソルが動かない

- `halconf.h` で `HAL_USE_ADC TRUE` が定義されているか確認
- `mcuconf.h` で `RP_ADC_USE_ADC1 TRUE` が定義されているか確認
- `rules.mk` に `SRC += analog.c` が含まれているか確認
- ジョイスティックの VCC が 3.3V に接続されているか確認
- `JOYSTICK_DEBUG 1` でコンソール出力を確認し、ADC 値が変化するか確認

### 方向によって速度が違う

- 使用中のモデルに対応する `#define JH16` または `#define JS16` が `config.h` に定義されているか確認
- モデル定義を削除して自動レンジ学習モードにすると、実機のレンジを方向ごとに自動学習します（接続後にスティックを数回全倒ししてください）
- それでも解消しない場合は、`JOYSTICK_ADC_X/Y_MIN/MAX` を実際の ADC 出力レンジに合わせて個別定義してください
- `JOYSTICK_DEBUG 1` を有効にして起動時のログ（`AnalogStick range:`）と実際の ADC 値を比較してください

### 自動レンジ学習モードで接続直後だけ速度が出すぎる

- 学習が収束するまでの仕様です。接続後にスティックをゆっくり一周（全倒し）させると実機のレンジに収束します
- 学習結果は EEPROM に保存されるため、発生するのは初回起動時（または EEPROM リセット後）のみです
- 気になる場合は `#define JH16` / `#define JS16` で固定レンジにしてください

### 4 方向にしか動かない / 4 方向に引っ張られる

- 本ライブラリはベクトル合成と円形デッドゾーンで対策済みです
- それでも発生する場合は `JOYSTICK_ACCEL_RATE` を小さくして最大速度を抑えてください
- `JOYSTICK_SMOOTHING` を大きくすると入力が滑らかになります

### ボタンが反応しない

- `config.h` で `JOYSTICK_SW_PIN` が定義されているか確認
- SW ピンが GPIO に直接接続されているか確認（キーマトリクス経由では動作しません）
- テスターで SW ピンと GND 間を計測し、押下時に導通するか確認

### ボタンが押しっぱなしになる

- SW ピンが GND にショートしていないか確認
- 配線が正しいか確認（SW ピンはアクティブ LOW: 押すと GND に接続）

## License

MIT

---

<a name="english"></a>

# QMK Analog Stick Library

A library for using analog joysticks as mouse cursor controllers in QMK/Vial firmware. Compatible with analog output joysticks such as the K-SILVER JS16 (TMR) and JH16 (Hall Effect).

## Features

- **Sub-pixel processing**: Internal x1000 scale precision for smooth cursor movement even at speeds below 1.0 px/cycle
- **Vector composition**: X/Y axes are processed as a combined vector, providing uniform speed in all directions including diagonals
- **Circular deadzone**: Deadzone evaluated against the magnitude of the combined vector — uniform in all directions
- **Quadratic acceleration**: Acceleration proportional to the square of the tilt amount — slow at start, faster as you push further
- **Proportional deceleration**: Returning the stick reduces speed proportionally; current speed converges exponentially toward the speed limit for the current tilt
- **Moving average filter**: ADC noise reduction
- **Startup auto-centering**: TMR sensor warmup delay and automatic center value acquisition at boot
- **Asymmetric range correction**: Per-direction normalization even when the center output is not at the midpoint of the ADC range
- **Model selection**: Define `#define JH16` or `#define JS16` in `config.h` to apply the correct ADC range for each model
- **Adaptive range learning**: With no model defined, the ADC range is learned automatically from actual readings — one firmware works with any model, and the learned range is persisted to EEPROM
- **Orientation correction**: `JOYSTICK_ORIENTATION` supports 90° rotation correction for non-standard mounting
- **Scroll mode support**: `analog_stick_get_scroll_values()` returns linear tilt values without acceleration
- **Button support**: Mouse click via SW pin (direct GPIO connection, optional)
- **Fully configurable**: All parameters can be overridden via `#define` in `config.h`

## Requirements

- **MCU**: RP2040 (RP2040-Zero, etc.)
- **QMK Firmware** (including Vial-compatible forks)
- **ChibiOS** (uses the RP2040 ADC driver)

## Files

| File | Description |
|---|---|
| `qmk_analog_stick.h` | Header file (default parameter definitions + API declarations) |
| `qmk_analog_stick.c` | Implementation file |
| `halconf.h` | ChibiOS HAL configuration (enables ADC) |
| `mcuconf.h` | ChibiOS MCU configuration (enables RP2040 ADC driver) |

## Quick Start

### 1. Place the Files

Copy `qmk_analog_stick.h`, `qmk_analog_stick.c`, `halconf.h`, and `mcuconf.h` into your keyboard directory. If `halconf.h` or `mcuconf.h` already exist, merge the content from steps 2 and 3 into the existing files.

```
keyboards/your_keyboard/
  ├── qmk_analog_stick.h
  ├── qmk_analog_stick.c
  ├── halconf.h
  ├── mcuconf.h
  └── keymaps/default/
      ├── config.h
      ├── keymap.c
      └── rules.mk
```

### 2. halconf.h

```c
#pragma once

#define HAL_USE_ADC TRUE

#include_next <halconf.h>
```

### 3. mcuconf.h

```c
#pragma once

#include_next <mcuconf.h>

#undef RP_ADC_USE_ADC1
#define RP_ADC_USE_ADC1 TRUE
```

### 4. config.h

Define the joystick pins and select the joystick model.

```c
#define JOYSTICK_X_PIN GP28
#define JOYSTICK_Y_PIN GP29

// Select the joystick model (omit both for adaptive range mode)
#define JH16   // X: 8~1023 / Y: 8~782
// #define JS16   // X: 0~1023 / Y: 0~1023

// Required to persist the learned range in adaptive range mode (VIA/Vial)
// #define VIA_EEPROM_CUSTOM_CONFIG_SIZE 10

// Optional: enable button support
// #define JOYSTICK_SW_PIN GP13
```

### 5. keymap.c

```c
#include QMK_KEYBOARD_H
#include "qmk_analog_stick.h"

// ... keymap definitions ...

void keyboard_post_init_user(void) {
    analog_stick_init();
}

report_mouse_t pointing_device_task_user(report_mouse_t mouse_report) {
    return analog_stick_update(mouse_report);
}
```

### 6. rules.mk

```makefile
POINTING_DEVICE_ENABLE = yes
POINTING_DEVICE_DRIVER = custom
SRC += analog.c qmk_analog_stick.c
```

---

## Scroll Mode

Using `analog_stick_get_scroll_values()`, you can obtain linear (non-accelerated) tilt values (-1000 to +1000) for scroll behavior on a specific layer:

```c
#define SCROLL_INTERVAL_MS  8      // Accumulation interval (ms)
#define SCROLL_SPEED_DIV    6000   // Larger = slower, smaller = faster
#define SCROLL_MAX_SPEED    600    // Maximum scroll speed cap (1~1000)

static int32_t  scroll_accum_h = 0;
static int32_t  scroll_accum_v = 0;
static uint16_t scroll_timer   = 0;

report_mouse_t pointing_device_task_user(report_mouse_t mouse_report) {
    if (IS_LAYER_ON(_SCROLL_LAYER)) {
        int16_t stick_x, stick_y;
        analog_stick_get_scroll_values(&stick_x, &stick_y);

        if (stick_x >  SCROLL_MAX_SPEED) stick_x =  SCROLL_MAX_SPEED;
        if (stick_x < -SCROLL_MAX_SPEED) stick_x = -SCROLL_MAX_SPEED;
        if (stick_y >  SCROLL_MAX_SPEED) stick_y =  SCROLL_MAX_SPEED;
        if (stick_y < -SCROLL_MAX_SPEED) stick_y = -SCROLL_MAX_SPEED;

        if (timer_elapsed(scroll_timer) >= SCROLL_INTERVAL_MS) {
            scroll_timer = timer_read();
            scroll_accum_h += stick_x;
            scroll_accum_v += stick_y;
        }

        mouse_report.x = 0;
        mouse_report.y = 0;
        mouse_report.h = (int8_t)(scroll_accum_h / SCROLL_SPEED_DIV);
        mouse_report.v = (int8_t)(scroll_accum_v / SCROLL_SPEED_DIV);
        scroll_accum_h %= SCROLL_SPEED_DIV;
        scroll_accum_v %= SCROLL_SPEED_DIV;
    } else {
        mouse_report = analog_stick_update(mouse_report);
        scroll_accum_h = 0;
        scroll_accum_v = 0;
        scroll_timer   = timer_read();
    }
    return mouse_report;
}
```

- Use `SCROLL_SPEED_DIV` to set the overall scroll speed, and `SCROLL_MAX_SPEED` to cap the maximum
- Cursor mode uses `analog_stick_update()` directly with no rate limiting

---

## API Reference

### `void analog_stick_init(void)`

Initializes the joystick. Call this inside `keyboard_post_init_user()`.

**What it does:**

1. Waits `JOYSTICK_WARMUP_MS` milliseconds for the TMR sensor to stabilize
2. Samples the X/Y axes 64 times to auto-detect center values (startup centering)
3. Initializes the moving average buffer with the center values
4. If `JOYSTICK_SW_PIN` is defined, configures the button pin as input with pull-up
5. Sets the ADC range (fixed values if a model is defined; otherwise adaptive learning starts from center ± `JOYSTICK_INITIAL_RANGE`)
6. In adaptive range mode with persistence enabled, loads and merges the saved range from EEPROM
7. If debug is enabled, prints center values and ADC range to the console

**Note:** Do not touch the stick during initialization. Any deflection will be recorded as the center value.

---

### `report_mouse_t analog_stick_update(report_mouse_t mouse_report)`

Updates the mouse report on every scan cycle. Call this inside `pointing_device_task_user()`.

**Arguments:**

| Name | Type | Description |
|---|---|---|
| `mouse_report` | `report_mouse_t` | Current mouse report |

**Returns:**

Updated mouse report with `mouse_report.x`, `mouse_report.y`, and (if `JOYSTICK_SW_PIN` is defined) `mouse_report.buttons` set.

**What it does:**

1. ADC read + moving average filter
2. Normalize each axis to -1000~+1000 (with asymmetric range correction)
3. Calculate the combined vector magnitude (X/Y)
4. Circular deadzone check
5. Calculate speed limit from current tilt (`JOYSTICK_CURVE_POWER` curve + low-speed blend via `JOYSTICK_CURVE_LOW_GAIN`)
6. If `current_speed < speed_limit`: accelerate; if `current_speed > speed_limit`: proportionally decelerate
7. Apply orientation correction (`JOYSTICK_ORIENTATION`)
8. Distribute combined speed to X/Y axes by direction ratio
9. Sub-pixel accumulation + integer pixel conversion
10. Read button state (if `JOYSTICK_SW_PIN` is defined)

---

### `void analog_stick_get_scroll_values(int16_t *out_x, int16_t *out_y)`

Returns linear (non-accelerated) normalized tilt values for scroll mode.

**Arguments:**

| Name | Type | Description |
|---|---|---|
| `*out_x` | `int16_t *` | X axis tilt (-1000~+1000) |
| `*out_y` | `int16_t *` | Y axis tilt (-1000~+1000) |

**What it does:**

- Returns deadzone-processed linear tilt values
- Applies orientation correction
- Resets `analog_stick_update()` acceleration state (`current_speed`, `accel_accum`, sub-pixels)

**Note:** Call this instead of `analog_stick_update()` while in scroll mode. The acceleration state is reset every cycle to prevent the cursor from jumping when switching back to cursor mode.

---

## Architecture

### Speed Determination Flow

```
Stick tilt
     ↓
Normalize each axis (-1000~+1000)
     ↓
Combined vector: magnitude = √(x² + y²)
     ↓
Circular deadzone check (magnitude > deadzone?)
     ↓  YES
Subtract deadzone → adjusted_magnitude (0~1000)
     ↓
Apply curve: curved = (adjusted_magnitude / 1000)^CURVE_POWER × 1000
Low-speed blend: curved = (LOW_GAIN × curved + (1000 - LOW_GAIN) × curved²/1000) / 1000
Speed limit: speed_limit = curved × MAX_SPEED / 1000
     ↓
current_speed < speed_limit?  → Accelerate: current_speed += adjusted_magnitude² × ACCEL_RATE / 1000000
current_speed > speed_limit?  → Decelerate: current_speed -= (current_speed - speed_limit) × DECEL_RATE / 100
     ↓
Apply orientation correction (JOYSTICK_ORIENTATION)
     ↓
Direction distribution: speed_x = current_speed × norm_x / magnitude
                        speed_y = current_speed × norm_y / magnitude
     ↓
Sub-pixel accumulation → mouse_report.x, mouse_report.y
```

### Circular Deadzone

The deadzone is evaluated against the magnitude of the combined X/Y vector.

```
Square deadzone (before)    This library (circular)
+---+---+                       .---.
|   |   |                      /     \
+---+---+                     |   o   |
|   |   |                      \     /
+---+---+                       '---'
```

A square deadzone causes one axis to react before the other in diagonal directions, producing a four-directional pull effect. The circular deadzone provides a uniform deadzone in all directions.

### Vector Composition

Processing X and Y axes independently leads to non-uniform speed in diagonal directions. This library calculates acceleration against the combined vector magnitude, then distributes it to X/Y by direction ratio.

```
magnitude = √(norm_x² + norm_y²)
speed_x = current_speed × norm_x / magnitude
speed_y = current_speed × norm_y / magnitude
```

### Speed Curve Tuning

The tilt-to-speed-limit curve can be adjusted with two parameters:

- **`JOYSTICK_CURVE_POWER`** (default `2`): Curve exponent. `1` = linear, `2` = quadratic, `3` = cubic. Higher values give finer control at small tilts and a sharper ramp at large tilts.
- **`JOYSTICK_CURVE_LOW_GAIN`** (default `1000`): Initial-tilt speed multiplier (0~1000). Setting `500` halves the speed at small tilts; the curve then gradually converges back so full tilt still reaches the same top speed.

Speed relative to default with `JOYSTICK_CURVE_LOW_GAIN 500`:

| Tilt | Relative speed |
|---|---|
| Small tilt | ~50% |
| 50% | ~56% |
| 70% | ~62% |
| 100% | 100% (top speed unchanged) |

---

## Parameter Reference

All parameters can be overridden in `config.h` using `#define`.

### Required Parameters

| Parameter | Description | Example |
|---|---|---|
| `JOYSTICK_X_PIN` | ADC pin for the X axis | `GP28` |
| `JOYSTICK_Y_PIN` | ADC pin for the Y axis | `GP29` |

### Model Selection

Defining a model in `config.h` selects fixed ADC ranges:

| Define | X range | Y range | Target model |
|---|---|---|---|
| `#define JH16` | 8~1023 | 8~782 | K-SILVER JH16 (Hall Effect) |
| `#define JS16` | 0~1023 | 0~1023 | K-SILVER JS16 (TMR) |
| (none) | auto-learned | auto-learned | Any model (adaptive range mode) |

Individual axis ranges can be overridden after the model define:

```c
#define JH16
#define JOYSTICK_ADC_Y_MAX  800   // override Y max only
```

#### Adaptive Range Mode

When no model is defined, the ADC range is learned automatically from actual readings. Use this when a single firmware should work with any joystick model.

- Starts with a conservative range of `center ± JOYSTICK_INITIAL_RANGE` (default `250`) measured at boot
- Whenever a reading falls outside the current range, the range expands in that direction
- After a few full tilts, the range converges to the actual hardware range — asymmetric ranges (such as the JH16 Y axis) are learned correctly per direction

**Characteristics:**

- Until learning converges, top speed is reached at a shallower tilt (the error is only toward being faster; top speed itself is unchanged)
- The learned range is automatically persisted to EEPROM and loaded as the initial range on the next boot (see below)

**Persisting the learned range:**

In VIA/Vial environments, the learned range is saved to EEPROM by default.

- A single write occurs `JOYSTICK_RANGE_SAVE_DELAY_MS` (default 3 s) after range expansion stops (flash wear protection)
- At boot, the saved values are loaded and merged with the initial range (the wider one wins), then learning continues
- Storage uses the VIA custom config area (10 bytes). The following define is **required** in `config.h`:
  ```c
  #define VIA_EEPROM_CUSTOM_CONFIG_SIZE 10
  ```
  This reservation shifts the dynamic keymap area 10 bytes forward, so the keymap area is never encroached (an insufficient reservation is caught as a compile error)
- When swapping from a wider-range stick to a narrower-range one, the saved wide range would make top speed unreachable — reset the EEPROM (Vial's EEPROM reset or `QK_CLEAR_EEPROM`) to re-learn

| Parameter | Default | Description |
|---|---|---|
| `JOYSTICK_INITIAL_RANGE` | `250` | Initial half-range for adaptive mode. Must be smaller than the hardware's smallest half-range (a larger value would make top speed unreachable) |
| `JOYSTICK_RANGE_SAVE` | `1` (when VIA is enabled) | Enable/disable EEPROM persistence of the learned range. Defaults to `0` without VIA |
| `JOYSTICK_RANGE_SAVE_DELAY_MS` | `3000` | Delay (ms) after the last range expansion before saving |
| `JOYSTICK_EEPROM_ADDR` | VIA custom config area | Manual storage address (define this to use persistence without VIA) |

### Button Parameters

| Parameter | Default | Description |
|---|---|---|
| `JOYSTICK_SW_PIN` | (undefined) | GPIO pin for SW. Defining this enables button support |
| `JOYSTICK_SW_BUTTON` | `MOUSE_BTN1` | Mouse button assigned to the joystick switch |

### Orientation Parameter

| Parameter | Default | Description |
|---|---|---|
| `JOYSTICK_ORIENTATION` | `0` | Mounting orientation correction (0: standard, 1: CW 90°, 2: 180°, 3: CCW 90°) |

### Speed / Acceleration Parameters

| Parameter | Default | Description |
|---|---|---|
| `JOYSTICK_MAX_SPEED` | `6000` | Maximum speed (x1000 scale; 6000 = 6.0 px/cycle) |
| `JOYSTICK_ACCEL_RATE` | `16` | Acceleration coefficient — higher values accelerate faster |
| `JOYSTICK_CURVE_POWER` | `2` | Speed curve exponent (`1`: linear, `2`: quadratic, `3`: cubic) |
| `JOYSTICK_CURVE_LOW_GAIN` | `1000` | Initial-tilt speed multiplier (0~1000). `500` halves the speed at small tilts while keeping the same top speed at full tilt |
| `JOYSTICK_DECEL_RATE` | `8` | Deceleration rate (%). Percentage of speed excess shed per cycle when returning the stick |

### ADC Parameters

Set automatically by model selection. Override individually if needed.

| Parameter | Description |
|---|---|
| `JOYSTICK_ADC_X_MIN` | Minimum ADC value for X axis |
| `JOYSTICK_ADC_X_MAX` | Maximum ADC value for X axis |
| `JOYSTICK_ADC_Y_MIN` | Minimum ADC value for Y axis |
| `JOYSTICK_ADC_Y_MAX` | Maximum ADC value for Y axis |

### Filter Parameters

| Parameter | Default | Description |
|---|---|---|
| `JOYSTICK_DEADZONE` | `30` | Center deadzone radius (ADC units) |
| `JOYSTICK_SMOOTHING` | `4` | Moving average sample count |

### Startup Parameters

| Parameter | Default | Description |
|---|---|---|
| `JOYSTICK_WARMUP_MS` | `1000` | TMR sensor stabilization wait time (ms) |

### Debug Parameters

| Parameter | Default | Description |
|---|---|---|
| `JOYSTICK_DEBUG` | `1` | Debug output (`1`: enabled, `0`: disabled) |

When enabled, the following is printed at startup and approximately once per second during use:

```
AnalogStick center: X=512 Y=498
AnalogStick range: X=8~1023 Y=8~782
Xr=450 cx=449 dx=0 | Yr=520 cy=519 dy=0 | spd=0
```

When the learned range is saved to EEPROM in adaptive range mode, the following line is printed:

```
AnalogStick range saved: X=8~1023 Y=8~782
```

To view debug output:
- **QMK Toolbox**: Connect the keyboard and console output appears automatically
- **qmk console**: Run `qmk console` in a terminal

---

## Configuration Examples

### Precision-focused (CAD / design work)

```c
#define JOYSTICK_X_PIN       GP28
#define JOYSTICK_Y_PIN       GP29
#define JH16
#define JOYSTICK_DEADZONE      60
#define JOYSTICK_MAX_SPEED   3000
#define JOYSTICK_ACCEL_RATE     4
#define JOYSTICK_DECEL_RATE    30
```

### Speed-focused (browsing / gaming)

```c
#define JOYSTICK_X_PIN       GP28
#define JOYSTICK_Y_PIN       GP29
#define JH16
#define JOYSTICK_DEADZONE      20
#define JOYSTICK_MAX_SPEED  12000
#define JOYSTICK_ACCEL_RATE    64
#define JOYSTICK_DECEL_RATE    80
```

### With button (left click)

```c
#define JOYSTICK_X_PIN       GP28
#define JOYSTICK_Y_PIN       GP29
#define JH16
#define JOYSTICK_SW_PIN      GP13
```

### Using JS16 model

```c
#define JOYSTICK_X_PIN GP28
#define JOYSTICK_Y_PIN GP29
#define JS16
```

### Adaptive range mode (one firmware for any model)

```c
#define JOYSTICK_X_PIN GP28
#define JOYSTICK_Y_PIN GP29
// No JH16 / JS16 define → adaptive range mode
// Required to persist the learned range (VIA/Vial environments)
#define VIA_EEPROM_CUSTOM_CONFIG_SIZE 10
```

---

## Hardware Notes

### Supported Joysticks

This library is broadly compatible with any joystick that outputs analog voltage read via ADC. Confirmed working models:

- **K-SILVER JS16** (TMR sensing)
- **K-SILVER JH16** (Hall Effect sensing)

Other analog-output joysticks also work without any range configuration when using adaptive range mode. Digital-output models (SPI/I2C etc.) are not supported (e.g., the capacitive K-SILVER JL16 has an unconfirmed output type — verify it outputs analog voltage before use).

### K-SILVER JS16 / JH16

- **Sensing method**: TMR (Tunnel Magnetoresistance) / Hall Effect
- **Operating voltage**: 1.8V – 3.3V
- **Current consumption**: ~210–215 µA
- **Durability**: ~5 million rotations
- **Pins**: VCC, GND, X output, Y output, SW (button)

### RP2040 ADC Pins

| Pin | ADC Channel |
|---|---|
| GP26 | ADC0 |
| GP27 | ADC1 |
| GP28 | ADC2 |
| GP29 | ADC3 |

**Note**: On some boards, GP29 may be reserved for VSYS voltage monitoring. In that case, use GP26, GP27, or GP28.

### Wiring Example

```
Joystick      RP2040-Zero
----------    -----------
VCC    ----->  3.3V
GND    ----->  GND
X out  ----->  GP28  (ADC pin)
Y out  ----->  GP29  (ADC pin)
SW     ----->  GP13  (any GPIO, optional)
```

### Wiring When Reusing an EC12 Encoder Footprint

```
EC12 pin       Joystick pin   Connect to
---------      ------------   ----------
Encoder A  --> X output   --> ADC pin (GP28, etc.)
Encoder GND -> GND        --> GND
Encoder B  --> Y output   --> ADC pin (GP29, etc.)
Switch 1   --> VCC        --> 3.3V power rail (※)
Switch 2   --> SW         --> GPIO pin (※)
```

> **(※) Note:** EC12 switch pins are normally connected to the key matrix ROW/COL lines. The joystick VCC requires a continuous 3.3V supply, so verify that the Switch 1 pad is connected to the 3.3V power rail. Switch 2 (SW) must be wired directly to a GPIO, not to the matrix.

---

## Troubleshooting

### Cursor moves on its own

- Increase `JOYSTICK_DEADZONE` (e.g., `80`)
- Increase `JOYSTICK_WARMUP_MS` to improve centering accuracy (e.g., `3000`)
- Do not touch the stick during startup

### Cursor does not move

- Check that `HAL_USE_ADC TRUE` is defined in `halconf.h`
- Check that `RP_ADC_USE_ADC1 TRUE` is defined in `mcuconf.h`
- Check that `SRC += analog.c` is included in `rules.mk`
- Check that joystick VCC is connected to 3.3V
- Enable `JOYSTICK_DEBUG 1` and check the console to confirm ADC values are changing

### Speed differs by direction

- Confirm that the correct model (`#define JH16` or `#define JS16`) is defined in `config.h`
- Alternatively, remove the model define to use adaptive range mode, which learns the actual per-direction range automatically (tilt the stick fully a few times after connecting)
- If it persists, override `JOYSTICK_ADC_X/Y_MIN/MAX` with the actual measured ADC range
- Enable `JOYSTICK_DEBUG 1` and compare the startup log (`AnalogStick range:`) against the actual ADC values at full tilt

### Speed is too high right after connecting in adaptive range mode

- This is expected until learning converges. Slowly swirl the stick once at full tilt after connecting
- Since the learned range is persisted to EEPROM, this only happens on the first boot (or after an EEPROM reset)
- If this bothers you, use fixed ranges with `#define JH16` / `#define JS16`

### Cursor only moves in 4 directions / pulled toward 4 directions

- This library addresses this with vector composition and a circular deadzone
- If it still occurs, reduce `JOYSTICK_ACCEL_RATE` to limit maximum speed
- Increasing `JOYSTICK_SMOOTHING` smooths out the input

### Button does not respond

- Check that `JOYSTICK_SW_PIN` is defined in `config.h`
- Check that the SW pin is connected directly to a GPIO (not through the key matrix)
- Use a multimeter to confirm continuity between SW and GND when pressed

### Button is stuck pressed

- Check that the SW pin is not shorted to GND
- Check the wiring (SW is active LOW: pressing connects it to GND)

## License

MIT
