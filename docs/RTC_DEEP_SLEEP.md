# RTC and Deep Sleep System

ESP32のディープスリープと時刻管理の詳細ドキュメント。

## 概要

EPDEnvClockは毎分スリープ→起動を繰り返す。1時間に約60回のサイクル。

```
┌─────────────────────────────────────────────────────────────┐
│  12:30:00        12:30:54    12:31:00                       │
│     │               │           │                           │
│     │◄─ deep sleep ─►│◄─ active ─►│                         │
│     │   (~54 sec)    │  (~6 sec)  │                         │
│  スリープ開始      起動       表示更新                        │
└─────────────────────────────────────────────────────────────┘
```

## ディープスリープ中に失われるもの

| 項目 | 保持される？ | 復元方法 |
|------|------------|---------|
| CPUレジスタ | ❌ | 再起動 |
| RAM | ❌ | - |
| RTC Memory (8KB) | ✅ | `RTC_DATA_ATTR` |
| システムクロック | ❌ | 手動で再設定 |
| WiFi接続 | ❌ | 再接続 |
| I2C/SPI設定 | ❌ | 再初期化 |

## 時刻復元の仕組み

### スリープ前に保存するもの

```cpp
struct RTCState {
  time_t savedTime;        // スリープ時の秒（Unix timestamp）
  int32_t savedTimeUs;     // スリープ時のマイクロ秒部分（0-999999）
  uint64_t sleepDurationUs; // スリープ予定時間（マイクロ秒）
  // ...
};
```

### 起動時の時刻復元

```cpp
// マイクロ秒精度で計算（切り捨てを防ぐ）
int64_t savedTimeUs = (int64_t)savedTime * 1000000LL + savedTimeUs;
int64_t wakeupTimeUs = savedTimeUs + sleepDurationUs + bootOverheadUs;

// ドリフト補正を適用
float sleepMinutes = sleepDurationUs / 60000000.0f;
int64_t driftCompensationUs = sleepMinutes * driftRateMsPerMin * 1000.0f;
wakeupTimeUs += driftCompensationUs;

// システムクロックに設定
struct timeval tv;
tv.tv_sec = wakeupTimeUs / 1000000LL;
tv.tv_usec = wakeupTimeUs % 1000000LL;
settimeofday(&tv, NULL);
```

### なぜマイクロ秒精度が必要か

整数除算による切り捨ての問題：

```cpp
// ❌ 旧コード（バグ）
time_t wakeupTime = savedTime + (sleepDurationUs / 1000000) + (bootOverheadUs / 1000000);
// 52,500,000 / 1,000,000 = 52 (0.5秒ロスト！)
```

毎サイクルで最大1秒の損失 → 60サイクル（1時間）で約60秒のドリフト。

## RTC ドリフト補正システム (Dec 2025)

### 問題

ESP32 の内蔵 150kHz RC オシレーターは約 170ms/分（10.2秒/時間）ドリフトする。
補正なしでは、1時間後に時計が約10秒遅れる。

### 解決策

ドリフトレートを測定し、時刻復元時に補正を適用する。

```
┌─────────────────────────────────────────────────────────────┐
│                      ドリフト補正フロー                       │
├─────────────────────────────────────────────────────────────┤
│  起動時:                                                     │
│    wakeup_time = saved_time + sleep_duration + boot_overhead │
│                + (sleep_minutes × driftRate)  ← 補正追加     │
│                                                              │
│  NTP同期時:                                                  │
│    trueDrift = residual + cumulativeCompensation             │
│    trueRate = trueDrift / minutesSinceLastSync               │
│    driftRate = 0.7 × old + 0.3 × trueRate  (EMA)             │
└─────────────────────────────────────────────────────────────┘
```

### RTCState に追加されたフィールド

```cpp
struct RTCState {
  // ...
  float driftRateMsPerMin = 170.0f;     // ドリフトレート（ms/分）
  bool driftRateCalibrated = false;     // NTP較正済みか
  int64_t cumulativeCompensationMs = 0; // 累積補正量（レート計算用）
};
```

### 累積補正量が必要な理由

NTP同期時に測定される「残差」は補正後の誤差：

```
例: rate = 170 ms/min で30分間動作

真のドリフト: 30分 × 170 ms/min = 5,100 ms 遅れ
補正適用:     30分 × 170 ms/min = 5,100 ms 追加
残差:         5,100 - 5,100 = 0 ms (理想的)

もし残差だけでレート更新すると:
  newRate = 0 ms / 30 min = 0 ms/min ← 間違い！
  次回補正なし → ドリフト復活

累積補正を加算:
  trueDrift = 0 + 5,100 = 5,100 ms
  trueRate = 5,100 / 30 = 170 ms/min ← 正しい！
```

### 期待される精度

| 項目 | 補正前 | 補正後 |
|------|--------|--------|
| 1分あたりドリフト | ~170 ms | ~10 ms |
| 1時間あたりドリフト | ~10.2 秒 | ~0.6 秒 |
| 累積誤差 | 線形増加 | 自己修正 |

### デバッグ用: 30分同期

開発時は30分ごとにもNTP同期してレート較正を確認できる。

```cpp
// deep_sleep_manager.cpp
// TODO: Remove 30 min sync after drift rate calibration is verified
if (timeinfo.tm_min == 0 || timeinfo.tm_min == 30)
```

## ESP32 RTC クロックシステム

### クロックソース

| クロック | 周波数 | 用途 | 精度 |
|---------|-------|------|------|
| XTAL | 40 MHz | メインCPU、キャリブレーション基準 | ±10 ppm |
| RTC_SLOW_CLK | ~150 kHz | ディープスリープタイマー | ±5% (未キャリブ) |
| RTC_FAST_CLK | ~8 MHz | ULP、RTC peripherals | - |

### 自動キャリブレーション

ESP32は起動時に RTC_SLOW_CLK をキャリブレーションする：

1. 40MHz 水晶を基準として使用
2. RTC_SLOW_CLK の周期を測定
3. 補正係数を計算して保存

```
キャリブレーション:
  40MHz (正確) で 150kHz (不正確) の周期を測定
  → 1サイクル = 6.67μs のはずが 6.72μs だった
  → 補正係数 = 6.72 / 6.67 = 1.0075
```

### キャリブレーションの限界

- 起動時の温度でキャリブレーション
- スリープ中に温度が変わると精度が落ちる
- 実測で ~170ms/分 のドリフト（約10秒/時間）

## アダプティブスリープ調整

### 目的

毎分ちょうど（XX:XX:00）に表示を更新したい。

### 仕組み

```
                    estimatedProcessingTime
                    ◄──────────────────────►
┌───────────────────────────────────────────────────────┐
│ 12:30:00                               12:31:00       │
│    │                                      │           │
│    │◄────── sleepMs ──────►│◄── active ──►│           │
│    │                       │              │           │
│  スリープ                 起動         表示更新        │
│  開始                                                 │
└───────────────────────────────────────────────────────┘

sleepMs = (次の分境界までの時間) - estimatedProcessingTime
```

### フィードバックループ

表示更新時に「どれだけ遅れたか」を測定：

```cpp
delayAtDrawTime = 現在秒 + ミリ秒/1000  // 例: 0.5秒遅れ
```

| 状況 | 意味 | 調整 |
|------|------|------|
| 分境界まで待った | 早く起きすぎ | `est -= waitedSeconds * 0.5` |
| 0.1秒以上遅れて更新 | 遅く起きすぎ | `est += delayAtDrawTime * 0.5` |
| どちらも小さい | 完璧 | 調整なし |

### スムージング

- 係数 0.5 で急激な変動を防ぐ
- 範囲は 1〜20 秒にクランプ
- WiFi接続時は調整しない（NTPで時刻が変わるため）

## NTP同期

### 同期タイミング

- 初回起動時
- 毎時0分（`tm_min == 0` のとき）
- デバッグ時: 30分にも同期（TODO: 検証後に削除）

### カスタムNTP実装 (Dec 2025)

ESP-IDFのSNTP (`configTime`) ではなく、カスタムUDP実装を使用：

```cpp
// 直接UDPパケット送受信
WiFiUDP udp;
udp.beginPacket("ntp.nict.jp", 123);
udp.write(ntpPacket, 48);
udp.endPacket();
// 応答待ち → 即座に時刻設定
settimeofday(&tv, NULL);
```

**メリット:**
- 同期時間: ~50ms（SNTP: 2-5秒）
- 完了検出が確実
- ミリ秒精度の時刻取得

### RTC ドリフト測定

NTP同期時にドリフトを測定してログに記録：

```cpp
// NTP同期前のRTC時刻を保存
DeepSleepManager_SaveRtcTimeBeforeSync();

// カスタムNTP同期
NetworkManager_SyncNtp();

// ドリフト計算（MarkNtpSynced内）
residual = ntpTime - rtcTimeBeforeSync - ntpSyncDuration
trueDrift = residual + cumulativeCompensation
```

ログ例：
```
True drift: 5100 ms (residual: -50 ms + compensation: 5150 ms)
Drift rate updated: 169.5 ms/min (true rate: 170.0 ms/min over 30.0 min)
```

### センサーログに記録されるフィールド

NTP同期時に以下がJSONLとD1に記録される：

| フィールド | 説明 |
|-----------|------|
| `rtc_drift_ms` | 残差ドリフト（補正後の誤差） |
| `cumulative_comp_ms` | 累積補正量（前回NTP以降に適用した補正の合計） |
| `drift_rate` | 使用したドリフトレート（ms/分） |

```json
{"unixtimestamp":1766451000,"rtc_drift_ms":103,"cumulative_comp_ms":5100,"drift_rate":170.0,...}
```

## トラブルシューティング

### 時計が1分/時間ズレる

**原因**: 整数除算による切り捨て（2025年12月修正済み）

**確認**: ログで `.XXX` のミリ秒部分があるか確認
```
Time restored: 2025-12-22 10:38:58.652 (boot overhead: 89 ms)
                                  ↑ これがあればOK
```

### estimatedProcessingTime が異常に大きい

**原因**: 時刻復元のバグで毎サイクル遅れ → システムが補正しようとして増加

**対処**: ファームウェア更新後、自動的に正常値に収束

### スリープ時間が不安定

**原因**: WiFi接続時はNTP同期で時刻が変わるため

**対処**: WiFi接続時は調整をスキップ（正常動作）

## 関連ファイル

| ファイル | 役割 |
|---------|------|
| `deep_sleep_manager.cpp` | スリープ管理、時刻保存/復元 |
| `EPDEnvClock.ino` | アダプティブ調整ロジック |
| `network_manager.cpp` | NTP同期、ドリフト測定 |

## 診断機能 (Dec 2025)

### 毎ブートNTPドリフト測定（現在無効）

RTC ドリフトを詳細に分析するため、毎ブートで WiFi 接続して NTP 時刻を取得する機能。

**現状**: 分析完了につき無効化済み。ドリフト補正システムが代わりに動作。

**有効化方法** (`EPDEnvClock.ino`):
```cpp
bool measureDriftOnly = !needFullNtpSync;  // 毎ブートWiFi接続
```

### 30分NTP同期（デバッグ用）

ドリフトレート較正の検証用に、0分と30分でNTP同期。

**無効化方法** (`deep_sleep_manager.cpp`):
```cpp
// TODO コメントを削除して 30 分チェックを削除
if (timeinfo.tm_min == 0)  // || timeinfo.tm_min == 30 を削除
```

### 実測値

- RTC ドリフト: 約 50-140 ms/分（温度依存）
- ESP32 内蔵 150kHz RC オシレーターの典型的なドリフト率
- 補正後の残差: 約 100 ms/同期 以下
- EMA（指数移動平均）で自動較正

### 温度とドリフトレートの相関 (Dec 2025 観測)

夜間の温度変化とドリフトレートの関係を実測：

| 温度帯 | 真のドリフトレート | 備考 |
|--------|-------------------|------|
| 22.7-22.8°C | 110-130 ms/min | 温暖時（夕方〜夜） |
| 21.3-21.6°C | 40-60 ms/min | 低温時（深夜〜朝） |

**観察結果:**
- 温度が低いほどドリフトが小さい
- 室温 1°C の変化で約 30-50 ms/min の差
- RC オシレーターの温度特性による（低温で周波数が高くなる傾向）

**補正システムへの影響:**
- EMA が温度変化に追従して drift_rate を調整
- 温度安定時（夜間）は drift_rate が収束
- 温度変動時（日中）は drift_rate が変動

```
例: 2025-12-23〜24 夜間データ
23:00  22.1°C  drift_rate=143.8 → true_rate=143.8 ms/min
00:00  22.8°C  drift_rate=131.7 → true_rate=115.3 ms/min
03:00  22.1°C  drift_rate=89.8  → true_rate=77.0 ms/min
09:30  21.3°C  drift_rate=51.1  → true_rate=42.3 ms/min
```

この温度依存性はESP32 RTCの正常な挙動であり、EMAベースの補正システムが適応的に対応する。

## 参考資料

- [ESP32 Technical Reference Manual - RTC](https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf#page=591)
- [ESP-IDF Sleep Modes](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/sleep_modes.html)
