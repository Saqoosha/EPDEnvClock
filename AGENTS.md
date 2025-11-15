# Arduino CLI コンパイル・アップロード手順書

このドキュメントは、arduino-cliを使用してESP32-S3 Dev Moduleにスケッチをコンパイル・アップロードする手順を記録している。

## 概要

ESP32-S3 Dev ModuleとEPDディスプレイ（800x272ピクセル）を使用したプロジェクトのビルド・アップロード手順。

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

## コンパイル手順

### 1. プロジェクトディレクトリに移動

```bash
cd /path/to/Desktop/sketch_nov15a
```

### 2. コンパイル実行

ESP32-S3 Dev Module用にコンパイル：

```bash
arduino-cli compile --fqbn esp32:esp32:esp32s3:PartitionScheme=huge_app,PSRAM=opi sketch_nov15a.ino
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

## アップロード手順

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
arduino-cli upload -p /dev/cu.wchusbserial110 --fqbn esp32:esp32:esp32s3:PartitionScheme=huge_app,PSRAM=opi sketch_nov15a.ino
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

## ワンライナー（コンパイル + アップロード）

一度にコンパイルとアップロードを実行：

```bash
cd /path/to/Desktop/sketch_nov15a && \
arduino-cli compile --fqbn esp32:esp32:esp32s3:PartitionScheme=huge_app,PSRAM=opi sketch_nov15a.ino && \
arduino-cli upload -p /dev/cu.wchusbserial110 --fqbn esp32:esp32:esp32s3:PartitionScheme=huge_app,PSRAM=opi sketch_nov15a.ino
```

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

### コンパイルのみ（アップロードしない）

```bash
arduino-cli compile --fqbn esp32:esp32:esp32s3:PartitionScheme=huge_app,PSRAM=opi sketch_nov15a.ino
```

### アップロードのみ（コンパイル済みバイナリを使用）

```bash
arduino-cli upload -p /dev/cu.wchusbserial110 --fqbn esp32:esp32:esp32s3:PartitionScheme=huge_app,PSRAM=opi sketch_nov15a.ino
```

## プロジェクト構成

```
sketch_nov15a/
├── sketch_nov15a.ino          # メインスケッチ
├── EPD.h / EPD.cpp            # EPDライブラリ
├── EPD_Init.h / EPD_Init.cpp  # EPD初期化ライブラリ
├── spi.h / spi.cpp            # SPIライブラリ
├── EPDfont.h                  # フォントデータ
├── convert_image.py           # 画像変換スクリプト（別トピック）
└── AGENTS.md                  # このファイル
```

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
- コンパイル成功後、`.build/`ディレクトリにバイナリが生成される
- ポート名は環境によって異なるため、毎回`arduino-cli board list`で確認することを推奨
- アップロード前にボードが正しく接続されているか確認すること
- Arduino IDEを使用する場合は、[公式チュートリアル](https://www.elecrow.com/wiki/CrowPanel_ESP32_E-Paper_5.79inch_Arduino_Tutorial.html#upload-the-code)を参照
