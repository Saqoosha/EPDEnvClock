# SDC41センサー統合ガイド

このドキュメントは、SDC41 CO2/温度/湿度センサーをCrowPanel ESP32-S3に接続する方法を説明します。

## ハードウェア接続

### 接続ピン

- **SDC41 VDD** → ESP32-S3 **3.3V**
- **SDC41 GND** → ESP32-S3 **GND**
- **SDC41 SDA** → ESP32-S3 **GPIO 38**
- **SDC41 SCL** → ESP32-S3 **GPIO 21**

**注意**: プルアップ抵抗はSDC41モジュールに内蔵されているため、追加のハードウェアは不要です。

## ライブラリのインストール

### Sensirion SCD4x Arduinoライブラリ

SDC41センサーを使用するには、Sensirion SCD4x Arduinoライブラリが必要です。

#### arduino-cliを使用する場合

```bash
arduino-cli lib install "Sensirion I2C SCD4x"
```

または、ライブラリのGitHubリポジトリから直接インストール：

```bash
arduino-cli lib install --git-url https://github.com/Sensirion/arduino-i2c-scd4x.git
```

#### Arduino IDEを使用する場合

1. Arduino IDEを開く
2. **スケッチ** → **ライブラリをインクルード** → **ライブラリを管理...**
3. 検索バーに「Sensirion I2C SCD4x」と入力
4. 「Sensirion I2C SCD4x」を選択してインストール

### ライブラリの確認

インストールされたライブラリを確認：

```bash
arduino-cli lib list | grep -i scd4x
```

## ソフトウェア設定

### I2Cピン設定

コード内で以下のようにI2Cピンを設定しています：

```cpp
#define I2C_SDA_PIN 38
#define I2C_SCL_PIN 21
```

### センサー読み取り間隔

デフォルトでは5秒ごとにセンサー値を読み取ります：

```cpp
#define SENSOR_READ_INTERVAL 5000  // ミリ秒
```

## 使用方法

### テストモード（EPD無効化）

現在のコードはEPD機能を無効化し、シリアル出力のみでセンサー値を確認するテストモードになっています。

1. シリアルモニターを115200bpsで開く
2. センサーが初期化され、5秒ごとに値が表示されます

### シリアル出力例

```
=== SDC41 Sensor Test ===
EPD features are temporarily disabled
Initializing SDC41 sensor...
SDC41 sensor initialized successfully!
Waiting for first measurement (5 seconds)...

Starting sensor readings...
Reading every 5 seconds...

=== SDC41 Sensor Reading ===
CO2: 420 ppm
Temperature: 23.5 °C
Humidity: 45.2 %RH
============================
```

## トラブルシューティング

### センサーが初期化できない場合

1. **接続を確認**:
   - SDAがGPIO 38に接続されているか
   - SCLがGPIO 21に接続されているか
   - VDDが3.3Vに接続されているか
   - GNDが接続されているか

2. **I2Cバスの確認**:
   - I2Cスキャナーを使用してセンサーが検出されるか確認
   - デフォルトI2Cアドレス: 0x62

3. **電源の確認**:
   - SDC41は2.4V-5.5Vで動作しますが、3.3V推奨
   - 電源が安定しているか確認

### データが読み取れない場合

1. **初期化時間**: センサーの初回起動時は約5秒の初期化時間が必要です
2. **読み取り間隔**: 定期的な読み取り間隔は5秒以上推奨（センサーの応答時間60秒を考慮）
3. **シリアル出力**: エラーメッセージを確認して問題を特定

## センサー仕様

- **I2Cアドレス**: 0x62 (デフォルト)
- **測定範囲**: 
  - CO2: 400-5000ppm
  - 温度: -10～+60°C
  - 湿度: 0-100%RH
- **精度**: 
  - CO2: ±(40ppm+5%)
  - 温度: ±0.8°C (15-35°Cの範囲)
  - 湿度: ±6%RH (15-35°C、20-65%RHの範囲)

## 参考リソース

- [LaskaKit SCD41 GitHub](https://github.com/LaskaKit/SCD41-CO2-Sensor)
- [Sensirion SCD4x Arduinoライブラリ](https://github.com/Sensirion/arduino-i2c-scd4x)
