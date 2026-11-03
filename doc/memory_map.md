# Memory map — Waveshare 7" show controller

Measured from the `esp32usb` build on 2026-11-03, after the CAN scope and
node-firmware screens. The numbers are link-time figures; runtime heap and
task stacks are lower because LVGL and the CAN consumer allocate from the
remaining internal RAM.

Regenerate after a build:

```sh
cd esp32gui
pio run -e esp32usb
~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-size \
  -A .pio/build/esp32usb/firmware.elf
```

The full linker map is `.pio/build/esp32usb/firmware.map`; it is a build
artifact and is not tracked.

---

## 1. Flash — 16 MiB

Partition table from `partitions_16m.csv`:

| Region | Offset | Size | Purpose |
|--------|--------|------|---------|
| bootloader + partition data | `0x000000` | `0x9000` | ROM bootloader, partition table, boot_app0 |
| `nvs` | `0x009000` | `0x004000` | 16 KiB NVS |
| `otadata` | `0x00D000` | `0x002000` | OTA selector |
| `phy_init` | `0x00F000` | `0x001000` | PHY calibration |
| `ota_0` | `0x010000` | `0x600000` | application slot A, 6 MiB |
| `ota_1` | `0x610000` | `0x600000` | application slot B, 6 MiB |
| `coredump` | `0xC10000` | `0x040000` | 256 KiB |
| `spiffs` | `0xC50000` | `0x3B0000` | 3.69 MiB config store |

Current application image:

| Section | Bytes |
|---------|-------|
| `.flash.text` (code) | 407 472 |
| `.flash.rodata` | 231 220 |
| `.eh_frame` | 9 152 |
| `.flash.appdesc` | 256 |
| **Linked sections** | **648 100** |
| **`firmware.bin`** | **737 052** (0.70 MiB) |
| **Free in the 6 MiB OTA slot** | **~5.30 MiB** |
| **Free in both OTA slots** | **~10.6 MiB** |

`firmware.bin` is larger than the linked-section sum because it also carries
initialised data and image padding.

ROM cost is not the constraint. The OTA slot stays large enough for the future
HBP2 playback code and a second recovery image.

---

## 2. Internal SRAM

| Region | Used | Total | Note |
|--------|------|-------|------|
| `.dram0.data` | 15 317 | — | initialised variables |
| `.dram0.bss` | 136 808 | — | zero-initialised variables |
| `.noinit` | 3 | — | survives software reset |
| **static RAM** | **152 128** | **327 680** | **46.4 %** |
| DIRAM `.text` | 57 239 | — | code placed in DIRAM |
| DIRAM total | 209 364 | 341 760 | 61.3 %, includes DIRAM code |
| DIRAM free at link | 132 396 | — | heap, task stacks, LVGL objects |
| IRAM | 16 384 | 16 384 | 100 %: vectors 1 028 + fast text 15 356 |
| RTC slow | 12 | 7 680 | |

The largest internal-RAM objects in this build:

| Object | Bytes | Owner |
|--------|-------|-------|
| `s_draw_buf` | 32 000 | LVGL partial RGB565 draw buffer, 20 lines |
| `s_can_buf` | 24 576 | CAN scope text snapshot, 256 × 96 B |
| `s_snapshot` | 16 708 | UI snapshot, including 256 CAN rows |
| CAN monitor table | 14 336 | 256 × 56 B identifier rows |
| CAN snapshot scratch | 14 336 | 256 × 56 B copy before the UI snapshot |
| CAN monitor ring | 2 560 | 128 queued RX frames |
| Commission node registry | 2 160 | 30 nodes |
| SPIFFS config | 1 490 | `/config.bin` |
| Config test order | 480 | 120 positions |

The CAN scope costs about 54 KiB of internal RAM (table + scratch + text
buffer). That is the main reason `.bss` rose when the scope was added. A
smaller scope limit would free it again, but 256 IDs leave room for a running
show plus commissioning and version traffic.

Runtime heap must also hold the Arduino/FreeRTOS task stacks, LVGL object
memory and the SPIFFS cache. Keep at least ~64 KiB of DIRAM free at runtime;
the link-time free figure above is not a runtime guarantee.

---

## 3. PSRAM — 8 MiB octal, 80 MHz

The module is `ESP32-S3-WROOM-1-N16R8`. `CONFIG_SPIRAM_USE_MALLOC=y` and
`CONFIG_SPIRAM_MODE_OCT=y`.

| User | Bytes | Note |
|------|-------|------|
| RGB panel framebuffer | 768 000 | `800 × 480 × 2 B`, allocated by `esp_lcd` in PSRAM |
| Application log ring | 262 144 | 256 KiB, `APP_LOG_DEFAULT_BYTES` |
| Rotation scratch | 32 000 | only when the panel is not in landscape |
| **HBP2 chunk cache target** | **4 194 304** | 4 MiB, see §4 |
| **Committed total** | **~5.01 MiB** | with rotation scratch |
| **Margin** | **~2.99 MiB** | heap, fragmentation, future buffers |

The framebuffer is the only PSRAM allocation made by the display path in
landscape. LVGL object memory stays in internal RAM; `lv_conf.h` deliberately
does not move objects to PSRAM.

---

## 4. HBP2 playback: PSRAM chunks, not continuous SD streaming

The SD card is a source, not the live playback medium. A stalled SD read must
not stall the CAN stream, so the show task never reads the card directly.

- At load, read the HBP2 header and block table into internal RAM.
- Fill a PSRAM chunk cache from SD before starting. If the whole body fits,
  SD is idle during the show.
- If the body is larger than the cache, a low-priority refill task replaces
  consumed chunks. The show task takes frames only from PSRAM.
- Check each HBP2 block CRC32 before publishing the block to the show task.
  Never play a partial block.
- On a short read, CRC failure or cache underrun: retry the block once, then
  stop with a safe hold and a log entry. Do not play stale frames.

Budget at `N=3`, `Δt=10 ms`, 120 motors:

```
frames/s = 120 / (3 × 0.010 s) = 4000
bytes/s  = 4000 × 8 B = 32 000 B/s = 31.25 KiB/s
4 MiB    ≈ 131 s of frames
```

Proposed cache layout:

| Parameter | Value |
|-----------|-------|
| Chunk size | 256 KiB (≈ 8 s at 32 KiB/s) |
| Slots | 16 |
| Total | 4 MiB |
| Refill high-water mark | keep ≥ 4 chunks (≈ 32 s) queued |
| Low-water warning | < 2 chunks (≈ 16 s) |
| Underrun | safe hold |

The cache is large enough to hide normal SD latency and a multi-second stall.
A card that cannot sustain 32 KiB/s or that fails CRC repeatedly is a fault,
not something the PSRAM buffer should mask indefinitely.

If a later codec reduces the file size, keep the cache size and increase the
covered time; do not spend the saved PSRAM on a larger framebuffer.

---

## 5. Rules for future changes

- Re-run the size command after adding a screen, buffer or table. Update this
  file when a row moves by more than about 8 KiB.
- Do not put LVGL objects or task stacks in PSRAM without measuring the redraw
  cost; the house panel already showed that moving objects to PSRAM makes the
  UI slower.
- Keep the RGB DMA draw buffer in internal RAM. The panel DMA path does not
  reliably cross into PSRAM.
- The HBP2 cache is the first place to take PSRAM from. The framebuffer and
  log ring are fixed; the remaining ~2.99 MiB is the safety margin.
