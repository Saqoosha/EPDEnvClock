# コードレビュー指摘事項まとめ

## 📋 概要

複数のレビューコメントを整理し、優先順位をつけてまとめました。

---

## 🚨 Priority 1: クリティカル（即座に修正が必要）

### 1.1 Flashメモリの寿命問題（SPIFFSへの頻繁な書き込み）

**問題**: 毎分Deep Sleepに入る直前にSPIFFSにフレームバッファを保存している。Flashメモリの書き換え寿命を短期間で使い切る可能性がある。

**ESP32-S3 Flashメモリの仕様**:

- **書き込み/消去サイクル**: 10,000〜100,000回/セクター（メーカー・温度・使用条件により変動）
- **SPIFFSのウェアレベリング**: サポートされているが、頻繁な書き込みは寿命に影響
- **実際の寿命**: 最悪ケース（10,000回）から最良ケース（100,000回）まで幅がある

**影響**:

- 1分に1回更新 = 1日 1,440回
- **最悪ケース（10,000回）**: 10,000回 ÷ 1,440回/日 ≒ **6.9日**
- **最良ケース（100,000回）**: 100,000回 ÷ 1,440回/日 ≒ **69.4日**
- **現実的な見積もり**: 25,000〜50,000回程度と仮定すると **17〜35日程度**

**注意**: SPIFFSはウェアレベリングにより書き込みを分散するが、毎分の書き込みは特定のセクターに集中する可能性があり、寿命を短縮する可能性がある。

**場所**: `EPDClock/deep_sleep_manager.cpp:273` (`DeepSleepManager_SaveFrameBuffer`)

**解決策**:

- **TFカード（SDカード）への保存に変更**（推奨・実装済み）
  - SDカードは書き込み寿命が長い（高品質なものは100万回以上、一般的なものでも10万回以上）
  - ESP32のFlashメモリを保護できる
  - Deep Sleep後もデータ保持
  - SDカードが接続されていない場合は自動的にSPIFFSにフォールバック
- または外部SPI Flashチップ（書き込み寿命は10万回程度だが、ESP32のFlashとは別）
- RTCメモリは8KBしかなく、27,200バイトのフレームバッファ（RLE圧縮後でも最大54,400バイト）は収まらない

**参考情報**:

- ESP32-S3のFlashメモリは一般的に10,000〜100,000回の書き込み/消去サイクルに耐える
- SPIFFSはウェアレベリングをサポートしているが、頻繁な書き込みは寿命に影響する
- 外部ストレージ（SDカード）の使用が推奨される（Espressif公式ドキュメント、Stack Overflow等）

**関連ファイル**:

- `EPDClock/deep_sleep_manager.cpp`
- `EPDClock/display_manager.cpp:653`

---

### 1.2 毎ブート40秒ブロッキングで深睡眠設計が崩壊

**状態**: ✅ **問題なし** - 実際の測定により、この問題は存在しないことが確認された

**当初の懸念**: `setup()`のたびに`SensorManager_ReadBlocking(40000)`を呼んでおり、ウェイク後も最大40秒（平均でも十数秒）I2C待ちでボードが起きっぱなしになるのではないかという懸念があった。

**実際の測定結果** (2025-11-22):

- **Cold boot時** (`wakeFromSleep=false`):
  - データ準備完了まで: **2ms**
  - 読み取り時間: **2ms**
  - 合計時間: **15ms**
  
- **Wake from sleep時** (`wakeFromSleep=true`):
  - データ準備完了まで: **1ms**
  - 読み取り時間: **2ms**
  - 合計時間: **4ms**

**結論**:

- `SensorManager_ReadBlocking(40000)`のタイムアウトは40秒だが、実際の読み取り時間は**数ミリ秒**しかかからない
- センサーは既に動作中でデータが準備済みのため、待機時間はほぼゼロ
- 深睡眠設計に影響はない（実際のサイクル時間は約1分）

**場所**: `EPDClock/EPDClock.ino:111`

**関連ファイル**:

- `EPDClock/EPDClock.ino`
- `EPDClock/sensor_manager.cpp`

---

### 1.3 フレームバッファ保存が常に失敗する圧縮バッファサイズ ✅ 解決済み

**問題**: RLE圧縮用バッファを元画像と同じサイズで確保しているが、RLEは最悪ケースで入力の2倍メモリを要求する（1バイトごとに[長さ, 値]の2バイトになる）。

**影響**:

- `compressRLE`が頻繁に`0`を返す
- `rtcState.compressedImageSize`が0のまま
- 次回起動で復元できず毎回フル初期化
- フレームバッファ保存の意味がなくなる

**場所**: `EPDClock/deep_sleep_manager.cpp:280,289`

**解決策**: ✅ **実装済み**

- **TFカードサポートがあるため、RLE圧縮を完全に削除**
- 非圧縮で直接保存・読み込みに変更
- メモリ使用量削減（圧縮バッファが不要）
- 処理時間短縮
- コードの簡素化

**関連ファイル**:

- `EPDClock/deep_sleep_manager.cpp`
- `EPDClock/deep_sleep_manager.h`

---

### 1.4 気温が0℃未満になると描画がメモリ破壊

**問題**: 温度描画ロジックは負数を全く想定していない。`tempInt / 10`や`tempDecimal`を`uint8_t`にキャストしてそのままフォント配列のインデックスに使っている。

**影響**:

- `-1.2°C`のような入力だと巨大なインデックスに化けてPROGMEMを読みに行く
- クラッシュやゴミ表示になる

**場所**: `EPDClock/display_manager.cpp:146-214` (`calculateTemperatureWidth`, `drawTemperature`)

**解決策**:

- 0未満を0にクランプするか、マイナス記号＋絶対値表示を実装

**関連ファイル**:

- `EPDClock/display_manager.cpp`

---

## ⚠️ Priority 2: 高優先度（早めに修正すべき）

### 2.1 メモリリークの可能性（compressionBuffer）

**問題**: `compressionBuffer`がグローバル変数で、エラー時に解放漏れの可能性がある。

**場所**: `EPDClock/deep_sleep_manager.cpp:17,278-336`

**解決策**:

- ローカル変数にする
- RAIIパターンやスマートポインタの使用を検討

**関連ファイル**:

- `EPDClock/deep_sleep_manager.cpp`

---

### 2.2 SPIFFSのエラーハンドリング不足

**問題**: SPIFFSのマウント失敗時に処理を続行している。フレームバッファの保存/読み込みに失敗する可能性がある。

**場所**: `EPDClock/deep_sleep_manager.cpp:126-133,138-141`

**解決策**:

- マウント失敗時はフレームバッファの保存/読み込みをスキップするか、エラーを上位に伝播

**関連ファイル**:

- `EPDClock/deep_sleep_manager.cpp`

---

### 2.3 時刻復元の精度

**問題**: 固定の+1秒で起動オーバーヘッドを補正しているが、実際の起動時間は変動する可能性がある。

**場所**: `EPDClock/deep_sleep_manager.cpp:84`

**解決策**:

- ESP32のRTCタイマーを使った実測
- 起動時間を計測して補正するか、NTP同期の頻度を上げる

**関連ファイル**:

- `EPDClock/deep_sleep_manager.cpp`

---

## 📝 Priority 3: 中優先度（改善推奨）

### 3.1 タイムアウト処理の不整合

**問題**: `SensorManager_Read()`でタイムアウトチェックがあるが、`getDataReadyStatus()`の呼び出し後に時間を計測しているため、実際の処理時間を正確に計測できていない。

**場所**: `EPDClock/sensor_manager.cpp:176-180`

**解決策**:

- タイムアウトチェックを削除するか、実際の処理時間を計測するように修正

**関連ファイル**:

- `EPDClock/sensor_manager.cpp`

---

### 3.2 マジックナンバー

**問題**: `kNtpSyncInterval`が定義されているが使用されていない。代わりに`DeepSleepManager_ShouldSyncWiFiNtp()`で60ブート（約60分）をハードコードしている。

**場所**:

- `EPDClock/EPDClock.ino:15` (定義)
- `EPDClock/deep_sleep_manager.cpp:263` (使用箇所)

**解決策**:

- 定数を一元管理し、実際に使用する

**関連ファイル**:

- `EPDClock/EPDClock.ino`
- `EPDClock/deep_sleep_manager.cpp`

---

### 3.3 エラーメッセージの一貫性

**問題**: エラーメッセージのフォーマットが統一されていない（"Sensor Failed!" vs "WiFi FAILED!" vs "NTP FAILED!"）。

**場所**:

- `EPDClock/EPDClock.ino:53`
- その他のエラーメッセージ

**解決策**:

- エラーメッセージのフォーマットを統一

**関連ファイル**:

- `EPDClock/EPDClock.ino`
- `EPDClock/sensor_manager.cpp`
- `EPDClock/network_manager.cpp`

---

### 3.4 未使用の変数

**問題**: `networkState.lastNtpSync`が設定されているが、実際には`DeepSleepManager_ShouldSyncWiFiNtp()`で判定しているため、この値は使用されていない。

**場所**: `EPDClock/EPDClock.ino:17,131`

**解決策**:

- 未使用の変数を削除するか、実際に使用する

**関連ファイル**:

- `EPDClock/EPDClock.ino`
- `EPDClock/network_manager.h`

---

### 3.5 コメントの不整合

**問題**: コメントに「最適化できるか確認」とあるが、実装されていない。

**場所**: `EPDClock/sensor_manager.cpp:144-147`

**解決策**:

- コメントを更新するか、最適化を実装

**関連ファイル**:

- `EPDClock/sensor_manager.cpp`

---

### 3.6 文字列バッファのサイズ

**問題**: `statusLine`のサイズが128バイトだが、実際のフォーマット文字列が長い場合にオーバーフローの可能性がある。

**場所**: `EPDClock/display_manager.cpp:416`

**解決策**:

- バッファサイズを確認し、必要に応じて拡大

**関連ファイル**:

- `EPDClock/display_manager.cpp`

---

## 🔧 Priority 4: 低優先度（最適化）

### 4.1 フォントデータの最適化（PROGMEM）

**問題**: フォントデータがPROGMEMに入っていない。

**場所**: `EPDClock/display_manager.cpp:79-97`

**解決策**:

- すべてPROGMEMに入れる
- 読み取り時に`pgm_read_ptr`/`pgm_read_word`を使用

**関連ファイル**:

- `EPDClock/display_manager.cpp`

---

### 4.2 デバッグコードの整理

**問題**: コメントアウトされたデバッグコードがちらほらある。

**場所**: `EPDClock/sensor_manager.cpp:23`

**解決策**:

- 削除するか、適切なログレベルシステムを導入

**関連ファイル**:

- `EPDClock/sensor_manager.cpp`
- その他のファイル

---

### 4.3 EPD_Display関数の最適化

**問題**: 2回ほぼ同じループがある。

**場所**: `EPDClock/EPD_Init.cpp:239-273`

**解決策**:

- 共通化できる処理を関数化

**関連ファイル**:

- `EPDClock/EPD_Init.cpp`

---

### 4.4 C++の活用不足

**問題**: C言語スタイルで書かれているが、Arduino(C++)なのでC++の機能を活用できる。

**解決策**:

- `struct NetworkState` → `class NetworkManager`にカプセル化
- namespaceの活用
- RAII パターンでリソース管理

**注意**: C言語スタイルにも利点はあるので、これは好みの問題

---

## 📊 修正優先順位まとめ

### 即座に修正（Priority 1）

1. ✅ Flashメモリの寿命問題（SPIFFS → RTCメモリ）
2. ✅ 毎ブート40秒ブロッキング問題
3. ✅ 圧縮バッファサイズ不足
4. ✅ 気温0℃未満のメモリ破壊

### 早めに修正（Priority 2）

5. メモリリークの可能性
6. SPIFFSのエラーハンドリング不足
7. 時刻復元の精度

### 改善推奨（Priority 3）

8. タイムアウト処理の不整合
9. マジックナンバー
10. エラーメッセージの一貫性
11. 未使用の変数
12. コメントの不整合
13. 文字列バッファのサイズ

### 最適化（Priority 4）

14. フォントデータの最適化（PROGMEM）
15. デバッグコードの整理
16. EPD_Display関数の最適化
17. C++の活用不足

---

## 📝 補足

### 良い点（維持すべき）

- モジュール設計が秀逸（機能ごとにきれいに分離）
- Deep Sleep実装がしっかりしている
- 電力最適化が適切
- 境界チェックがしっかりしている

### 潜在的なバグ

- SPIFFS初期化の競合（確認が必要）
- 時刻のドリフト（実測して問題なければOK）
- フレームバッファの圧縮失敗時の動作（確認が必要）

---

## 🔗 関連ドキュメント

- [Arduino CLI コンパイル・アップロード手順書](../AGENTS.md)
- [ImageBW Export ドキュメント](./README_IMAGEBW.md)
- [SCD41 センサー ドキュメント](./README_SCD41.md)
