# Fly-Micro4 — node identity and CAN addressing

> The installation node is the Mellow Fly-Micro4 V1.0. The selection, the
> termination rework and the physical-layer limits are in
> `controller_analysis.md`. This file owns board identity, node discovery,
> short node IDs, motor-ID assignment and CAN-ID allocation.
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
- It changes if the flash is replaced; re-provision the logical node ID after a
  board swap.
- Confirm the exact flash part supports the unique-ID command (most NOR flash
  do; the SDK handles 8- or 16-byte variants).
- RP2350/RP2354B (the custom carrier) has an OTP-based unique ID, so the source
  differs on that board.

## 2. Short node ID and motor-ID offsets

- The master assigns a short node ID **1–30** and a grid position. The node
  ID occupies the whole 7-bit field on node-level frames; it is not a 5-bit
  subfield packed beside the motor ID.
- Node *n* owns motor IDs `4(n−1)+1 … 4(n−1)+4`; store `axes_per_node` with the
  mapping.
- The node stores its short ID, grid position and calibration in flash.
- The master stores `flash_id → short_id → grid_position`.
- On boot an assigned node sends `HELLO`; an unassigned node advertises `DISC`.
- Reject duplicate flash IDs and duplicate short-ID claims; a cloned or
  replaced flash, or a copied node config, is a maintenance event.

## 3. Commissioning frames

The 64-bit flash ID is advertised in **one classic CAN data frame** (8 B).
Thirty nodes × 8 B is negligible; discovery is boot/maintenance traffic only
and does not affect the motion bus budget.

| Step | CAN ID field/type | Payload |
|------|-------------------|---------|
| Discover | field 0, type `DISC` | full 64-bit flash ID (8 B) |
| Hello | node ID, type `HELLO` | full 64-bit flash ID |
| Assign | field 0, type `SET_ID` A then B | A: 8-byte flash ID. B: 4-byte prefix, short ID, flags, column, row |
| Confirm | node ID, type `HELLO` | short node ID in CAN ID, flash ID in payload |

- Unassigned nodes advertise on `DISC` with a deterministic backoff derived
  from the flash ID (e.g. a 10-bit hash × 5 ms). The identifier stays 11-bit.
  Discovery is boot and maintenance traffic; it does not run during a pattern.
- `SET_ID` uses two consecutive frames because the 64-bit flash ID plus the
  assigned ID does not fit in one 8-byte payload. Both are field 0,
  little-endian:

  | Frame | Type | Bytes |
  |-------|------|--------|
  | A | 10 | `0–7` flash ID, the same 8 bytes as `DISC` |
  | B | 11 | `0–3` flash ID prefix (bytes 0–3 of A); `4` short node ID; `5` flags; `6` column; `7` row |

  The node accepts B only when bytes 0–3 equal its own flash ID. `flags` bit 0
  means the column and row are valid. Column is `0–9`, row is `0–11`. The
  master stores `axes_per_node` (4 on this board) in its own table.
- A node with a stored short ID uses it on boot and sends `HELLO`; the master
  verifies the mapping. If the master's config is lost, it can reassign.

## 4. CAN-ID allocation (11-bit standard only)

Every frame on this bus uses a **standard 11-bit identifier**. 29-bit extended
identifiers are not used for motion, sync, status, commands or discovery.
Time is carried by the sync frame in `motion_streaming_whitepaper.md` §4.5,
not by the identifier.

```
[ 10:4 ]  7-bit field: motor ID (1–120), node ID (1–30), or 0 = system
[  3:0 ]  4-bit type
```

| Type | Meaning |
|------|---------|
| 0 | motion, `N=3` (3 positions + speed) |
| 1 | motion, `N=4` (4 positions) |
| 2 | sync / window header |
| 3 | status |
| 4 | command |
| 5 | home |
| 6 | parameter |
| 7 | fault / E-stop |
| 8 | `DISC` (identity advertisement) |
| 9 | `HELLO` (identity hello) |
| 10 | `SET_ID` A |
| 11 | `SET_ID` B |

The 7-bit field is a motor ID, a node ID, or zero. The type says which:

| Types | Field |
|-------|--------|
| 0, 1 (motion) | motor ID, 1–120 |
| 2 (sync) | 0, broadcast |
| 3–7 (status, command, home, parameter, fault) | node ID 1–30, or 0 when the master addresses every node |
| 8, 10, 11 (`DISC`, `SET_ID` A/B) | 0 |
| 9 (`HELLO`) | the node's short ID |

- Motion types 0 and 1 never carry a node ID. A node-level type never carries a motor ID. The same numeric field can therefore be motor 5 or node 5 without a collision.
- Field 0 is the system/broadcast field. `DISC` and `SET_ID` always use it. A fault or command with field 0 is a broadcast; the hardwired E-stop is still the safety stop.
- Node *n* accepts motion for motor IDs `4(n−1)+1 … 4(n−1)+4`, frames addressed to node *n*, and field 0. Every other motion frame is discarded before the payload is parsed. The receive-path timing is in `motion_streaming_whitepaper.md` §5.5.
- Types 12–15 are reserved.
- The motion payload layout is in `motion_streaming_whitepaper.md` §4.2. Sync, status and command payloads are in §4.5 of that paper.
