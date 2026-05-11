# ChipperZero Build & Flash Guide

## 開発環境

選択肢は3つ。どれを使っても同じ .bin を焼ける。

| ツール | 用途 |
|--------|------|
| Arduino IDE 2.x | GUI、ライブラリ管理が楽 |
| arduino-cli | CI・スクリプト化・エディタ自由 |
| esptool.py | コンパイル済み .bin を直接書き込む場合のみ |

---

## ボードマネージャ設定

Arduino IDE → ファイル → 環境設定 → 追加のボードマネージャURL:

```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

ボードマネージャで `esp32 by Espressif Systems` をインストール。

---

## 必要ライブラリ

Arduino IDE → スケッチ → ライブラリをインクルード → ライブラリを管理

| ライブラリ名 | 作者 | 用途 |
|------------|------|------|
| U8g2 | olikraus | SH1106 OLED |
| RF24 | TMRh20 | NRF24L01 |
| NimBLE-Arduino | h2zero | ESP32 BLE |
| Adafruit PN532 | Adafruit | NFC (部品到着後) |
| IRremote | Armin Joachimsmeyer | IR (部品到着後) |

SD ライブラリは ESP32 コアに同梱。追加インストール不要。

---

## ボード設定

ツール メニューの設定:

| 項目 | 値 |
|------|---|
| Board | ESP32 Dev Module |
| Upload Speed | 921600 |
| CPU Frequency | 240MHz (WiFi/BT) |
| Flash Frequency | 80MHz |
| Flash Mode | QIO |
| Flash Size | 4MB (32Mb) |
| Partition Scheme | Default 4MB with spiffs |
| Core Debug Level | None |
| PSRAM | Disabled |
| Port | COMx (デバイスマネージャで確認) |

---

## 書き込み手順

### 通常時
1. USB接続
2. COMポートをツールメニューで選択
3. スケッチ → マイコンボードに書き込む (Ctrl+U)

### 書き込めない場合（boot loop / timeout）
ESP32-WROOM-32UE は自動リセット回路が必要。ない場合は手動で:

1. **BOOT** ボタンを押し続ける
2. **EN** (RST) ボタンを一瞬押して離す
3. Arduino IDE が "Connecting..." と表示されたら **BOOT** を離す
4. 書き込み開始

### COMポートが見えない場合
- CP2102 ドライバが必要: Silicon Labs CP210x USB to UART Bridge
- デバイスマネージャで「不明なデバイス」になっている場合はドライバを再インストール
- ドライバ再インストール後は **Windows を再起動**すること

---

## シリアルモニタ

| 項目 | 値 |
|------|---|
| ボーレート | 115200 |
| 改行コード | 両方 (CR+LF) |

起動時に以下が表示されれば正常:

```
ChipperZero v0.1 starting...
Display init OK
Encoder init OK
NRF24 init OK
Menu ready
```

---

## NRF24L01 書き込み時の注意

NRF24L01+PA+LNA は起動時に 3.3V ラインから大電流を引く。
キャパシタなしの場合、電圧降下で ESP32 がリセットされることがある。

対策:
- NRF24L01 の VCC-GND 間に **100µF 電解コンデンサ** を追加
- または書き込み時だけ NRF24L01 を外す

---

## ビルドのみ（書き込みなし）

スケッチ → 検証・コンパイル (Ctrl+R)

エラーが出た場合はシリアルコンソールでエラーを確認すること。
ライブラリが見つからないエラーは上記ライブラリ一覧を確認。

---

## パーティション・フラッシュ使用量の目安

| コンポーネント | サイズ概算 |
|--------------|---------|
| NRF spam ペイロード (484デバイス) | ~15KB |
| U8g2 フルバッファ | 1KB RAM |
| NimBLE スタック | ~50KB RAM |
| 全体スケッチ (stub段階) | < 500KB flash |

4MB フラッシュで十分に収まる。SPIFFS は将来の設定保存用に確保。

---

---

## arduino-cli

### インストール

```powershell
winget install ArduinoSA.CLI
# または https://arduino.github.io/arduino-cli/ からバイナリを取得
```

### 初期設定

```bash
arduino-cli config init
arduino-cli config add board_manager.additional_urls \
  https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32
```

### ライブラリインストール

```bash
arduino-cli lib install "U8g2"
arduino-cli lib install "RF24"
arduino-cli lib install "NimBLE-Arduino"
arduino-cli lib install "Adafruit PN532"
arduino-cli lib install "IRremote"
```

### ビルド

```bash
arduino-cli compile \
  --fqbn esp32:esp32:esp32 \
  --build-property "build.extra_flags=-DCORE_DEBUG_LEVEL=0" \
  ChipperZero/ChipperZero.ino
```

### 書き込み

```bash
arduino-cli upload \
  --fqbn esp32:esp32:esp32 \
  --port COM4 \
  ChipperZero/ChipperZero.ino
```

### ビルド + 書き込み 一発

```bash
arduino-cli compile --upload \
  --fqbn esp32:esp32:esp32 \
  --port COM4 \
  ChipperZero/ChipperZero.ino
```

### COMポート確認

```bash
arduino-cli board list
```

---

## esptool.py

コンパイル済み .bin ファイルを直接書き込む場合に使う。
arduino-cli や Arduino IDE でビルドした .bin の場所:
- Arduino IDE: `%TEMP%\arduino\sketches\<hash>\ChipperZero.ino.bin`
- arduino-cli: `build/esp32.esp32.esp32/ChipperZero.ino.bin`

### インストール

```bash
pip install esptool
```

### 書き込み

```bash
esptool.py --chip esp32 --port COM4 --baud 921600 \
  write_flash -z 0x1000 ChipperZero.ino.bin
```

### フルフラッシュ（bootloader + partitions + app）

arduino-cli の `--output-dir` で出力した場合:

```bash
esptool.py --chip esp32 --port COM4 --baud 921600 \
  write_flash \
  0x1000  build/ChipperZero.ino.bootloader.bin \
  0x8000  build/ChipperZero.ino.partitions.bin \
  0xe000  ~/.arduino15/packages/esp32/hardware/esp32/2.x.x/tools/partitions/boot_app0.bin \
  0x10000 build/ChipperZero.ino.bin
```

### 手動書き込みモード（自動リセット回路なしの場合）

```bash
# --before no_reset でesptoolが自動リセットしないようにする
esptool.py --chip esp32 --port COM4 --baud 921600 \
  --before no_reset --after no_reset \
  write_flash -z 0x10000 ChipperZero.ino.bin
# → "Connecting..." が出たら BOOT ボタンを押す
```

### フラッシュ消去（工場出荷状態に戻す）

```bash
esptool.py --chip esp32 --port COM4 erase_flash
```

---

## トラブルシューティング

| 症状 | 原因 | 対処 |
|------|------|------|
| COMポートが見えない | CP2102 ドライバ未インストール | ドライバ再インストール後 Windows 再起動 |
| Upload timeout | 自動リセット回路なし | BOOT+EN の手動操作で書き込みモードへ |
| TG0WDT_SYS_RESET | NRF24L01 起動時電流不足 | 100µF コンデンサ追加 |
| STATUS=0xFF | SPI 接続不良 / CSN ピン設定ミス | 配線確認・CSN=27 を確認 |
| OLEDが映らない | I2C アドレス不一致 / SH1106/SSD1306 ドライバ混在 | U8g2 の SH1106 コンストラクタを使用しているか確認 |
| スパムが BLE スキャナに映らない | チャンネル切替時の settling time 不足 | `delayMicroseconds(130)` がチャンネル切替前にあるか確認 |
