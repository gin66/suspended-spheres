# Fly-Micro4 — node identity and CAN addressing

> The installation node is the Mellow Fly-Micro4 V1.0. Selection, termination and
> physical-layer limits are in `controller_analysis.md`: termination is
> jumper-selectable — J3 inserted only on the last board, open on every other
> node. R39/R58 stay fitted; no desoldering is needed. This file owns board
> identity, discovery, the volatile board ID, and CAN-ID allocation. The host
> owns the map from a board axis to a grid position. A board does not.
>
> Motion payloads, sample timing and the bus budget stay in
> `motion_streaming_whitepaper.md`. Do not copy the ID layout back into that
> paper. The CAN bootloader that occupies four of these identifiers is
> `katapult_whitepaper.md`.

---

## 1. Hardware identity

The RP2040 has no on-chip unique identifier. All RP2040 silicon instances are
identical and have no persistent state. On the Fly-Micro4 the raw material is
the 8-byte unique ID of the external QSPI NOR:

- `pico_get_unique_board_id()` / `flash_get_unique_id()`
- It is a **board identity, not a secret**; flash unique IDs are readable and
  some are only lot/serial-derived.
- The QSPI flash is soldered to the board, so this ID stays with the board
  for its life.
- Confirm the exact flash part supports the unique-ID command (most NOR flash
  do; the SDK handles 8- or 16-byte variants). The hash below takes the first
  8 bytes, which is what the RP2040 read-unique-ID command returns.
- RP2350/RP2354B (the custom carrier) has an OTP-based unique ID, so the
  source bytes differ on that board. The on-wire name is still the 6-byte
  UUID computed the same way.

The value on the wire, in the host map, and in Katapult is not those 8 bytes.
It is the **6-byte UUID** Katapult and Klipper derive:

1. `hash = fasthash64(unique_id[0..7], seed = 0xA16231A7)`, the function in
   Klipper `src/generic/fasthash.c`.
2. The UUID is the little-endian low 6 bytes of `hash`. Byte 0 of the UUID
   is bits 7..0 of the hash. Byte 5 is bits 47..40.
3. `DISC`, `ASSIGN`, and Katapult `SET_NODEID` all send byte 0 first.

The raw 8-byte flash ID is not a payload and is not a CAN identifier. A
48-bit UUID does not fit in an 11-bit identifier, so the UUID is only a name.
The short board ID in §2 is the runtime alias.

## 2. Board ID and stepper addresses

The 6-byte UUID is the only identity that survives power. The short board ID
is RAM. A power cycle clears it, and the board is unidentified until the
master assigns it again.

- Board IDs are **0–29**. The master assigns one. The board does not choose
  it and does not store it in flash.
- Local stepper axes are **0–3**, in silkscreen order Drive0–Drive3.
- The motor field skips **63**, because `(63 << 4) | 0` is Katapult's admin
  identifier `0x3F0` (`katapult_whitepaper.md`). With
  `raw = board_id × 4 + axis`:

  | `raw` | Motor field |
  |-------|-------------|
  | `0 … 62` | `raw` |
  | `63 … 119` | `raw + 1` |

  Board 0 owns motors 0–3. Board 15 owns 60, 61, 62 and 64. Board 29 owns
  117–120. Field 63 is never a motor. Fields 121–127 are not motors.
- The host stores `(uuid, axis 0–3) → position 0–119`. The board never
  learns that position. Motion frames use the motor field above; the host
  translates a grid position into a board and an axis before it sends.

A known UUID is given the same board ID it already has in the host map,
so the four motor fields stay put. An unknown UUID receives the lowest
free board ID.

## 3. Discovery and assignment

The master repeats this until a request draws no advertisement. It also
sends the request every **5 s** for the life of the run, so a board that
lost its RAM ID can rejoin. One 8-byte advertisement is negligible next to
the motion stream.

| Step | Who | CAN ID | Payload |
|------|-----|--------|---------|
| 1. Ask | master | field 0, type `ASK` (11) | 8 × `0x00` |
| 2. Advertise | each board with no board ID | field 0, type `DISC` (8) | 6-byte UUID, then `0x00`, `0x00` |
| 3. Assign | master, once per UUID it received | field = board ID, type `ASSIGN` (10) | that same 8-byte payload |

- Advertisements use one shared identifier. Two different payloads on that
  identifier at the same moment produce an error frame, not two deliveries.
  Each unidentified board sends **once**, `1 ms × (UUID byte 0 mod 10)`
  after the ask, so the sends fall inside the **10 ms** window and most of
  them do not overlap. The master keeps every `DISC` it receives. Boards
  that were lost in a collision answer on a later ask.
- A board accepts `ASSIGN` only when the first 6 bytes equal its own UUID
  and the last two bytes are zero. It then stores the 7-bit field as its
  board ID and stops answering `ASK`.
- A board that already has a board ID stays silent on `ASK`.
- `HELLO` and the two-frame `SET_ID` split are not part of this protocol.
  The board ID is in the `ASSIGN` identifier, and the payload is only the
  UUID, so one frame is enough.
- The 5 s `ASK` runs during a pattern. Only unidentified boards reply.

## 4. CAN-ID allocation (11-bit standard only)

Every frame on this bus uses a **standard 11-bit identifier**. 29-bit extended
identifiers are not used for motion, sync, status, commands or discovery.
Time is carried by the sync frame in `motion_streaming_whitepaper.md` §4.5,
not by the identifier.

```
[ 10:4 ]  7-bit field: motor, board 0–29, 127 = all boards, or 0 = broadcast
[  3:0 ]  4-bit type
```

Four identifiers are reserved for Katapult and are never a legal show frame.
`katapult_whitepaper.md` owns how they are used:

| Identifier | Why it is free under the rules below |
|------------|----------------------------------------|
| `0x3E8` | field 62, type 8. `DISC` allows field 0 only |
| `0x3E9` | field 62, type 9. Type 9 is rejected |
| `0x3F0` | field 63, type 0. Field 63 is not a motor |
| `0x3F1` | field 63, type 1. Type 1 is rejected |

| Type | Meaning |
|------|---------|
| 0 | motion: cubic segment `p0, v0, a0, j` (whitepaper §4.2) |
| 1 | reserved (was four position samples; do not send) |
| 2 | sync / window header |
| 3 | status |
| 4 | command |
| 5 | home |
| 6 | parameter |
| 7 | fault / E-stop |
| 8 | `DISC` — unidentified board, payload is the 6-byte UUID plus two zero bytes |
| 9 | reserved (was `HELLO`) |
| 10 | `ASSIGN` — payload is the UUID plus two zero bytes, field is the board ID |
| 11 | `ASK` — master asks unidentified boards to advertise |
| 12 | `VERSION_REQ` — field is a board ID or 127 for every board |
| 13 | `VERSION_RSP` — field is the answering board ID |

The 7-bit field is a motor field, a board ID, 127, or zero. The type says which:

| Types | Field |
|-------|--------|
| 0 (motion) | motor field from §2: `0–62` and `64–120`. Field 63 and fields `121–127` are rejected |
| 1, 9 | reserved; reject |
| 2 (sync), 8 (`DISC`), 11 (`ASK`) | 0 |
| 3–7 (status, command, home, parameter, fault) | board ID 0–29, or 127 for every board |
| 10 (`ASSIGN`) | the board ID being given, 0–29 |
| 12 (`VERSION_REQ`) | board ID 0–29, or 127 for every board |
| 13 (`VERSION_RSP`) | the answering board ID, 0–29 |

- Type 0 carries a motor field, never a board ID by itself. Motor 0 is
  board 0 axis 0; it is a legal motion field. The same numeric value can
  be motor 5 or board 5 because the type is different.
- Field 127 on types 3–7 addresses every board. Field 0 on those types is
  board 0, not a broadcast. `ASK` and `DISC` are the broadcasts, and both
  use field 0.
- Board *b* accepts motion for the four motor fields §2 assigns it, frames
  addressed to board *b*, frames with field 127 on types 3–7, and field 0
  on types 2, 8 and 11. Every other motion frame is discarded before the
  payload is parsed. The receive-path timing is in
  `motion_streaming_whitepaper.md` §5.5.
- Identifiers `0x3E8`, `0x3E9`, `0x3F0`, and `0x3F1` are dropped even if a
  later type code would have produced them.
- Types 12 and 13 are the version query and response. Types 14–15 remain
  reserved.
- The motion payload layout is in `motion_streaming_whitepaper.md` §4.2. Sync, status and command payloads are in §4.5 of that paper.
