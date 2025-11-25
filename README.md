# EPDEnvClock

ESP32-S3ベースの電子ペーパー時計プロジェクト。SCD41 CO2/温度/湿度センサーを統合し、省電力設計で長時間動作します。

## 📋 概要

EPDEnvClockは、CrowPanel ESP32-S3 E-Paper 5.79インチディスプレイ（792x272ピクセル）を使用した時計アプリケーションです。以下の機能を提供します：

- **時刻・日付表示**: 大きな数字で時刻と日付を表示
- **環境センサー**: SCD41センサーによるCO2、温度、湿度の測定と表示
- **省電力設計**: Deep Sleepモードにより長時間動作（約1分間隔で更新）
- **WiFi接続**: NTP時刻同期とImageBWデータのWiFiエクスポート機能
- **フレームバッファ保存**: Deep Sleep後も前回の表示を復元

## ✨ 主な機能

### 表示機能

- **時刻表示**: 大きな数字フォント（Number L）で時刻を表示
- **日付表示**: 中サイズ数字フォント（Number M）で日付を表示
- **センサー値表示**: 温度、湿度、CO2濃度をアイコン付きで表示
- **ステータス表示**: WiFi接続状態、NTP同期状態、起動回数などを表示

### センサー機能

- **SCD41統合**: CO2（400-5000ppm）、温度（-10〜+60°C）、湿度（0-100%RH）を測定
- **省電力モード**: Idle Single-Shotモードで約1.5mAの消費電流
- **温度補正**: 自己発熱を補正する温度オフセット機能

### 省電力機能

- **Deep Sleep**: 約1分間隔でDeep Sleepに入り、消費電流を最小化
- **EPD Deep Sleep**: ディスプレイもDeep Sleepモードに入り、電力消費を削減
- **フレームバッファ保存**: SDカードまたはSPIFFSにフレームバッファを保存し、起動時に復元

### ネットワーク機能

- **WiFi接続**: 自動的にWiFiに接続
- **NTP同期**: 約60分ごとにNTPサーバーから時刻を同期
- **ImageBW Export**: WiFi経由で表示データをサーバーに送信（オプション）

## 🔧 ハードウェア要件

### CrowPanelに内蔵されているコンポーネント

- **ESP32-S3 Dev Module**
- **EPDディスプレイ**: 792x272ピクセル（マスター/スレーブ2つのSSD1683 ICで制御）
- **SDカードスロット**: フレームバッファ保存用（オプション、SPIFFSより書き込み寿命が長い）

### 外部コンポーネント（オプション）

- **SCD41センサー**: CO2/温度/湿度センサー（オプション）

### 接続ピン（SCD41センサー）

- **SCD41 VDD** → ESP32-S3 **3.3V**
- **SCD41 GND** → ESP32-S3 **GND**
- **SCD41 SDA** → ESP32-S3 **GPIO 38**
- **SCD41 SCL** → ESP32-S3 **GPIO 20**

**注意**: プルアップ抵抗はSCD41モジュールに内蔵されているため、追加のハードウェアは不要です。

## 🚀 セットアップ

### 1. 必要なソフトウェア

#### arduino-cliのインストール

**macOS**:

```bash
brew install arduino-cli
```

**Linux**:

```bash
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
```

**Windows**:
[公式サイト](https://arduino.github.io/arduino-cli/latest/installation/)からインストーラーをダウンロード

#### ESP32ボードサポートのインストール

```bash
arduino-cli core update-index
arduino-cli core install esp32:esp32
```

#### ライブラリのインストール

```bash
# Sensirion SCD4xライブラリ
arduino-cli lib install "Sensirion I2C SCD4x"
```

### 2. Wi-Fi設定

`EPDEnvClock/wifi_config.h.example`をコピーして`EPDEnvClock/wifi_config.h`を作成し、Wi-Fi認証情報を設定：

```cpp
#define WIFI_SSID "your_wifi_ssid"
#define WIFI_PASSWORD "your_wifi_password"
```

**注意**: `wifi_config.h`は`.gitignore`に含まれているため、コミットされません。

### 3. ImageBW Export設定（オプション）

`EPDEnvClock/server_config.h`でサーバーのIPアドレスとポートを設定：

```cpp
#define ENABLE_IMAGEBW_EXPORT 1  // 1で有効、0で無効
#define SERVER_IP "192.168.1.100"  // サーバーのIPアドレス
#define SERVER_PORT 8080           // サーバーポート
```

## 📦 ビルド・アップロード

### 推奨方法（コンパイル + アップロード）

```bash
cd /path/to/EPDEnvClock
arduino-cli compile --fqbn esp32:esp32:esp32s3:PartitionScheme=huge_app,PSRAM=opi --upload -p /dev/cu.wchusbserial110 EPDEnvClock
```

**注意**: `/path/to/EPDEnvClock`を実際のプロジェクトディレクトリのパスに置き換えてください。ポート名（`/dev/cu.wchusbserial110`）も環境に応じて変更してください。

**重要な設定パラメータ**:

- **FQBN**: `esp32:esp32:esp32s3`
- **PartitionScheme**: `huge_app` (Huge APP: 3MB No OTA/1MB SPIFFS)
- **PSRAM**: `opi` (OPI PSRAM)

### ポートの確認

```bash
arduino-cli board list
```

### 詳細な手順

詳細なビルド・アップロード手順は [AGENTS.md](./AGENTS.md) を参照してください。

## 💻 使用方法

### 基本的な動作

1. **起動**: ESP32-S3に電源を供給すると、自動的に起動します
2. **初期化**: センサーとWiFiの初期化が行われます（初回起動時）
3. **表示更新**: 約1分ごとに表示が更新されます
4. **Deep Sleep**: 表示更新後、Deep Sleepモードに入ります

### 表示内容

画面レイアウト（792x272ピクセル）：

- **上部（y=4）**: ステータス情報（WiFi接続状態、NTP同期状態、稼働時間、空きメモリ）
- **左側上部（y=55）**: 日付（YYYY/MM/DD形式、中サイズ数字）
- **左側中央（y=133）**: 時刻（HH:MM形式、大きな数字）
- **右側上部（y=33）**: 温度（アイコン + 値 + °C単位）
- **右側中央（y=114）**: 湿度（アイコン + 値 + %単位）
- **右側下部（y=193）**: CO2濃度（アイコン + 値 + ppm単位）

### ImageBW Export機能（オプション）

表示データをWiFi経由でサーバーに送信する場合：

1. **サーバーの起動**（Python 3が必要）:

```bash
cd /path/to/EPDEnvClock
python3 scripts/imagebw_server.py --port 8080
```

2. **Arduino側の設定**: `server_config.h`でサーバーのIPアドレスを設定

3. **自動送信**: 表示が更新されるたびに自動的にImageBWデータが送信されます

受信したデータは`output/`ディレクトリにPNGファイルとして保存されます。

**注意**: サーバーはPython 3がインストールされている任意のプラットフォーム（Windows、macOS、Linuxなど）で動作します。

詳細は [docs/README_IMAGEBW.md](./docs/README_IMAGEBW.md) を参照してください。

## 📁 プロジェクト構造

```
EPDEnvClock/
├── EPDEnvClock/                  # Arduino/Firmwareコード（スケッチディレクトリ）
│   ├── EPDEnvClock.ino          # メインスケッチ
│   ├── EPD.h / EPD.cpp       # EPDライブラリ
│   ├── EPD_Init.h / EPD_Init.cpp  # EPD初期化ライブラリ
│   ├── spi.h / spi.cpp       # SPIライブラリ
│   ├── display_manager.h / display_manager.cpp  # 表示管理
│   ├── sensor_manager.h / sensor_manager.cpp    # センサー管理
│   ├── network_manager.h / network_manager.cpp  # ネットワーク管理
│   ├── deep_sleep_manager.h / deep_sleep_manager.cpp  # Deep Sleep管理
│   ├── imagebw_export.h / imagebw_export.cpp    # ImageBW Export
│   ├── logger.h / logger.cpp # ログ機能
│   ├── EPDfont.h             # フォントデータ
│   ├── wifi_config.h          # Wi-Fi設定（gitignore）
│   ├── server_config.h        # サーバー設定
│   └── bitmaps/               # ビットマップヘッダーファイル
│       ├── Number_L_bitmap.h  # 数字フォント（大）
│       ├── Number_M_bitmap.h   # 数字フォント（中）
│       ├── Number_S_bitmap.h   # 数字フォント（小）
│       └── ...
├── scripts/                   # Pythonスクリプト
│   ├── convert_image.py       # 画像変換スクリプト
│   ├── convert_imagebw.py     # ImageBW変換スクリプト
│   ├── convert_numbers.py     # 数字画像変換スクリプト
│   ├── create_number_bitmaps.py  # 数字ビットマップ生成スクリプト
│   └── imagebw_server.py      # ImageBW受信サーバー
├── assets/                    # アセット（画像ファイルなど）
│   ├── Number L/              # 大きい数字フォント画像
│   ├── Number M/              # 中サイズ数字フォント画像（58px高）
│   └── Number S/              # 小さい数字フォント画像
├── docs/                      # ドキュメント
│   ├── README.md              # ドキュメントインデックス
│   ├── README_IMAGEBW.md      # ImageBW機能ガイド
│   ├── README_SCD41.md        # SCD41センサーガイド
│   └── reviews/               # コードレビュー
│       └── SENSOR_MANAGEMENT_REVIEW.md
├── output/                    # 生成された画像出力（gitignore）
├── AGENTS.md                  # Arduino CLI手順書
└── README.md                  # このファイル
```

## 📚 ドキュメント

### 主要ドキュメント

- **[AGENTS.md](./AGENTS.md)** - Arduino CLIコンパイル・アップロード手順書
- **[docs/README.md](./docs/README.md)** - ドキュメントインデックス
- **[docs/README_IMAGEBW.md](./docs/README_IMAGEBW.md)** - ImageBW WiFi Export機能の使い方
- **[docs/README_SCD41.md](./docs/README_SCD41.md)** - SCD41センサー統合ガイド

### コードレビュー

- **[docs/reviews/SENSOR_MANAGEMENT_REVIEW.md](./docs/reviews/SENSOR_MANAGEMENT_REVIEW.md)** - センサー管理の実装状況と改善提案

## 🔋 省電力設計

### 消費電流

- **SCD41**: 約1.5mA（Idle Single-Shotモード、1分おき測定）
- **ESP32-S3**: Deep Sleep中は数十〜数百µA
- **合計**: Deep Sleep中は約0.2〜0.3mA

### バッテリー持続時間（1480mAhバッテリーの場合）

- **平均消費電流**: 約2.5mA
- **持続時間**: 1480mAh ÷ 2.5mA ≈ **592時間（約25日）**

### Deep Sleepサイクル

- **更新間隔**: 約1分
- **動作時間**: 約10-15秒（センサー読み取り、表示更新、WiFi処理）
- **Deep Sleep時間**: 約45-50秒

## 🎨 フォント生成

数字フォント（Number S、Number M、Number L）は`scripts/create_number_bitmaps.py`を使用して生成します。

### 使用フォント

**重要**: すべての数字フォントは以下のフォントファイルを使用します：

- **フォント名**: Baloo Bhai 2
- **スタイル**: Extra Bold

### Number Mフォントの生成例

```bash
cd /path/to/EPDEnvClock
python3 scripts/create_number_bitmaps.py \
  --font-path "/path/to/fonts/BalooBhai2-ExtraBold.ttf" \
  --font-size-px 90 \
  --output-dir "assets/Number M"
```

**注意**:

- `/path/to/EPDEnvClock`を実際のプロジェクトディレクトリのパスに置き換えてください
- `/path/to/fonts/BalooBhai2-ExtraBold.ttf`を実際のフォントファイルのパスに置き換えてください

詳細は [AGENTS.md](./AGENTS.md) の「数字フォントの生成」セクションを参照してください。

## ⚙️ 技術仕様

### ESP32-S3設定

- **Board**: ESP32S3 Dev Module
- **Partition Scheme**: Huge APP (3MB No OTA/1MB SPIFFS)
- **PSRAM**: OPI PSRAM
- **CPU Frequency**: 240MHz (WiFi)
- **Flash Mode**: QIO 80MHz
- **Flash Size**: 4MB (32Mb)
- **Upload Speed**: 921600

### EPDディスプレイ仕様

- **実際の解像度**: 792x272ピクセル
- **コントローラー**: マスター/スレーブの2つのSSD1683 IC
  - 各コントローラー: 396x272ピクセルを担当
  - 中央に4pxのギャップ（コントローラー間の接続部分）
- **プログラム定義**: `EPD_W = 800`, `EPD_H = 272`（アドレスオフセット用）
- **バッファサイズ**: 800x272 = 27,200バイト

### SCD41センサー仕様

- **I2Cアドレス**: 0x62 (デフォルト)
- **測定範囲**:
  - CO2: 400-5000ppm
  - 温度: -10～+60°C
  - 湿度: 0-100%RH
- **精度**:
  - CO2: ±(40ppm+5%)
  - 温度: ±0.8°C (15-35°Cの範囲)
  - 湿度: ±6%RH (15-35°C、20-65%RHの範囲)

## 🐛 トラブルシューティング

### コンパイルエラー

- **エラー**: "Invalid FQBN"
  - **解決策**: FQBNの形式を確認。オプションは`:`で区切る（例: `esp32:esp32:esp32s3:PartitionScheme=huge_app,PSRAM=opi`）

### アップロードエラー

- **エラー**: "Unable to verify flash chip connection"
  - **解決策**:
    - 別のUSBポートを試す（`arduino-cli board list`で確認）
    - ボードのリセットボタンを押す
    - USBケーブルを確認（データ転送対応のケーブルか確認）

- **エラー**: ポートが見つからない
  - **解決策**:
    - USBケーブルを接続し直す
    - `arduino-cli board list`でポートを再確認
    - デバイスドライバが正しくインストールされているか確認

### センサーが初期化できない場合

1. **接続を確認**:
   - SDAがGPIO 38に接続されているか
   - SCLがGPIO 21に接続されているか
   - VDDが3.3Vに接続されているか
   - GNDが接続されているか

2. **I2Cバスの確認**:
   - I2Cスキャナーを使用してセンサーが検出されるか確認
   - デフォルトI2Cアドレス: 0x62

詳細は [docs/README_SCD41.md](./docs/README_SCD41.md) の「トラブルシューティング」セクションを参照してください。

## 📝 ライセンス

このプロジェクトのライセンス情報は記載されていません。使用する際は、各ライブラリのライセンスを確認してください。

## 📧 連絡先

プロジェクトに関する質問や問題がある場合は、GitHubのIssuesで報告してください。
