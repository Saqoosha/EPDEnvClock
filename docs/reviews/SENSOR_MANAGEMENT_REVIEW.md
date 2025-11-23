# センサー管理コードレビュー

## 📋 概要

SCD41 CO2/温度/湿度センサーの管理方法と実装状況をまとめたドキュメント。

---

## 🏗️ アーキテクチャ

### ファイル構成

```
EPDClock/
├── sensor_manager.h          # センサー管理API（ヘッダー）
├── sensor_manager.cpp        # センサー管理実装
└── EPDClock.ino             # メインスケッチ（初期化・読み取り呼び出し）
```

### モジュール設計

- **センサー管理**: `SensorManager_*` 関数群でカプセル化
- **状態管理**: 内部状態（`sensorInitialized`, `lastTemperature`など）をnamespace内で管理
- **I2C通信**: SensirionI2cScd4xライブラリを使用

---

## 🔌 ハードウェア設定

### I2C接続

```cpp
constexpr uint8_t I2C_SDA_PIN = 38;
constexpr uint8_t I2C_SCL_PIN = 21;
constexpr uint8_t SCD4X_I2C_ADDRESS = 0x62;
```

- **SDA**: GPIO 38
- **SCL**: GPIO 21
- **I2C周波数**: 100kHz（Standard Mode）
- **アドレス**: 0x62（デフォルト）

---

## 🔄 初期化フロー

### Cold Boot時（`wakeFromSleep=false`）

1. **I2Cバス初期化**
   - `Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN)`
   - `Wire.setClock(100000)` - 100kHz
   - `delay(100)` - バス安定化待機

2. **センサー初期化**
   - `scd4x.begin(Wire, SCD4X_I2C_ADDRESS)`

3. **周期測定モード停止**
   - `stopPeriodicMeasurement()` - デフォルトで周期測定が動作しているため停止
   - `delay(1000)` - 完全停止まで待機

4. **温度オフセット設定**
   - `setTemperatureOffset(4.0f)` - 4.0°Cに設定
   - 設定値を読み戻して確認（デバッグ用）

5. **状態設定**
   - `sensorInitialized = true`
   - シングルショットモード準備完了

### Wake from Sleep時（`wakeFromSleep=true`）

1. **簡易初期化**
   - I2Cバス初期化のみ
   - センサーは既にidle状態（周期測定停止済み）
   - `sensorInitialized = true` を設定して完了

**理由**: Deep Sleep中もセンサーは電源ONのままidle状態を維持しているため、再初期化は不要。

---

## 📊 測定モード

### 現在の実装: **Idle Single-Shot Mode**

**選択理由**:
- 1分おき測定では、idle single-shot（約1.5mA）がpower-cycled single-shot（約2.6mA）より省電力
- ASC（自動セルフキャリブレーション）が有効
- 測定間隔が380秒未満の場合に最適

**動作**:
- センサーは常にidle状態（約0.2mA）
- 測定時のみ`measureSingleShot()`を呼ぶ
- Deep Sleep前に`powerDown()`しない

### 測定フロー

```cpp
SensorManager_ReadBlocking(timeoutMs)
  ↓
1. stopPeriodicMeasurement() - 安全のため（既に停止済み）
  ↓
2. measureSingleShot() - シングルショット測定開始（内部で5秒待機）
  ↓
3. readMeasurement() - 測定値を読み取り
  ↓
4. 内部状態を更新（lastTemperature, lastHumidity, lastCO2）
```

---

## 🔋 省電力戦略

### 消費電流（1分おき測定の場合）

| モード | SCD41消費電流 | 説明 |
|--------|--------------|------|
| **Idle Single-Shot** | **約1.5mA** | 現在の実装（推奨） |
| Power-Cycled Single-Shot | 約2.6mA | 380秒以上の間隔で有効 |
| Low-Power Periodic (30s) | 約3.2mA | 周期測定モード |

### Deep Sleepとの統合

- **ESP32-S3**: Deep Sleep中は数十〜数百µA
- **SCD41**: idle状態のまま（約0.2mA）
- **合計**: Deep Sleep中は約0.2〜0.3mA

**注意**: Deep Sleep前に`powerDown()`を呼ばない（idle single-shotの方が省電力のため）

---

## 📖 API一覧

### 初期化

```cpp
bool SensorManager_Begin(bool wakeFromSleep);
```
- Cold boot時: フル初期化（周期測定停止、温度オフセット設定）
- Wake from sleep時: 簡易初期化（I2Cバス初期化のみ）

### 測定

```cpp
bool SensorManager_ReadBlocking(unsigned long timeoutMs = 10000);
```
- **推奨**: シングルショットモード用
- ブロッキング読み取り（最大timeoutMs待機）
- `measureSingleShot()`を使用（内部で5秒待機）

```cpp
void SensorManager_Read();
```
- **非推奨**: 周期測定モード用
- 非ブロッキング読み取り
- `getDataReadyStatus()`でデータ準備を確認
- **問題**: 現在はシングルショットモードなので、この関数は動作しない可能性がある

### 状態取得

```cpp
bool SensorManager_IsInitialized();
float SensorManager_GetTemperature();
float SensorManager_GetHumidity();
uint16_t SensorManager_GetCO2();
```

### 省電力制御（現在未使用）

```cpp
void SensorManager_PowerDown();
void SensorManager_WakeUp();
```
- **注意**: 1分おき測定では使用しない（idle single-shotの方が省電力）
- 380秒以上の測定間隔の場合に有効

---

## 🔍 使用箇所

### EPDClock.ino

1. **初期化** (`setup()`)
   ```cpp
   handleSensorInitializationResult(wakeFromSleep);
   sensorInitialized = SensorManager_IsInitialized();
   ```

2. **初回読み取り** (`setup()`)
   ```cpp
   if (sensorInitialized) {
     SensorManager_ReadBlocking(timeoutMs);  // 推奨
     // フォールバック: SensorManager_Read();  // 非推奨
   }
   ```

3. **Deep Sleep前**
   - `SensorManager_PowerDown()`は呼ばない（idle状態を維持）

### display_manager.cpp

1. **分更新時の読み取り**
   ```cpp
   if (SensorManager_IsInitialized()) {
     SensorManager_Read();  // ⚠️ 問題: シングルショットモードでは動作しない
   }
   ```

2. **値の取得**
   ```cpp
   float temp = SensorManager_GetTemperature();
   float humidity = SensorManager_GetHumidity();
   uint16_t co2 = SensorManager_GetCO2();
   ```

---

## ⚠️ 問題点と改善提案

### 1. `SensorManager_Read()`の使用

**問題**:
- `display_manager.cpp:596`で`SensorManager_Read()`を使用
- この関数は周期測定モードを前提としている
- 現在はシングルショットモードなので、データが準備されていない可能性がある

**影響**:
- 分更新時にセンサー値が更新されない可能性
- 古い値が表示され続ける

**解決策**:
- `display_manager.cpp`で`SensorManager_ReadBlocking()`を使用する
- または、`SensorManager_Read()`を削除して、`setup()`で読み取った値のみを使用

### 2. `setup()`のタイムアウト設定

**問題**:
- `wakeFromSleep=true`時のタイムアウトが2秒
- シングルショットモードは5秒かかるため、タイムアウトが短すぎる

**現在のコード**:
```cpp
unsigned long timeoutMs = wakeFromSleep ? 2000 : 5000;
```

**解決策**:
- シングルショットモードは常に5秒以上かかるため、タイムアウトを統一
- `timeoutMs = 6000;` など、余裕を持たせる

### 3. フォールバック処理

**問題**:
- `setup()`で`SensorManager_ReadBlocking()`が失敗した場合、`SensorManager_Read()`をフォールバックとして呼んでいる
- しかし、シングルショットモードでは`SensorManager_Read()`は動作しない

**解決策**:
- フォールバック処理を削除するか、エラーメッセージを出力して終了

---

## 📈 期待される消費電流

### 現在の実装（Idle Single-Shot）

- **SCD41**: 約1.5mA（1分おき測定）
- **ESP32-S3**: 約1mA（Deep Sleep中心）
- **合計**: 約2.5mA

### バッテリー持続時間

- **1480mAh ÷ 2.5mA ≈ 592時間（約25日）**

---

## 🔧 推奨される改善

### 優先度: 高

1. **`display_manager.cpp`の修正**
   - `SensorManager_Read()`を`SensorManager_ReadBlocking()`に変更
   - または、分更新時の読み取りを削除（`setup()`で読み取った値のみ使用）

2. **タイムアウト設定の修正**
   - `setup()`のタイムアウトを6秒以上に統一

### 優先度: 中

3. **フォールバック処理の見直し**
   - `SensorManager_Read()`へのフォールバックを削除

4. **`SensorManager_Read()`の削除または警告**
   - シングルショットモードでは使用不可であることを明確化

---

## 📝 まとめ

### 現在の実装状況

✅ **良好な点**:
- Idle single-shotモードを正しく実装
- Deep Sleep前に`powerDown()`を呼ばない（省電力）
- 初期化フローが適切

⚠️ **改善が必要な点**:
- `display_manager.cpp`で`SensorManager_Read()`を使用（シングルショットモードでは動作しない）
- `setup()`のタイムアウト設定が不適切
- フォールバック処理が機能しない

### 推奨アクション

1. `display_manager.cpp`を修正して`SensorManager_ReadBlocking()`を使用
2. `setup()`のタイムアウトを6秒以上に統一
3. 不要なフォールバック処理を削除

---

## 📚 参考資料

- [Sensirion SCD4x Low Power Operation Modes](https://sensirion.com/media/documents/077BC86F/62BF01B9/CD_AN_SCD4x_Low_Power_Operation_D1.pdf)
- [Sensirion SCD4x Datasheet](https://sensirion.com/media/documents/E0F04247/631EF271/CD_DS_SCD40_SCD41_Datasheet_D1.pdf)
