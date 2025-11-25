# EPDEnvClock

ESP32-S3ベースの電子ペーパー時計プロジェクト。SCD41 CO2/温度/湿度センサーを統合し、省電力設計で長時間動作します。

## 📋 概要

EPDEnvClockは、CrowPanel ESP32-S3 E-Paper 5.79インチディスプレイ（792x272ピクセル）を使用した時計アプリケーションです。以下の機能を提供します：

- **時刻・日付表示**: 大きな数字で時刻と日付を表示
- **環境センサー**: SCD41センサーによるCO2、温度、湿度の測定と表示
- **省電力設計**: Deep Sleepモードにより長時間動作（約1分間隔で更新）
- **WiFi接続**: NTP時刻同期とImageBWデータのWiFiエクスポート機能
- **バッテリー電圧監視**: リアルタイムでバッテリー電圧を表示
- **ボタンウェイクアップ**: HOMEボタンでDeep Sleepから復帰して全画面更新

## ✨ 主な機能

### 表示機能

- **時刻表示**: 大きな数字フォント（Number L）で時刻を表示（カーニング対応）
- **日付表示**: 中サイズ数字フォント（Number M）で日付を表示（YYYY.MM.DD形式）
- **センサー値表示**: 温度、湿度、CO2濃度をアイコン付きで表示
- **ステータス表示**: バッテリー電圧、WiFi接続状態、NTP同期状態、稼働時間、空きメモリなどを表示

### センサー機能

- **SCD41統合**: CO2（400-5000ppm）、温度（-10〜+60°C）、湿度（0-100%RH）を測定
- **省電力モード**: Single-Shotモードで約1.5mAの消費電流（Light Sleep中に測定完了を待機）
- **温度補正**: 自己発熱を補正する温度オフセット機能（4.0°C）
- **自動キャリブレーション**: ASC（Automatic Self-Calibration）対応

### 省電力機能

- **Deep Sleep**: 約1分間隔でDeep Sleepに入り、消費電流を最小化
- **Light Sleep**: センサー測定待機中（約5秒）はLight Sleepで消費電力を削減
- **EPD Deep Sleep**: ディスプレイもDeep Sleepモードに入り、電力消費を削減
- **フレームバッファ保存**: SDカードまたはSPIFFSにフレームバッファを保存し、起動時に復元
- **SDカード電源制御**: Deep Sleep中はSDカードの電源をオフにして消費電流を削減
- **WiFi省電力**: NTP同期は約60分（60回の起動）ごとに1回のみ実行

### ネットワーク機能

- **WiFi接続**: 自動的にWiFiに接続
- **NTP同期**: 約60分ごとにNTPサーバーから時刻を同期（RTC時刻を保持）
- **ImageBW Export**: WiFi経由で表示データをサーバーに送信（オプション）

### データロギング機能

- **センサーログ**: SDカードにJSONL形式でセンサー値を自動記録
- **記録項目**: 日付、時刻、Unixタイムスタンプ、NTP同期からの経過時間、温度、湿度、CO2、バッテリーADC値、バッテリー電圧
- **ファイル形式**: `/sensor_logs/sensor_log_YYYYMMDD.jsonl`（日付ごとにファイルを分割）
- **フォールバック**: SDカードが使用できない場合はSPIFFSに保存

### ボタン機能

- **HOMEボタン (GPIO 2)**: Deep Sleepから復帰し、全画面更新を実行
- **その他のボタン**: EXIT, PRV, NEXT, OKボタンもサポート（将来の拡張用）

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

#### USBシリアルドライバのインストール

CrowPanel ESP32-S3は **CH340** USBシリアルチップを使用しています。

**macOS (10.14 Mojave以降)**:
追加のドライバは**不要**です。macOS 10.14以降はCH340をネイティブサポートしています。

- デバイスを接続すると `/dev/cu.usbserial-*` または `/dev/cu.wchusbserial*` として認識されます
- **注意**: 追加ドライバをインストールすると逆に問題が発生する場合があります

**macOS (10.13以前)**:
ドライバのインストールが必要です：

- Homebrew: `brew install --cask wch-ch34x-usb-serial-driver`
- または [SparkFun CH340ドライバガイド](https://learn.sparkfun.com/tutorials/how-to-install-ch340-drivers)

**Windows**:
通常は自動認識されます。認識されない場合：

- [SparkFun CH340ドライバガイド](https://learn.sparkfun.com/tutorials/how-to-install-ch340-drivers)からダウンロード

**Linux**:
カーネルに組み込み済みで追加インストール不要です。認識されない場合：

```bash
sudo modprobe ch34x
```

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
arduino-cli core install esp32:esp32@2.0.7
```

**注意**: ESP32コアのバージョンは`2.0.7`を使用してください。新しいバージョンでは互換性の問題が発生する可能性があります。

#### ライブラリのインストール

```bash
# Sensirion SCD4xライブラリ（依存するSensirion Coreも自動でインストールされます）
arduino-cli lib install "Sensirion I2C SCD4x@0.4.0"
```

### 開発環境のバージョン情報

| コンポーネント | バージョン | 備考 |
|---------------|-----------|------|
| arduino-cli | 最新版推奨 | `brew install arduino-cli` (macOS) |
| ESP32 Core | 2.0.7 | `esp32:esp32@2.0.7` |
| Sensirion I2C SCD4x | 0.4.0 | CO2/温度/湿度センサーライブラリ |
| Sensirion Core | 0.6.0 | 依存ライブラリ（自動インストール） |

#### インストール済みライブラリの確認

```bash
arduino-cli lib list
```

#### ESP32コアバージョンの確認

```bash
arduino-cli core list
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
5. **ボタン復帰**: HOMEボタンを押すとDeep Sleepから復帰して全画面更新

### 表示内容

画面レイアウト（792x272ピクセル）：

- **上部（y=4）**: ステータス情報（バッテリー電圧、WiFi接続状態、NTP同期状態、稼働時間、空きメモリ）
- **左側上部（y=45）**: 日付（YYYY.MM.DD形式、中サイズ数字）
- **左側中央（y=123）**: 時刻（H:MM または HH:MM形式、大きな数字）
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
│   ├── EPD.h / EPD.cpp          # EPDライブラリ
│   ├── EPD_Init.h / EPD_Init.cpp  # EPD初期化ライブラリ
│   ├── spi.h / spi.cpp          # SPIライブラリ
│   ├── display_manager.h / display_manager.cpp  # 表示管理
│   ├── font_renderer.h / font_renderer.cpp      # フォントレンダラー（カーニング対応）
│   ├── sensor_manager.h / sensor_manager.cpp    # センサー管理
│   ├── network_manager.h / network_manager.cpp  # ネットワーク管理
│   ├── deep_sleep_manager.h / deep_sleep_manager.cpp  # Deep Sleep管理
│   ├── imagebw_export.h / imagebw_export.cpp    # ImageBW Export
│   ├── logger.h / logger.cpp    # ログ機能（タグ/レベル対応）
│   ├── EPDfont.h                # フォントデータ（12pxテキスト用）
│   ├── wifi_config.h            # Wi-Fi設定（gitignore）
│   ├── server_config.h          # サーバー設定
│   └── bitmaps/                 # ビットマップヘッダーファイル
│       ├── Number_L_bitmap.h    # 数字フォント（大）
│       ├── Number_M_bitmap.h    # 数字フォント（中）
│       ├── Kerning_table.h      # カーニングテーブル
│       ├── Icon_temp_bitmap.h   # 温度アイコン
│       ├── Icon_humidity_bitmap.h  # 湿度アイコン
│       ├── Icon_co2_bitmap.h    # CO2アイコン
│       └── Unit_*.h             # 単位ビットマップ
├── scripts/                     # Pythonスクリプト
│   ├── convert_image.py         # 画像変換スクリプト
│   ├── convert_imagebw.py       # ImageBW変換スクリプト
│   ├── convert_numbers.py       # 数字画像変換スクリプト
│   ├── create_number_bitmaps.py # 数字ビットマップ生成スクリプト
│   └── imagebw_server.py        # ImageBW受信サーバー
├── assets/                      # アセット（画像ファイルなど）
│   ├── Number L/                # 大きい数字フォント画像
│   ├── Number M/                # 中サイズ数字フォント画像（58px高）
│   └── Number S/                # 小さい数字フォント画像
├── docs/                        # ドキュメント
│   ├── README.md                # ドキュメントインデックス
│   ├── README_IMAGEBW.md        # ImageBW機能ガイド
│   ├── README_SCD41.md          # SCD41センサーガイド
│   └── reviews/                 # コードレビュー
│       └── SENSOR_MANAGEMENT_REVIEW.md
├── output/                      # 生成された画像出力（gitignore）
├── AGENTS.md                    # Arduino CLI手順書
└── README.md                    # このファイル
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

| 状態 | 消費電流 |
|------|----------|
| SCD41 Idle Single-Shot | 約1.5mA |
| ESP32-S3 Deep Sleep | 約0.2〜0.3mA |
| ESP32-S3 Light Sleep (センサー測定待機中) | 約2〜3mA |
| ESP32-S3 Active (WiFi含む) | 約80〜150mA |

### バッテリー持続時間（1480mAhバッテリーの場合）

- **平均消費電流**: 約2.5mA（WiFi同期は1時間に1回）
- **持続時間**: 1480mAh ÷ 2.5mA ≈ **592時間（約25日）**

### Deep Sleepサイクル

- **更新間隔**: 約1分（毎分0秒に更新）
- **動作時間**: 約6-8秒（センサー測定5秒 + 表示更新 + 初期化）
- **Deep Sleep時間**: 約52-54秒
- **WiFi接続**: 60回の起動ごとに1回（約1時間ごと）

### 省電力最適化

1. **センサー測定中のLight Sleep**: Single-Shot測定の5秒待機中にLight Sleepを使用
2. **WiFi接続の最小化**: NTP同期は1時間ごと、それ以外はRTC時刻を使用
3. **SDカード電源制御**: Deep Sleep中はSDカード電源をオフ
4. **EPD Deep Sleep**: ディスプレイをDeep Sleepモードに移行

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
- **I2Cピン**: SDA=GPIO 38, SCL=GPIO 20
- **測定モード**: Single-Shot（Light Sleep中に5秒待機）
- **温度オフセット**: 4.0°C（自己発熱補正）
- **測定範囲**:
  - CO2: 400-5000ppm
  - 温度: -10～+60°C
  - 湿度: 0-100%RH
- **精度**:
  - CO2: ±(40ppm+5%)
  - 温度: ±0.8°C (15-35°Cの範囲)
  - 湿度: ±6%RH (15-35°C、20-65%RHの範囲)

### ロガー機能

- **ログレベル**: DEBUG, INFO, WARN, ERROR
- **タイムスタンプ**: ブート時間、日時、または両方を表示
- **タグ**: Setup, Loop, Network, Sensor, Display, Font, DeepSleep, ImageBW
- **ANSIカラー**: ログレベルに応じた色分け表示

## 🐛 トラブルシューティング

### コンパイルエラー

- **エラー**: "Invalid FQBN"
  - **解決策**: FQBNの形式を確認。オプションは`:`で区切る（例: `esp32:esp32:esp32s3:PartitionScheme=huge_app,PSRAM=opi`）

- **エラー**: "SensirionI2cScd4x.h: No such file or directory"
  - **解決策**: 正しいヘッダー名は `SensirionI2CScd4x.h`（大文字小文字に注意）
  - ライブラリをインストール: `arduino-cli lib install "Sensirion I2C SCD4x@0.4.0"`

- **エラー**: "no matching function for call to 'SensirionI2CScd4x::begin'"
  - **解決策**: ライブラリバージョン0.4.0では `scd4x.begin(Wire)` を使用（I2Cアドレス引数は不要）

- **エラー**: "'class SensirionI2CScd4x' has no member named 'getDataReadyStatus'"
  - **解決策**: バージョン0.4.0では `getDataReadyFlag()` に変更されました

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
    - データ転送対応のUSBケーブルを使用（充電専用ケーブルでは動作しない）
    - macOS 10.13以前の場合はCH340ドライバをインストール（上記「USBシリアルドライバのインストール」参照）
    - 別のUSBポートを試す

### センサーが初期化できない場合

1. **接続を確認**:
   - SDAがGPIO 38に接続されているか
   - SCLがGPIO 20に接続されているか
   - VDDが3.3Vに接続されているか
   - GNDが接続されているか

2. **I2Cバスの確認**:
   - I2Cスキャナーを使用してセンサーが検出されるか確認
   - デフォルトI2Cアドレス: 0x62

3. **電源の確認**:
   - SCD41の電源電圧が3.3V±0.1Vか確認
   - Deep Sleep後にセンサーがリセットされていないか確認

詳細は [docs/README_SCD41.md](./docs/README_SCD41.md) の「トラブルシューティング」セクションを参照してください。

### 時刻が正しくない場合

1. **WiFi接続を確認**: NTP同期にはWiFi接続が必要
2. **RTC時刻の確認**: Deep Sleep後はRTC時刻から復元される
3. **タイムゾーン**: JST（UTC+9）が設定されている

### SDカードが認識されない場合

1. **SDカードのフォーマット**: FAT32でフォーマット
2. **電源ピン**: GPIO 42がHIGHになっているか確認
3. **SPIピン**: MOSI=40, MISO=13, SCK=39, CS=10

**注意**: SDカードが使用できない場合、SPIFFSにフォールバックしますが、書き込み寿命が限られます。

## 📝 ライセンス

このプロジェクトのライセンス情報は記載されていません。使用する際は、各ライブラリのライセンスを確認してください。

## 📧 連絡先

プロジェクトに関する質問や問題がある場合は、GitHubのIssuesで報告してください。
