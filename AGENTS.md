# Arduino CLI コンパイル・アップロード手順書

このドキュメントは、arduino-cliを使用してESP32-S3 Dev Moduleにスケッチをコンパイル・アップロードする手順を記録している。

## 概要

ESP32-S3 Dev ModuleとEPDディスプレイ（792x272ピクセル）を使用したプロジェクトのビルド・アップロード手順。

**注意**: EPDディスプレイは実際には792x272ピクセルですが、マスター/スレーブの2つのコントローラー（各396px）で制御されており、中央に4pxのギャップがあります。そのため、プログラムでは`EPD_W`を800として定義していますが、実際の表示領域は792x272です。

## 必要な環境

- arduino-cli（Homebrewでインストール可能）
- ESP32-S3 Dev Module
- USBケーブル

## arduino-cliのインストール

```bash
brew install arduino-cli
```

## ボード設定の確認

使用するボードの詳細を確認：

```bash
arduino-cli board details -b esp32:esp32:esp32s3
```

## コンパイル・アップロード手順

### arduino-cliの動作について

`arduino-cli upload`コマンドの動作：

- **公式ヘルプ**: "This does NOT compile the sketch prior to upload"と明記されている
- **実際の動作**: ビルドディレクトリ（`.build/`）に既存のバイナリがある場合、それを使用してアップロードする
- **バイナリがない場合**: バージョンによっては自動的にコンパイルを実行する場合があるが、**保証されていない**

**推奨方法**:

1. **確実な方法**: `compile --upload`を使用（コンパイルとアップロードを一度に実行）
2. **分離したい場合**: `compile`を実行してから`upload`を実行
3. **`upload`のみ**: 既存のバイナリがある場合のみ使用（コード変更後は再コンパイルが必要）

## コンパイル手順（オプション）

### 1. プロジェクトディレクトリに移動

```bash
cd /path/to/Desktop/EPDClock
```

### 2. コンパイル実行

ESP32-S3 Dev Module用にコンパイル：

```bash
arduino-cli compile --fqbn esp32:esp32:esp32s3:PartitionScheme=huge_app,PSRAM=opi EPDClock
```

**重要な設定パラメータ：**

- **FQBN**: `esp32:esp32:esp32s3`
- **PartitionScheme**: `huge_app` (Huge APP: 3MB No OTA/1MB SPIFFS)
- **PSRAM**: `opi` (OPI PSRAM)

### 3. コンパイル結果の確認

成功すると以下のような出力が表示される：

```
Sketch uses 279665 bytes (8%) of program storage space. Maximum is 3145728 bytes.
Global variables use 46916 bytes (14%) of dynamic memory, leaving 280764 bytes for local variables. Maximum is 327680 bytes.
```

## アップロード手順（推奨）

通常はこの手順だけでコンパイルとアップロードが完了します。

### 1. USBシリアルポートの確認

接続されているボードを確認：

```bash
arduino-cli board list
```

出力例：

```
Port                            Protocol Type              Board Name FQBN Core
/dev/cu.wchusbserial110         serial   Serial Port (USB) Unknown
/dev/cu.usbserial-110           serial   Serial Port (USB) Unknown
```

### 2. アップロード実行

適切なポートを指定してアップロード：

```bash
arduino-cli upload -p /dev/cu.wchusbserial110 --fqbn esp32:esp32:esp32s3:PartitionScheme=huge_app,PSRAM=opi EPDClock
```

**注意**: ポート名は環境によって異なる可能性がある。`arduino-cli board list`で確認すること。

### 3. アップロード成功の確認

成功すると以下のような出力が表示される：

```
Writing at 0x00010000... (100 %)
Wrote 280032 bytes (145860 compressed) at 0x00010000 in 2.4 seconds...
Hash of data verified.
Hard resetting via RTS pin...
```

## ワンライナー（推奨方法）

### 方法1: compile --upload（推奨）

コンパイルとアップロードを一度に実行（最も確実）：

```bash
cd /path/to/Desktop/EPDClock && \
arduino-cli compile --fqbn esp32:esp32:esp32s3:PartitionScheme=huge_app,PSRAM=opi --upload -p /dev/cu.wchusbserial110 EPDClock
```

### 方法2: 分離実行

コンパイルとアップロードを分けて実行：

```bash
cd /path/to/Desktop/EPDClock && \
arduino-cli compile --fqbn esp32:esp32:esp32s3:PartitionScheme=huge_app,PSRAM=opi EPDClock && \
arduino-cli upload -p /dev/cu.wchusbserial110 --fqbn esp32:esp32:esp32s3:PartitionScheme=huge_app,PSRAM=opi EPDClock
```

### 方法3: uploadのみ（既存バイナリがある場合）

**注意**: コードを変更した場合は、必ず`compile`を実行してから`upload`してください。`upload`コマンドは自動的にコンパイルを実行することを保証していません。

## トラブルシューティング

### コンパイルエラー

- **エラー**: "Invalid FQBN"
  - **解決策**: FQBNの形式を確認。オプションは`:`で区切る（例: `esp32:esp32:esp32s3:PartitionScheme=huge_app,PSRAM=opi`）

- **エラー**: "expected '}' before numeric constant"
  - **解決策**: ヘッダーファイルの配列フォーマットを確認

### アップロードエラー

- **エラー**: "Unable to verify flash chip connection"
  - **解決策**:
    - 別のUSBポートを試す（`arduino-cli board list`で確認）
    - ボードのリセットボタンを押す
    - USBケーブルを確認（データ転送対応のケーブルか確認）

- **エラー**: "This chip is ESP32-S3 not ESP32"
  - **解決策**: FQBNを`esp32:esp32:esp32s3`に変更

- **エラー**: ポートが見つからない
  - **解決策**:
    - USBケーブルを接続し直す
    - `arduino-cli board list`でポートを再確認
    - デバイスドライバが正しくインストールされているか確認

## よく使うコマンド

### ボード情報の確認

```bash
# 接続されているボード一覧
arduino-cli board list

# 特定のボードの詳細情報
arduino-cli board details -b esp32:esp32:esp32s3

# インストールされているコア一覧
arduino-cli core list
```

### コンパイル + アップロード（推奨）

```bash
arduino-cli compile --fqbn esp32:esp32:esp32s3:PartitionScheme=huge_app,PSRAM=opi --upload -p /dev/cu.wchusbserial110 EPDClock
```

### コンパイルのみ

```bash
arduino-cli compile --fqbn esp32:esp32:esp32s3:PartitionScheme=huge_app,PSRAM=opi EPDClock
```

### アップロードのみ（既存バイナリを使用）

**注意**: コード変更後は必ず`compile`を実行してから使用してください。

```bash
arduino-cli upload -p /dev/cu.wchusbserial110 --fqbn esp32:esp32:esp32s3:PartitionScheme=huge_app,PSRAM=opi EPDClock
```

## EPDディスプレイの解像度について

### 実際の表示領域

- **実際の解像度**: 792x272ピクセル
- **コントローラー構成**: マスター/スレーブの2つのSSD1683 IC
  - 各コントローラー: 396x272ピクセルを担当
  - 中央に4pxのギャップ（コントローラー間の接続部分）

### プログラムでの定義

- **EPD_W**: 800ピクセル（アドレスオフセット用）
- **EPD_H**: 272ピクセル
- **バッファサイズ**: 800x272 = 27,200バイト

### 画像表示関数の違い

- **EPD_Display()**: 800x272のビットマップデータが必要（全画面表示用）
- **EPD_ShowPicture()**: 792x272のビットマップデータが必要（実際の表示領域）

詳細は`EPD_Init.h`のコメントを参照。

## ImageBW WiFi Export サーバー

### サーバーの起動

Python HTTPサーバーを起動して、ArduinoからImageBWデータを受信します：

```bash
cd /path/to/Desktop/EPDClock
python3 scripts/imagebw_server.py --port 8080
```

**オプション**:
- `--port`: サーバーポート（デフォルト: 8080）
- `--host`: サーバーホスト（デフォルト: 0.0.0.0）

**バックグラウンドで起動**:
```bash
python3 scripts/imagebw_server.py --port 8080 &
```

### サーバーの停止

実行中のサーバーを停止：

```bash
pkill -f "imagebw_server.py"
```

または、プロセスIDを確認して停止：

```bash
ps aux | grep "imagebw_server.py" | grep -v grep
kill <PID>
```

### サーバー状態の確認

サーバーが動作しているか確認：

```bash
curl http://localhost:8080/status
```

### エンドポイント

- `POST /imagebw` - バイナリImageBWデータ（27,200バイト）を受信
- `POST /imagebw/base64` - Base64エンコードされたImageBWデータを受信
- `GET /status` - サーバー状態を確認

### 設定

サーバーのIPアドレスとポートは`EPDClock/server_config.h`で設定：

```c
#define ENABLE_IMAGEBW_EXPORT 1  // 1で有効、0で無効
#define SERVER_IP "192.168.11.9"  // MacのIPアドレス
#define SERVER_PORT 8080           // サーバーポート
```

### 出力

受信したImageBWデータは`output/`ディレクトリにPNGファイルとして保存されます：
- ファイル名形式: `imagebw_YYYYMMDD_HHMMSS.png`
- 画像サイズ: 792x272ピクセル（1ビット白黒）

## プロジェクト構成

```
EPDClock/
├── EPDClock/                  # Arduino/Firmwareコード（スケッチディレクトリ）
│   ├── EPDClock.ino          # メインスケッチ
│   ├── EPD.h / EPD.cpp       # EPDライブラリ
│   ├── EPD_Init.h / EPD_Init.cpp  # EPD初期化ライブラリ
│   ├── spi.h / spi.cpp       # SPIライブラリ
│   ├── EPDfont.h             # フォントデータ
│   ├── wifi_config.h          # Wi-Fi設定（gitignore）
│   ├── server_config.h        # サーバー設定
│   └── bitmaps/               # ビットマップヘッダーファイル
│       ├── Number_S_bitmap.h  # 数字フォント（小）
│       ├── Number_L_bitmap.h  # 数字フォント（大）
│       └── ...
├── scripts/                   # Pythonスクリプト
│   ├── convert_image.py       # 画像変換スクリプト
│   ├── convert_imagebw.py     # ImageBW変換スクリプト
│   ├── convert_numbers.py     # 数字画像変換スクリプト
│   ├── create_number_bitmaps.py  # 数字ビットマップ生成スクリプト
│   └── imagebw_server.py      # ImageBW受信サーバー
├── assets/                    # アセット（画像ファイルなど）
│   ├── Number L/              # 大きい数字フォント画像
│   └── Number S/              # 小さい数字フォント画像
├── output/                    # 生成された画像出力（gitignore）
├── docs/                      # ドキュメント
│   └── README_IMAGEBW.md      # ImageBW関連ドキュメント
├── AGENTS.md                  # このファイル（Arduino CLI手順書）
└── README.md                  # プロジェクトREADME
```

**注意**: arduino-cliを使用する場合、スケッチディレクトリ`EPDClock`を指定してコンパイル・アップロードを実行します。スケッチディレクトリ名と`.ino`ファイル名（`EPDClock.ino`）が一致している必要があります。

## 参考情報

### 公式チュートリアル

Elecrow公式のCrowPanel ESP32 E-Paper 5.79インチ Arduinoチュートリアル：

- **URL**: <https://www.elecrow.com/wiki/CrowPanel_ESP32_E-Paper_5.79inch_Arduino_Tutorial.html#upload-the-code>
- このチュートリアルには、Arduino IDEでのボード設定やアップロード手順が詳しく記載されている

### ESP32-S3 Dev Module 設定

公式チュートリアルに基づく推奨設定：

- **Board**: ESP32S3 Dev Module
- **Partition Scheme**: Huge APP (3MB No OTA/1MB SPIFFS)
- **PSRAM**: OPI PSRAM
- **CPU Frequency**: 240MHz (WiFi)
- **Flash Mode**: QIO 80MHz
- **Flash Size**: 4MB (32Mb)
- **Upload Speed**: 921600

**Arduino IDEでの設定手順**（公式チュートリアル参照）：

1. Tools → Board → esp32 → ESP32S3 Dev Module を選択
2. Partition Scheme → "Huge APP (3MB No OTA/1MB SPIFFS)" を選択
3. PSRAM → "OPI PSRAM" を選択

### FQBN形式

```
<パッケージ>:<アーキテクチャ>:<ボード>:<オプション1>=<値1>,<オプション2>=<値2>
```

例：

```
esp32:esp32:esp32s3:PartitionScheme=huge_app,PSRAM=opi
```

## メモ

- コンパイルとアップロードは別々に実行できる
- コンパイル成功後、`EPDClock/.build/`ディレクトリにバイナリが生成される
- ポート名は環境によって異なるため、毎回`arduino-cli board list`で確認することを推奨
- アップロード前にボードが正しく接続されているか確認すること
- **重要**: arduino-cliコマンドはプロジェクトルート（`EPDClock/`）から実行し、スケッチディレクトリ`EPDClock`を指定する。スケッチディレクトリ名と`.ino`ファイル名（`EPDClock.ino`）が一致している必要がある
- Arduino IDEを使用する場合は、`EPDClock/`ディレクトリをスケッチフォルダとして開くか、[公式チュートリアル](https://www.elecrow.com/wiki/CrowPanel_ESP32_E-Paper_5.79inch_Arduino_Tutorial.html#upload-the-code)を参照
