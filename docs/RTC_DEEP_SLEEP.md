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

### RTC ドリフト測定

NTP同期時にRTCのドリフトを測定してログに記録：

```cpp
// NTP同期前のRTC時刻を保存
DeepSleepManager_SaveRtcTimeBeforeSync();

// NTP同期
configTime(gmtOffset, 0, "ntp.nict.jp");

// ドリフト計算
drift = ntpTime - rtcTimeBeforeSync - ntpSyncDuration
```

ログ例：
```json
{"rtc_drift_ms": 510, ...}
```

510ms/hour = 0.014% のドリフト（良好な精度）

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

## 一時的な診断機能: 毎ブートNTPドリフト測定 (Dec 2025)

### 概要

RTC ドリフトを詳細に分析するため、毎ブートで WiFi 接続して NTP 時刻を取得し、
システム時刻との差分（ドリフト）を測定する機能。

**注意**: この機能は診断目的で、バッテリー消費が約50%増加する。
分析完了後は無効化を検討すること。

### 動作

1. 毎ブートで WiFi 接続
2. カスタム NTP 実装でミリ秒精度の時刻取得（システムクロック変更なし）
3. ドリフト = NTP時刻 - システム時刻
4. ドリフトをログに記録（`rtc_drift_ms`）
5. 毎時0分のみシステムクロックを NTP で更新

### ログ例

```
NTP drift measured: 743 ms (NTP: 1766379536.512, System: 1766379535.769)
```

### 無効化方法

`EPDEnvClock.ino` で以下を変更:

```cpp
// 現在（毎ブートWiFi接続）
bool measureDriftOnly = !needFullNtpSync;

// 無効化（従来動作に戻す）
bool measureDriftOnly = false;
```

### 実測値

- RTC ドリフト: 約 170-200 ms/分 (10-12 秒/時間)
- ESP32 内蔵 150kHz RC オシレーターの典型的なドリフト率

## 参考資料

- [ESP32 Technical Reference Manual - RTC](https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf#page=591)
- [ESP-IDF Sleep Modes](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/sleep_modes.html)
