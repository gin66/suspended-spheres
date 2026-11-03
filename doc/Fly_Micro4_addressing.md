# Fly-Micro4 — node identity and CAN addressing

> The installation node is the Mellow Fly-Micro4 V1.0. The selection, the
> termination rework and the physical-layer limits are in
> `controller_analysis.md`: R39 and R58 are desoldered from every board, and
> the 120 Ω ends are the Waveshare CAN jumper, set to 120 Ω, and a resistor
> soldered into a cable terminator. This file owns board identity, discovery, the
> volatile board ID, and CAN-ID allocation. The host owns the map from a
> board axis to a grid position. A board does not.
>
> Motion payloads, sample timing and the bus budget stay in
> `motion_streaming_whitepaper.md`. Do not copy the ID layout back into that
> paper.

---

## 1. Hardware identity

The RP2040 has no on-chip unique identifier. All RP2040 silicon instances are
identical and have no persistent state. On the Fly-Micro4 the practical 64-bit
board ID comes from the external QSPI NOR flash:

- `pico_get_unique_board_id()` / `flash_get_unique_id()`
- It is a **board identity, not a secret**; flash unique IDs are readable and
  some are only lot/serial-derived.
- The QSPI flash is soldered to the board, so this ID stays with the board
  for its life.
- Confirm the exact flash part supports the unique-ID command (most NOR flash
  do; the SDK handles 8- or 16-byte variants).
- RP2350/RP2354B (the custom carrier) has an OTP-based unique ID, so the source
  differs on that board.

## 2. Board ID and stepper addresses

The 64-bit flash ID is the only identity that survives power. The short
board ID is RAM. A power cycle clears it, and the board is unidentified
until the master assigns it again.

- Board IDs are **0–29**. The master assigns one. The board does not choose
  it and does not store it in flash.
- Local stepper axes are **0–3**, in silkscreen order Drive0–Drive3.
- The CAN motor field for that axis is `board_id × 4 + axis`. Board 0 owns
  motors 0–3. Board 29 owns motors 116–119. Motors 120–127 are unused.
- The host stores `(flash_id, axis 0–3) → position 0–119`. The board never
  learns that position. Motion frames use the motor field above; the host
  translates a grid position into a board and an axis before it sends.

A known flash ID is given the same board ID it already has in the host map,
so the four motor fields stay put. An unknown flash ID receives the lowest
free board ID.

## 3. Discovery and assignment

The master repeats this until a request draws no advertisement. It also
sends the request every **5 s** for the life of the run, so a board that
lost its RAM ID can rejoin. One 8-byte advertisement is negligible next to
the motion stream.

| Step | Who | CAN ID | Payload |
|------|-----|--------|---------|
| 1. Ask | master | field 0, type `ASK` (11) | 8 × `0x00` |
| 2. Advertise | each board with no board ID | field 0, type `DISC` (8) | its 64-bit flash ID |
| 3. Assign | master, once per flash ID it received | field = board ID, type `ASSIGN` (10) | that 64-bit flash ID |

- Advertisements use one shared identifier. Two different payloads on that
  identifier at the same moment produce an error frame, not two deliveries.
  Each unidentified board sends **once**, `1 ms × (flash_id byte 0 mod 10)`
  after the ask, so the sends fall inside the **10 ms** window and most of
  them do not overlap. The master keeps every `DISC` it receives. Boards
  that were lost in a collision answer on a later ask.
- A board accepts `ASSIGN` only when the payload equals its own flash ID.
  It then stores the 7-bit field as its board ID and stops answering `ASK`.
- A board that already has a board ID stays silent on `ASK`.
- `HELLO` and the two-frame `SET_ID` split are not part of this protocol.
  The board ID is in the `ASSIGN` identifier, and the payload is only the
  flash ID, so one frame is enough.
- The 5 s `ASK` runs during a pattern. Only unidentified boards reply.

## 4. CAN-ID allocation (11-bit standard only)

Every frame on this bus uses a **standard 11-bit identifier**. 29-bit extended
identifiers are not used for motion, sync, status, commands or discovery.
Time is carried by the sync frame in `motion_streaming_whitepaper.md` §4.5,
not by the identifier.

```
[ 10:4 ]  7-bit field: motor 0–119, board 0–29, 127 = all boards, or 0 = broadcast
[  3:0 ]  4-bit type
```

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
| 8 | `DISC` — unidentified board, payload is the 64-bit flash ID |
| 9 | reserved (was `HELLO`) |
| 10 | `ASSIGN` — payload is the 64-bit flash ID, field is the board ID |
| 11 | `ASK` — master asks unidentified boards to advertise |

The 7-bit field is a motor field, a board ID, 127, or zero. The type says which:

| Types | Field |
|-------|--------|
| 0 (motion) | motor `board_id × 4 + axis`, axis 0–3, result 0–119 |
| 1, 9 | reserved; reject |
| 2 (sync), 8 (`DISC`), 11 (`ASK`) | 0 |
| 3–7 (status, command, home, parameter, fault) | board ID 0–29, or 127 for every board |
| 10 (`ASSIGN`) | the board ID being given, 0–29 |

- Type 0 carries a motor field, never a board ID by itself. Motor 0 is
  board 0 axis 0; it is a legal motion field. The same numeric value can
  be motor 5 or board 5 because the type is different.
- Field 127 on types 3–7 addresses every board. Field 0 on those types is
  board 0, not a broadcast. `ASK` and `DISC` are the broadcasts, and both
  use field 0.
- Board *b* accepts motion for motors `4b … 4b+3`, frames addressed to
  board *b*, frames with field 127 on types 3–7, and field 0 on types 2, 8
  and 11. Every other motion frame is discarded before the payload is
  parsed. The receive-path timing is in `motion_streaming_whitepaper.md` §5.5.
- Types 12–15 are reserved.
- The motion payload layout is in `motion_streaming_whitepaper.md` §4.2. Sync, status and command payloads are in §4.5 of that paper.
