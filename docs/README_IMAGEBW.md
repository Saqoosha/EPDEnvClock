# ImageBW Wi-Fi Export 使い方

## 概要
ArduinoからImageBWキャンバス配列をWi-Fi経由でMac上のPythonサーバーに送信し、PNG画像として保存する機能です。

## セットアップ

### 1. サーバー設定の確認
`server_config.h`でサーバーのIPアドレスとポートを設定：
```c
#define SERVER_IP "192.168.3.1"  // MacのIPアドレス
#define SERVER_PORT 8080           // サーバーポート
```

### 2. Pythonサーバーの起動
```bash
python3 imagebw_server.py --port 8080
```

サーバーは以下のエンドポイントを提供します：
- `POST /imagebw` - バイナリImageBWデータ（27,200バイト）を受信
- `POST /imagebw/base64` - Base64エンコードされたImageBWデータを受信
- `GET /status` - サーバー状態を確認

### 3. Arduinoのアップロード
1. `server_config.h`をMacのIPアドレスに合わせて更新
2. Arduino IDEまたはarduino-cliで`EPDEnvClock.ino`をアップロード

## 動作

### 自動送信
- 表示が更新されるたびに自動的にImageBWを送信
- `IMAGEBW_EXPORT_INTERVAL`で定期的な送信も可能（デフォルト: 60秒）

### 設定オプション
`EPDEnvClock.ino`内で以下の設定が可能：
```cpp
bool enableImageBWExport = true;  // falseで無効化
const unsigned long IMAGEBW_EXPORT_INTERVAL = 60000; // 0で自動送信を無効化
```

## 出力

受信したImageBWデータは`output/`ディレクトリにPNGファイルとして保存されます：
- ファイル名形式: `imagebw_YYYYMMDD_HHMMSS.png`
- 画像サイズ: 800x272ピクセル（1ビット白黒）

## トラブルシューティング

### サーバーに接続できない
- MacのIPアドレスが正しいか確認: `ifconfig | grep "inet "`
- ファイアウォールでポート8080が開いているか確認
- ArduinoとMacが同じWi-Fiネットワークに接続されているか確認

### データが受信されない
- Arduinoのシリアルモニターで`[ImageBW]`ログを確認
- Wi-Fi接続状態を確認
- `enableImageBWExport`が`true`になっているか確認

### 画像が正しく表示されない
- ImageBWデータサイズが27,200バイトであることを確認
- `convert_imagebw.py`を直接実行してテスト可能

## テスト

### サーバー状態の確認
```bash
curl http://localhost:8080/status
```

### 手動で画像変換をテスト
```bash
# バイナリファイルがある場合
python3 convert_imagebw.py <input_file.bin> output.png
```
