# EPDEnvClock

[English version here](README.md)

ESP32-S3ベースの電子ペーパー時計プロジェクト。SCD41 CO2/温度/湿度センサーを統合し、省電力設計で長時間動作します。

## 📋 概要

EPDEnvClockは、CrowPanel ESP32-S3 E-Paper 5.79インチディスプレイ（792x272ピクセル）を使用した時計アプリケーションです。以下の機能を提供します：

- **時刻・日付表示**: 大きな数字で時刻と日付を表示
- **環境センサー**: SCD41センサーによるCO2、温度、湿度の測定と表示
- **省電力設計**: Deep Sleepモードにより長時間動作（約1分間隔で更新）
- **Wi-Fi接続**: Wi-Fi経由でNTP時刻同期
- **バッテリー監視**: MAX17048燃料ゲージによるバッテリー残量と電圧をリアルタイム表示
- **ボタンウェイクアップ**: HOMEボタンでDeep Sleepから復帰して全画面更新

## ✨ 主な機能

### 表示機能

- **時刻表示**: 大きな数字フォントで時刻を表示（カーニング対応）
- **日付表示**: 中サイズ数字フォントで日付を表示（YYYY.MM.DD形式）
- **センサー値表示**: 温度、湿度、CO2濃度をアイコン付きで表示
- **ステータス表示**: バッテリー残量と電圧、Wi-Fi接続状態、NTP同期状態、稼働時間、空きメモリなどを表示

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
- **Wi-Fi省電力**: NTP同期は毎時0分に実行

### ネットワーク機能

- **Wi-Fi接続**: 設定されたWi-Fiに接続（SSID/パスワード変更には再コンパイルが必要）
- **NTP同期**: 毎時0分にNTPサーバーから時刻を同期（RTC時刻を保持）

### データロギング機能

- **センサーログ**: SDカードにJSONL形式でセンサー値を自動記録
- **記録項目**: 日付、時刻、Unixタイムスタンプ、RTCドリフト、温度、湿度、CO2、バッテリー電圧、バッテリー残量、充電率
- **ファイル形式**: `/sensor_logs/sensor_log_YYYYMMDD.jsonl`（日付ごとにファイルを分割）

### ボタン機能

- **HOMEボタン (GPIO 2)**: Deep Sleepから復帰し、全画面更新を実行
- **その他のボタン**: EXIT (GPIO 1)、PRV (GPIO 6)、NEXT (GPIO 4)、OK (GPIO 5) - 将来の拡張用
- すべてのボタンはアクティブLOW（内部プルアップ）

## 🔧 ハードウェア要件

### CrowPanelに内蔵されているコンポーネント

- **ESP32-S3 Dev Module**
- **EPDディスプレイ**: 792x272ピクセル（マスター/スレーブ2つのSSD1683 ICで制御）
- **SDカードスロット**: フレームバッファ保存用（オプション、SPIFFSより書き込み寿命が長い）

### 外部コンポーネント

- **SCD41センサー**: CO2/温度/湿度センサー
- **MAX17048燃料ゲージ**: バッテリー残量モニター（Adafruitブレイクアウトボード推奨）

### ピン構成

#### SCD41センサー（I2Cバス0）

| ピン | GPIO |
|-----|------|
| SDA | 38 |
| SCL | 20 |
| VDD | 3.3V |
| GND | GND |

**注意**: プルアップ抵抗はSCD41モジュールに内蔵されているため、追加のハードウェアは不要です。

#### MAX17048燃料ゲージ（I2Cバス1）

| ピン | GPIO / 接続先 |
|-----|---------------|
| SDA | 14 |
| SCL | 16 |
| VIN | 3.3V |
| GND | GND |
| CELL+ | LiPoバッテリー + |
| CELL- | LiPoバッテリー - (GND) |

**注意**: MAX17048はバッテリーから電源を得ます（VINではない）。バッテリーが接続されていないとI2Cに応答しません。

#### SDカード（HSPIバス）

| ピン | GPIO |
|-----|------|
| MOSI | 40 |
| MISO | 13 |
| SCK | 39 |
| CS | 10 |
| 電源イネーブル | 42 |

#### EPDディスプレイ（ビットバンギングSPI）

| ピン | GPIO |
|-----|------|
| MOSI | 11 |
| SCK | 12 |
| CS | 45 |
| DC | 46 |
| RST | 47 |
| BUSY | 48 |

#### ボタン（アクティブLOW）

| ボタン | GPIO |
|--------|------|
| HOME | 2 |
| EXIT | 1 |
| PRV | 6 |
| NEXT | 4 |
| OK | 5 |

## 🚀 セットアップ

### 1. 必要なソフトウェア

#### USBシリアルドライバのインストール

CrowPanel ESP32-S3は **CH340** USBシリアルチップを使用しています。

- **macOS**: 内蔵ドライバはシリアルコンソールのみ対応。ファームウェアアップロードには公式ドライバが必要。
- **Windows/Linux**: ドライバインストールガイドを参照。

全プラットフォーム共通: [SparkFun CH340ドライバガイド](https://learn.sparkfun.com/tutorials/how-to-install-ch340-drivers)

デバイスを接続すると `/dev/cu.usbserial-*` または `/dev/cu.wchusbserial*` (macOS) として認識されます。

#### arduino-cliのインストール

**macOS**:

```bash
brew install arduino-cli
```

その他のプラットフォーム: [arduino-cliインストールガイド](https://arduino.github.io/arduino-cli/latest/installation/)

#### ESP32ボードサポートのインストール

```bash
arduino-cli core update-index
arduino-cli core install esp32:esp32@2.0.12
```

**注意**: ESP32コアのバージョンは`2.0.12`（v2.x系の最新）を使用してください。v3.x系はSPIなどのAPIに破壊的変更があり、Adafruitライブラリ（BusIO、MAX1704Xなど）と互換性がありません。

#### ライブラリのインストール

```bash
# Sensirion SCD4xライブラリ（依存するSensirion Coreも自動でインストールされます）
arduino-cli lib install "Sensirion I2C SCD4x@0.4.0"

# Adafruit MAX17048燃料ゲージライブラリ
arduino-cli lib install "Adafruit MAX1704X"
```

### 開発環境のバージョン情報

| コンポーネント | バージョン | 備考 |
|---------------|-----------|------|
| arduino-cli | 最新版推奨 | `brew install arduino-cli` (macOS) |
| ESP32 Core | 2.0.12 | `esp32:esp32@2.0.12`（v3.xはAdafruit非互換）|
| Sensirion I2C SCD4x | 0.4.0 | CO2/温度/湿度センサーライブラリ |
| Sensirion Core | 0.7.2 | 依存ライブラリ（自動インストール） |
| Adafruit MAX1704X | 1.0.3 | バッテリー燃料ゲージライブラリ |

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

### 3. ImageBW Export設定（デバッグ機能）

`EPDEnvClock/server_config.h`でサーバーのIPアドレスとポートを設定：

```cpp
#define ENABLE_IMAGEBW_EXPORT 1       // 1で有効、0で無効
#define IMAGEBW_SERVER_IP "192.168.1.100"  // サーバーのIPアドレス
#define IMAGEBW_SERVER_PORT 8080           // サーバーポート
```

## 📦 ビルド・アップロード

### 推奨方法（コンパイル + アップロード）

```bash
cd /path/to/EPDEnvClock
arduino-cli compile --fqbn esp32:esp32:esp32s3:PartitionScheme=huge_app,PSRAM=opi --upload -p /dev/cu.wchusbserial110 EPDEnvClock
```

**注意**: `/path/to/EPDEnvClock`を実際のプロジェクトディレクトリのパスに置き換えてください。ポート名（`/dev/cu.wchusbserial110`）も環境に応じて変更してください。

**重要**:

- 常に`compile --upload`を一緒に使用してください（uploadだけでは再コンパイルが保証されません）
- `arduino-cli board list`でポートを確認してください - ポート名は変わることがあります

**設定パラメータ**:

- **FQBN**: `esp32:esp32:esp32s3`
- **PartitionScheme**: `huge_app` (Huge APP: 3MB No OTA/1MB SPIFFS)
- **PSRAM**: `opi` (OPI PSRAM)

### ポートの確認

```bash
arduino-cli board list
```

## 💻 使用方法

### 基本的な動作

1. **起動**: ESP32-S3に電源を供給すると、自動的に起動します
2. **初期化**: センサーとWi-Fiの初期化が行われます（初回起動時）
3. **表示更新**: 約1分ごとに表示が更新されます
4. **Deep Sleep**: 表示更新後、Deep Sleepモードに入ります
5. **ボタン復帰**: HOMEボタンを押すとDeep Sleepから復帰して全画面更新

### 表示内容

画面レイアウト（792x272ピクセル）：

- **上部（y=4）**: ステータス情報（バッテリー残量と電圧、Wi-Fi接続状態、NTP同期状態、稼働時間、空きメモリ）
- **左側上部（y=45）**: 日付（YYYY.MM.DD形式、中サイズ数字）
- **左側中央（y=123）**: 時刻（H:MM または HH:MM形式、大きな数字）
- **右側上部（y=33）**: 温度（アイコン + 値 + °C単位）
- **右側中央（y=114）**: 湿度（アイコン + 値 + %単位）
- **右側下部（y=193）**: CO2濃度（アイコン + 値 + ppm単位）

### ImageBW Export機能（デバッグ）

表示データをWi-Fi経由でサーバーに送信する場合：

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
│   ├── EPDEnvClock.ino          # メインスケッチ（setup/loop）
│   ├── EPD.h / EPD.cpp          # 低レベルEPDドライバ
│   ├── EPD_Init.h / EPD_Init.cpp  # EPD初期化
│   ├── spi.h / spi.cpp          # EPD用ビットバンギングSPI
│   ├── display_manager.*        # 表示レンダリング、レイアウト
│   ├── fuel_gauge_manager.*     # MAX17048バッテリー燃料ゲージ
│   ├── font_renderer.*          # カーニング対応グリフ描画
│   ├── sensor_manager.*         # SCD41センサー（Light Sleep付きSingle-Shotモード）
│   ├── sensor_logger.*          # SDカードへのセンサーデータ記録
│   ├── network_manager.*        # Wi-Fi接続、NTP同期
│   ├── deep_sleep_manager.*     # Deep Sleep、RTC状態、SD/SPIFFSフレームバッファ
│   ├── imagebw_export.*         # ImageBW Export（デバッグ）
│   ├── logger.*                 # ログ機能（DEBUG/INFO/WARN/ERRORレベル対応）
│   ├── wifi_config.h            # Wi-Fi認証情報（gitignored）
│   ├── secrets.h                # APIキー（gitignored）
│   ├── server_config.h          # サーバー設定
│   └── bitmaps/                 # 数字フォント、アイコン、単位、カーニングテーブル
├── scripts/                     # Pythonスクリプト
│   ├── create_number_bitmaps.py # TTFフォントから数字ビットマップを生成
│   ├── convert_numbers.py       # PNG数字をCヘッダーに変換
│   ├── convert_icon.py          # PNGアイコンをCヘッダーに変換
│   ├── imagebw_server.py        # ImageBW受信サーバー（デバッグ）
│   └── upload_sensor_data.py    # JSONLログをダッシュボードAPIにアップロード
├── assets/                      # アセット（画像ファイルなど）
│   ├── Number L/                # 大きい数字フォント画像
│   └── Number M/                # 中サイズ数字フォント画像
├── web/                         # Webダッシュボード（Astro + Cloudflare Pages）
├── docs/                        # ドキュメント
│   ├── README.md                # ドキュメントインデックス
│   ├── README_IMAGEBW.md        # ImageBW機能ガイド
│   ├── README_SCD41.md          # SCD41センサーガイド
│   └── reviews/                 # コードレビュー
├── output/                      # 生成された画像出力（gitignore）
└── README.md                    # このファイル
```

## 📚 ドキュメント

- **[docs/README.md](./docs/README.md)** - ドキュメントインデックス
- **[docs/README_IMAGEBW.md](./docs/README_IMAGEBW.md)** - ImageBW Wi-Fi Export機能の使い方
- **[docs/README_SCD41.md](./docs/README_SCD41.md)** - SCD41センサー統合ガイド
- **[web/README.md](./web/README.md)** - Webダッシュボードのドキュメント

## 🌐 Webダッシュボード

このプロジェクトにはセンサーデータを表示するWebダッシュボードが含まれており、Cloudflare Pagesにデプロイされています。

### ローカル開発

```bash
cd web
bun install
bun run dev
```

<http://localhost:4321/> でアクセス

### デプロイ

```bash
cd web
bun run build
bunx wrangler pages deploy dist --branch=main
```

**注意**: `--branch=main`は本番ドメインへのデプロイに必要です。これがないと、プレビューURLにのみデプロイされます。

## 🔋 省電力設計

### 消費電流

| 状態 | 消費電流 |
|------|----------|
| SCD41 Idle Single-Shot | 約1.5mA |
| MAX17048 Hibernate | 約3µA |
| ESP32-S3 Deep Sleep | 約0.2〜0.3mA |
| ESP32-S3 Light Sleep (センサー測定待機中) | 約2〜3mA |
| ESP32-S3 Active (Wi-Fi含む) | 約80〜150mA |

### バッテリー持続時間（1480mAhバッテリーの場合）

- **平均消費電流**: 約2.5mA（Wi-Fi同期は1時間に1回）
- **持続時間**: 1480mAh ÷ 2.5mA ≈ **592時間（約25日）**

### Deep Sleepサイクル

- **更新間隔**: 約1分（毎分0秒に更新）
- **動作時間**: 約6-8秒（センサー測定5秒 + 表示更新 + 初期化）
- **Deep Sleep時間**: 約52-54秒
- **Wi-Fi接続**: 毎時0分にNTP同期

### 省電力最適化

1. **センサー測定中のLight Sleep**: Single-Shot測定の5秒待機中にLight Sleepを使用
2. **Wi-Fi接続の最小化**: NTP同期は1時間ごと、それ以外はRTC時刻を使用
3. **SDカード電源制御**: Deep Sleep中はSDカード電源をオフ（GPIO 42 LOW）
4. **EPD Deep Sleep**: ディスプレイをDeep Sleepモードに移行
5. **I2Cピンをハイに保持**: Deep Sleep中にセンサーをアイドルモードに保持

## 🎨 フォント生成

数字フォントは`scripts/create_number_bitmaps.py`を使用して生成します。

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

## ⚙️ 技術仕様

### ESP32-S3設定

- **Board**: ESP32S3 Dev Module
- **Partition Scheme**: Huge APP (3MB No OTA/1MB SPIFFS)
- **PSRAM**: OPI PSRAM
- **CPU Frequency**: 240MHz (Wi-Fi)
- **Flash Mode**: QIO 80MHz
- **Flash Size**: 4MB (32Mb)
- **Upload Speed**: 921600

### EPDディスプレイ仕様

- **実際の解像度**: 792x272ピクセル
- **コントローラー**: マスター/スレーブの2つのSSD1683 IC
  - 各コントローラー: 396x272ピクセルを担当
  - 中央に8pxのアドレスオフセット（ソフトウェアで処理）
- **プログラム定義**: `EPD_W = 800`, `EPD_H = 272`（アドレスオフセット用）
- **バッファサイズ**: 27,200バイト (800×272ピクセル ÷ 8ビット)
- **インターフェース**: ビットバンギングSPI (MOSI=11, SCK=12, CS=45, DC=46, RST=47, BUSY=48)

### SCD41センサー仕様

- **I2Cアドレス**: 0x62 (デフォルト)
- **I2Cバス**: Wire (バス0) - SDA=GPIO 38, SCL=GPIO 20
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

### MAX17048燃料ゲージ仕様

- **I2Cアドレス**: 0x36 (デフォルト)
- **I2Cバス**: Wire1 (バス1) - SDA=GPIO 14, SCL=GPIO 16
- **電源**: バッテリーから給電（動作にはバッテリー接続が必要）
- **スリープモード**: Hibernateモード（消費電流約3µA）
- **測定項目**:
  - バッテリー電圧: 0-5V
  - 充電状態: 0-100%
  - 充電率: %/hr（正=充電中、負=放電中）
- **アルゴリズム**: ModelGauge™による電流センサー不要の正確なSOC測定

### 時刻管理

- **NTPサーバー**: `ntp.nict.jp`
- **タイムゾーン**: JST (UTC+9)
- **同期間隔**: 毎時0分
- **RTC保持**: スリープ前にRTCメモリに時刻を保存、起床時に復元

### ロガー機能

- **ログレベル**: DEBUG, INFO, WARN, ERROR
- **タイムスタンプ**: ブート時間、日時、または両方を表示
- **タグ**: Setup, Loop, Network, Sensor, Display, Font, DeepSleep, ImageBW
- **ANSIカラー**: ログレベルに応じた色分け表示

## 📝 ライセンス

このプロジェクトはMITライセンスの下で公開されています。詳細は[LICENSE](LICENSE)ファイルを参照してください。

## 📧 連絡先

プロジェクトに関する質問や問題がある場合は、GitHubのIssuesで報告してください。
