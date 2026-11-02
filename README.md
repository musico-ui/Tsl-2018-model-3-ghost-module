# Tesla Ghost Monitor v0.1 — Waveshare ESP32-S3 RS485/CAN

A small custom firmware baseline for a Waveshare ESP32-S3 RS485/CAN board connected to a 2018 Tesla Model 3 Vehicle/Chassis CAN tap.

## What this build does

- 500 kbit/s CAN receive using ESP32 TWAI
- Hardware **listen-only** mode
- Live Wi-Fi access point: `Tesla-Ghost`
- Password: `ghost2018`
- Dashboard: `http://192.168.4.1`
- Live vehicle speed from CAN ID `0x257`
- Live `0x334 UI_powertrainControl` decoder
- Pedal-map display:
  - 0 = Chill
  - 1 = Standard
  - 2 = Performance
- Rolling counter display from byte 6 upper nibble
- `0x334` additive checksum verification
- Browser-based OTA update of future `.bin` files

This build deliberately does **not transmit CAN frames**. It is meant to establish a clean, reproducible custom firmware baseline on the same Waveshare hardware that is already capturing the car successfully.

## Hardware target

Waveshare ESP32-S3 RS485/CAN

- CAN TX: GPIO 15
- CAN RX: GPIO 16
- CAN bitrate: 500000
- Flash: 8 MB

The GPIO target matches the `waveshare-s3-can` environment in the current `hypery11/flipper-tesla-fsd` ESP32 port.

## Build with PlatformIO

Install VS Code + PlatformIO, open this folder, then:

```bash
pio run -e waveshare-s3-can
```

The OTA image will be:

```text
.pio/build/waveshare-s3-can/firmware.bin
```

## First install using the FSD firmware already on your ESP32

Because your current FSD firmware already supports browser OTA:

1. Connect your phone/laptop to the ESP32's current Wi-Fi network.
2. Open its dashboard.
3. Open the firmware update card.
4. Upload this project's `firmware.bin`.
5. Wait for reboot.
6. Connect to:
   - SSID: `Tesla-Ghost`
   - password: `ghost2018`
7. Browse to `http://192.168.4.1`.

After that, future builds of this project can be uploaded directly from the Ghost Monitor dashboard.

## 0x334 decoding

For `UI_powertrainControl`:

- `UI_pedalMap` is byte 0 bits 5–6.
- The upper nibble of byte 6 is displayed as the rolling counter.
- Byte 7 is checked using:

```text
checksum = (
  CAN_ID_low +
  CAN_ID_high +
  byte0 + byte1 + byte2 + byte3 + byte4 + byte5 + byte6
) & 0xFF
```

The user's captures independently showed:
- `0x9F...` = Chill
- `0xBF...` = Standard

The DBC/upstream project maps value 2 to Performance.

## Vehicle speed

For CAN ID `0x257`:

```text
DI_vehicleSpeed : 12|12 little-endian
scale = 0.08
offset = -40
unit = km/h
```

## Next development stage

Once this firmware is confirmed on the actual board and bus, the next branch can add:
- capture/download from the web dashboard
- persistent settings
- OTA rollback partition handling
- a stationary-gated bench/test transmit path
- optional Flipper UART control

Keep the current working FSD firmware `.bin` available so you can OTA back to it at any time.

## GitHub Actions build

This repository includes `.github/workflows/build.yml`.

After pushing the project to GitHub, open **Actions → Build Waveshare Ghost Firmware → Run workflow**. GitHub will compile the `waveshare-s3-can` target and publish an artifact named `ghost-waveshare-s3-can` containing:

```text
ghost-waveshare-s3-can.bin
```

See `IPHONE_INSTALL.md` for the iPhone-only OTA process.
