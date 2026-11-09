# Katapult — CAN firmware update for the Fly-Micro4

> **Status: procedure, for review.** The node is the Mellow Fly-Micro4 V1.0
> (RP2040, CAN on GPIO 4 / GPIO 5, 1 Mbit/s). This paper is how that board is
> given a CAN bootloader, how the show controller updates one board, and how a
> row of boards is updated. Show CAN identifiers and the permanent board name
> are in `Fly_Micro4_addressing.md`. Motion payloads stay in
> `motion_streaming_whitepaper.md`. This paper does not copy those layouts.
>
> Katapult was CanBoot. The name on current trees and on `flashtool.py` is
> Katapult: <https://github.com/Arksine/katapult>. The protocol description
> used below is Katapult `protocol.md` and the admin bytes in
> `scripts/flashtool.py`. cangaroo is a CAN monitor and is not part of this
> path.

---

## 1. BOOTSEL still recovers the board

The RP2040 USB boot ROM is mask ROM. It is not in the QSPI flash, and neither
Katapult nor the application can overwrite it.

Holding **BOOT** (QSPI chip-select) while the chip comes out of reset stops
the ROM from running whatever is in flash. The chip enumerates as a USB drive
named `RPI-RP2`, vendor/product `2e8a:0003`. Copying a UF2 onto that drive
rewrites flash from the start. That works when:

- the application is missing or corrupt
- Katapult is missing or corrupt
- a CAN update was interrupted
- the board no longer answers on the bus

A bad CAN write is a board that needs this gesture. It is not a dead RP2040.
The same gesture is the bring-up path required beside in-place update
(`NFR-MAINT-004`: update in the installed position, with a USB path for
bring-up, integrity verification, and rollback on failure).

Two consequences:

- Field updates go over CAN, through Katapult, and start at `0x10004000`.
  They do not replace the first 16 KiB.
- `pio run -t upload`, or dragging a UF2 that was linked at address 0, writes
  flash from the beginning and removes Katapult. The board still boots via
  BOOTSEL. Put `katapult.uf2` back before the next CAN update.

The factory Fly-Micro4 image is **USB** Katapult (`Mico_Katapult_USB.uf2` in
Mellow's recovery notes). USB Katapult does not accept a program from the
ESP32 on the trunk. The first shop step replaces it with the CAN build in §4.
That replacement is itself a BOOTSEL copy, so it is the same recovery action.

---

## 2. What lives in flash

| Region | Address | What it is |
|--------|---------|------------|
| Stage-2 + Katapult | `0x10000000` for 16 KiB | CAN bootloader, including the RP2040 stage-2 that brings up XIP |
| Application | `0x10004000` onward | Node firmware. Vector table at the first byte. No second stage-2 |

Katapult's launch address for the RP2040 is `0x10004000` (`LAUNCH_APP_ADDRESS`
in its Kconfig). `CONNECT` must report that address. The flasher refuses any
other value, so a CAN session cannot be aimed at the bootloader.

The permanent name of a board is the **6-byte Katapult UUID** defined in
`Fly_Micro4_addressing.md` §1. The volatile board ID `0–29` stays. An 11-bit
CAN identifier cannot hold a 48-bit name, and the 8-byte motion frame is the
cubic, so the UUID is not a CAN ID. The show map leaves four identifiers
unused so Katapult can speak while the other boards are still programmed.
Those holes are §4 of the addressing note. This paper uses them as follows.

| CAN ID | Direction | Use |
|--------|-----------|-----|
| `0x3F0` | master → nodes | Katapult / Klipper admin |
| `0x3F1` | node → master | Admin response |
| `0x3E8` | master → the board being programmed | Bootloader data. Node byte **372** (`372 × 2 + 0x100 = 0x3E8`) |
| `0x3E9` | that board → master | Bootloader data response |

`0x3F0` is the identifier the old motor numbering used for motor 63
(`(63 << 4) | 0`). The addressing note skips that motor field so a board left
in the bootloader is not fed motion frames on the admin ID. Stock
`flashtool.py` assigns a different data ID (the first node lands on `0x202`)
and broadcasts a node-id clear. Do not point it at this bus. The show
controller is the flasher, and it uses node byte 372 only.

---

## 3. When the motors stop

Entering Katapult resets the RP2040. The four TMC2209s on that board stop
being stepped. Update one board at a time, with the show stopped, and with
those four axes already disabled or mechanically safe. The enter-bootloader
command in §6 disables the four drivers before it writes the request word.

The other boards stay in the application. They keep their board IDs. They do
not see a valid show frame on `0x3F0`, `0x3F1`, `0x3E8`, or `0x3E9`.

---

## 4. Build Katapult for this board

Katapult is its own Make/Kconfig tree. It is not a PlatformIO library and it
is not linked into `rp2040/` or `esp32gui/`. Build it on a machine with
`arm-none-eabi-gcc` and `make`.

```sh
git clone https://github.com/Arksine/katapult
cd katapult
make menuconfig
make
```

`make menuconfig` selections:

| Menu | Value |
|------|--------|
| Micro-controller Architecture | Raspberry Pi RP2040 / RP235x |
| Processor model | rp2040 |
| Build Katapult deployment application | Do not build |
| Communication interface | **CAN bus** |
| CAN RX gpio number | **4** |
| CAN TX gpio number | **5** |
| CAN bus speed | **1000000** |
| Flash chip | **GENERIC_03H with CLKDIV 4** |

Leave the deployment entry on "Do not build". That entry is a helper image
for chips that cannot take a UF2. This board can. The bootloader still jumps
to `0x10004000`.

Do not select "USB to CAN bus bridge" and do not select `USBSERIAL`. Both
produce a different binary than a CAN node. GPIO 4 and GPIO 5 and 1 Mbit/s
are already Katapult's defaults; set them anyway so a later default does not
silently change the image.

`GENERIC_03H` reads the NOR with the standard `0x03` command at a low clock.
That comes up on the usual QSPI parts. A wrong stage-2 looks like a dead
board and is cleared by BOOTSEL, then a corrected UF2. After the fitted NOR
is identified, a later Katapult rebuild may switch to `W25Q080 with CLKDIV 2`
when the part is in that Winbond command set. `FLASH_SIZE` defaults to
`0x200000` (2 MiB). Do not raise it above the chip that is soldered down.
Katapult uses the value as the end of the region it is willing to erase.

`make` writes `out/katapult.uf2` and `out/katapult.bin`. The UF2 is the file
that goes on `RPI-RP2`. Keep the `.config` next to the UF2 so the next build
is the same CAN image.

RP2040 Katapult does not select double-tap reset (`HAVE_BOARD_CHECK_DOUBLE_RESET`
is selected for the RP2350 only). Recovery on this board is BOOTSEL, or the
application command in §6. The BOOT button is the ROM path. It is not a
Katapult gesture.

---

## 5. Install Katapult once

Per board, at the bench or on the rack with a USB cable:

1. Power the board off. Hold **BOOT**.
2. Connect USB-C. Release BOOT when `RPI-RP2` appears.
3. Copy `out/katapult.uf2` onto that drive. The drive ejects itself.
4. Power-cycle on the CAN trunk at 1 Mbit/s, with the bus terminated as in
   `controller_analysis.md`. No application is present, so Katapult stays in
   the bootloader and will answer §7 once the master assigns node byte 372.

The first application image then goes over CAN (§7), not by a second UF2.
A UF2 of the application, linked at address 0, would remove Katapult.

---

## 6. Application image, and how it enters Katapult

### 6.1 Link the PlatformIO firmware at `0x10004000`

`rp2040/` uses the Earle Philhower core. That core links the sketch at
`0x10000000` and prefixes a 256-byte stage-2. Katapult will jump to
`0x10004000` and expects an ARM vector table there. The stock
`.pio/build/fly_micro4/firmware.uf2` is the wrong file for a CAN update.

The CAN payload is a raw `.bin`:

- load address `0x10004000`
- first word: initial stack pointer, in SRAM (`0x200xxxxx`)
- second word: reset handler in `0x10004xxx`, Thumb bit set
- no stage-2 prefix
- length short enough that the image ends before the filesystem or the end
  of the NOR, and at least 16 KiB below the size used when the image was
  linked at address 0

Check the first two words before any CAN write. A file whose first bytes are
stage-2 opcodes is a BOOTSEL image, not a Katapult payload.

`board_build.ldscript` points at a copy of the core `memmap_default.ld` with
`FLASH` origin `0x10004000` and the length reduced by 16 KiB. The stage-2
output section is not in that script. `pio run` produces the `.bin`. Field
updates do not use `pio run -t upload`.

Until that link change is in the tree, there is no application binary to send.

### 6.2 Enter the bootloader from the running application

The show command is `COMMAND` (type 4) with opcode **4** (`ENTER_BOOTLOADER`),
defined with the other opcodes in `esp32gui_whitepaper.md` §9.3. The field is
that board's ID, never 127. The handler disables all four drivers, then
requests Katapult the same way Klipper's `try_request_canboot()` does:

```c
#define CANBOOT_SIGNATURE 0x21746f6f426e6143ULL  /* "CanBoot!" */
#define CANBOOT_REQUEST   0x5984E3FA6CA1589BULL

void enter_katapult(void) {
    uint32_t *vectors = (uint32_t *)0x10000000;
    uint64_t *sig = (uint64_t *)(vectors[1] - 9);
    uint64_t *slot = (uint64_t *)vectors[0];
    if (((uintptr_t)sig & 7) || *sig != CANBOOT_SIGNATURE)
        return;
    if ((uintptr_t)slot & 7)
        return;
    __disable_irq();
    *slot = CANBOOT_REQUEST;
    NVIC_SystemReset();
}
```

The signature check returns without resetting when the CAN Katapult image is
absent. The running application does not listen on `0x3F0`. Klipper's admin
reboot (`0x3F0`, first data byte `0x02`) is not the enter path, and stock
`flashtool.py -r` will not move this firmware into the bootloader.

---

## 7. Flash one board from the ESP32

The panel already speaks classic CAN at 1 Mbit/s (TWAI, TX GPIO 20, RX GPIO
19). The flasher is a task beside that transport. It runs only while the show
transmitter is idle. LVGL does not call it.

The master already holds the board's UUID from `DISC` (`Fly_Micro4_addressing.md`
§3). It does not broadcast `QUERY_UNASSIGNED`. That query makes every
unassigned Katapult node answer at once.

### 7.1 Admin assign

One standard frame, identifier `0x3F0`, 8 data bytes:

| Byte | Value |
|------|--------|
| 0 | `0x11` (`SET_NODEID`) |
| 1–6 | UUID byte 0 first, the same order as `DISC` |
| 7 | `372` |

The board then accepts bootloader bytes on `0x3E8` and answers on `0x3E9`.
A `CLEAR_NODE_ID` (`0x3F0`, byte `0x12`) is a broadcast to every Katapult
node. This procedure does not send it. Only one board is in Katapult.

### 7.2 Byte stream

Bootloader commands are a byte stream chopped into 8-byte CAN frames. Each
command is:

```
01 88 | cmd | word_count | payload | crc16_le | 99 03
```

`word_count` is the payload length in 4-byte words. The CRC16 covers `cmd`,
`word_count`, and the payload. It is the Klipper/Katapult variant, init
`0xFFFF`:

```c
uint16_t crc16_ccitt(const uint8_t *buf, size_t len) {
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < len; i++) {
        uint8_t data = buf[i] ^ (crc & 0xff);
        data ^= (data & 0x0f) << 4;
        crc = (uint16_t)(((data << 8) | (crc >> 8)) ^ (data >> 4) ^ (data << 3));
    }
    return crc;
}
```

Responses use the same framing. Acknowledgement `0xA0`, NACK `0xF1` (retry),
error `0xF2` (abort this board), busy `0xF3` (wait and resend). The first
payload word of an ack is the command it answers.

| Command | Byte | Payload |
|---------|------|---------|
| CONNECT | `0x11` | none |
| SEND_BLOCK | `0x12` | `uint32` address, then 64 data bytes |
| EOF | `0x13` | none |
| REQUEST_BLOCK | `0x14` | `uint32` address |
| COMPLETE | `0x15` | none. The chip resets into the application after the ack |
| GET_CANBUS_ID | `0x16` | none. Response carries the 6-byte UUID |

### 7.3 Order for this board

1. Show transmitter idle. This board's drivers disabled.
2. `COMMAND` opcode 4 to its board ID. Wait 1 s.
3. Admin `SET_NODEID` as in §7.1.
4. `CONNECT`. Require start address `0x10004000`, block size 64, and an MCU
   string that reports `rp2040`. Any other start address aborts the session.
5. `GET_CANBUS_ID`. Require the UUID from step 2's board. A mismatch means
   the wrong node took the id; stop, do not write.
6. `SEND_BLOCK` from `0x10004000`, 64 bytes at a time, in order. Pad the last
   block with `0xFF`. The address in the ack must match.
7. `EOF`.
8. `REQUEST_BLOCK` over the same range. Compare with the padded image.
9. On a match, `COMPLETE`. On a mismatch, do not send `COMPLETE`. Write the
   previous good image from step 6 through step 8, then `COMPLETE`. The chip
   is still in Katapult until `COMPLETE`, which is the rollback
   (`NFR-MAINT-004`).
10. The application boots with no board ID. The next `ASK` produces a `DISC`
    with the same UUID. `ASSIGN` gives it the same board ID as before.
11. `VERSION_REQ` / `VERSION_RSP` must show the new build. If the new image
    never answers, that one board is recovered with BOOTSEL (§1) or, when the
    image still runs, with opcode 4 and the stored previous binary.

The panel keeps the previous raw `.bin` and the new one until step 11 has
passed. There is no second flash slot in Katapult. Rollback after a bad
image has started running is another pass of this section with the stored
previous file.

---

## 8. A row of 15 boards

Do them **one after another**. Do not put the row into the bootloader
together.

A single 1 Mbit/s bus is the only pipe. Interleaving two downloads shares
that pipe, and each RP2040 still erases and programs its own pages between
acknowledgements. Parallel sessions do not give fifteen times the throughput.
They do make a CRC or address error ambiguous, and they reset several boards'
drivers at once.

Katapult can in principle hold several node bytes at once. This installation
reserves one data pair (`0x3E8` / `0x3E9`) and the procedure uses only that
pair. `QUERY_UNASSIGNED` on a bus with more than one Katapult node collides
the answers. The master already knows each UUID, so it never needs that query.

The other boards stay in the application for the whole row. Pick the 15 from
the panel's board list (a physical row, or any other set of 15). The
addressing note still allows board IDs `0–29`; a row of 15 is an operator
selection, not a smaller installation.

For each of the 15, in order:

1. Confirm a fresh `VERSION_RSP` from that board ID. Skip a board that is
   already silent and recover it with BOOTSEL instead of guessing.
2. Run §7 to completion, including the new `VERSION_RSP`.
3. Only then send opcode 4 to the next board.

Stop the row on the first board that fails step 9 or step 11. The boards
already completed stay on the new image. The board that failed is either
still in Katapult, with the previous image restored before `COMPLETE`, or it
is the BOOTSEL case in §7 step 11. The boards not yet started are unchanged.

Expect the wall clock to be the sum of fifteen erase/write/read-back cycles
plus the reboot and `ASSIGN` between them. The CAN time is a small part of
each cycle. The page erase on the board being written is the long part, and
it does not overlap the next board in this procedure.

---

## 9. What the tree still implements

The contract above is the one to build. The code in the tree is still the
previous map:

- `lib/motion_core` encodes `id = (field << 4) | type` with motor fields
  `0–119`, including motor 63, which is CAN ID `0x3F0`.
- `esp32gui` discovery stores an 8-byte flash ID and has no Katapult writer.
  `UPGRADE SELECTED` reports that the transfer is not implemented.
- The host tests in `doc/motion_streaming_tdd.md` §3, P7, P8, and §9 lock
  that previous map.

Do not run a CAN Katapult session against a master that still emits motor 63.
Until `motion_core` skips field 63, keep the show transmitter idle for the
whole session, which this procedure already requires.

- [ ] Update `lib/motion_core` and `esp32gui` to this contract. `motion_core`
  skips motor field 63, and the host tests in `motion_streaming_tdd.md` §3,
  P7, P8, and §9 follow. `esp32gui` discovers and stores the 6-byte UUID,
  and `UPGRADE SELECTED` runs the §7 writer. The image it sends is the
  `rp2040/` build linked at `0x10004000`, and that firmware handles
  opcode 4 (§6.1, §6.2).
