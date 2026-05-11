# ChipperZero BLE Remote Control Protocol

ChipperZero exposes a small custom GATT service so a phone app (Android/iOS/Web Bluetooth)
can drive the menu remotely. Input received over BLE is injected into the same event
queue that the rotary encoder feeds, so any UI state reachable with the physical
controls is reachable over BLE as well.

The service uses the ESP32's built-in BLE radio via NimBLE-Arduino. It is independent
of the NRF24L01 — running the NRF Spam module does not interfere with the remote.

## Advertising

- Local name: `ChipperZero`
- Advertised service UUID: `c4b1e000-7a66-4c12-9c69-d2f8c4a3f100`
- Auto-starts on boot. Toggle from `System > BLE Remote` in the menu.

## GATT Service

| Item | UUID | Properties |
|---|---|---|
| Service | `c4b1e000-7a66-4c12-9c69-d2f8c4a3f100` | — |
| Command characteristic | `c4b1e001-7a66-4c12-9c69-d2f8c4a3f100` | `WRITE`, `WRITE_NO_RESPONSE` |
| Status characteristic | `c4b1e002-7a66-4c12-9c69-d2f8c4a3f100` | `READ`, `NOTIFY` |

## Command opcodes

Write a single byte to the command characteristic:

| Byte | Event | Meaning |
|---|---|---|
| `0x01` | `UP`    | Move selection up / encoder CCW |
| `0x02` | `DOWN`  | Move selection down / encoder CW |
| `0x03` | `OK`    | Activate selected item |
| `0x04` | `BACK`  | Pop submenu / stop active module |
| `0x05` | `EXTRA` | Reserved (module-specific) |

Any other byte value is silently ignored. Multi-byte writes are accepted but only
the first byte is interpreted.

## Web Bluetooth example

```js
const SVC  = 'c4b1e000-7a66-4c12-9c69-d2f8c4a3f100';
const CMD  = 'c4b1e001-7a66-4c12-9c69-d2f8c4a3f100';

const dev = await navigator.bluetooth.requestDevice({
  filters: [{ services: [SVC] }],
});
const server = await dev.gatt.connect();
const svc    = await server.getPrimaryService(SVC);
const cmd    = await svc.getCharacteristic(CMD);

const send = (op) => cmd.writeValueWithoutResponse(new Uint8Array([op]));
await send(0x02);  // DOWN
await send(0x03);  // OK
```

## Android (Kotlin) example

```kotlin
val cmdChar = service.getCharacteristic(UUID.fromString("c4b1e001-7a66-4c12-9c69-d2f8c4a3f100"))
cmdChar.value = byteArrayOf(0x03.toByte())     // OK
cmdChar.writeType = BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
gatt.writeCharacteristic(cmdChar)
```

## nRF Connect quick test

1. Scan → connect to `ChipperZero`.
2. Open service `c4b1e0…f100`.
3. Write `01` (hex) to characteristic `c4b1e001…f100` → menu cursor moves up.

## Notes

- Pairing/bonding is not required (open GATT). Add bonding later if you want to lock
  the remote to a single phone.
- Only one central can be connected at a time (NimBLE default).
- Disconnections automatically restart advertising while BLE Remote is enabled.
