# ChipperZero Tools

## ble_remote.html — Windows BLE 操作 GUI

ChipperZero 本体に物理ボタンが繋がっていなくても、PC から BLE 経由で操作するための単一ファイルのリモコンページ。

### 使い方 (Windows)

1. **Chrome** または **Edge** で `tools/ble_remote.html` をダブルクリックして開く
   (`file://` でも Web Bluetooth は動作)
2. ESP32 の電源を入れる。シリアルモニタに `BLE Remote advertising` が出ていればOK
3. ページの **接続** ボタン → ダイアログで `ChipperZero` を選ぶ
4. 画面のボタンまたはキーボードで操作:

   | キー | 動作 |
   |------|------|
   | ↑ | UP |
   | ↓ | DOWN |
   | Enter | OK |
   | Esc | BACK |
   | Space | EXTRA |

### トラブルシューティング

- **デバイス一覧に出ない**: Windows 設定 → Bluetooth で既にペアリング済みだと
  requestDevice 一覧から消えることがある。一度削除してから再試行。
- **接続できるがボタン無効**: シリアルで BLE 受信ログを確認。
- **Firefox は非対応**。Chrome / Edge のみ。

### プロトコル

完全な仕様は [`docs/ble_remote.md`](../docs/ble_remote.md) 参照。

- Service UUID: `c4b1e000-7a66-4c12-9c69-d2f8c4a3f100`
- Cmd Char (Write / Write w/o Response): `...e001-...`
- Status Char (Read / Notify): `...e002-...`
- Opcode: 1byte, `0x01=UP / 0x02=DOWN / 0x03=OK / 0x04=BACK / 0x05=EXTRA`

## 書き込みテスト手順 (現状: u.fl + NRF24 のみ接続)

OLED・エンコーダ・ボタン未接続でもブートする設計
(`display::begin()` 失敗時もそのまま続行)。

1. Arduino IDE 2.x で `ChipperZero.ino` を開く
2. ボード: **ESP32 Dev Module** / Flash 4MB / Partition: Default
3. ライブラリインストール: `U8g2`, `RF24`, `NimBLE-Arduino`, `Adafruit PN532`, `IRremote`
4. 書き込み後シリアルモニタ (115200) で以下が出ればOK:
   ```
   ChipperZero v0.1 starting...
   Display init FAILED      ← OLED 未接続なので想定どおり
   Encoder init OK
   BLE Remote advertising   ← ここまで出れば GUI から操作可能
   NRF24 init OK
   Menu ready
   ```
5. `tools/ble_remote.html` を Chrome/Edge で開いて接続テスト

### 注意

- ボタン未接続の GPIO34/35/36 は **入力専用 + 外部プルアップ必須**。
  未配線だと電気的に浮くが、内部読み出しは LOW/HIGH ランダムに揺れる程度で
  起動には影響しない (チャタリング判定に5msの待機が入っている)。
- `power::isCharging()` は GPIO13 の TP4056 STAT 入力。プルアップ無し+未接続だと
  HIGH/LOW 不定だが副作用は status bar 表示のみ。
