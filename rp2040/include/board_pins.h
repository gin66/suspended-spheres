#pragma once

#include <stdint.h>

// Fly-Micro4 V1.0 pin map. Source: boards/Fly-Micro4/Fly_Micro4_pinmap.md and
// boards/Fly-Micro4/Fly-Micro4.h. Verify the board revision before flashing.

namespace board {

struct DrivePins {
    uint8_t step;
    uint8_t dir;
    uint8_t en;
    uint8_t diag;
    uint8_t uart_address;
};

inline constexpr uint8_t kDriveCount = 4;

// Silkscreen Drive0..Drive3. The four EN lines are active low. The UART
// addresses follow the address printed on the board and the pin poster:
// 0, 1, 2, 3 for Drive0..Drive3. The shipped Klipper reference config happens
// to put Z/E0 on 2/3, but the board straps are identity; see
// boards/Fly-Micro4/Fly_Micro4_pinmap.md §1.
inline constexpr DrivePins kDrives[kDriveCount] = {
    { 6, 3, 7, 13, 0 },
    { 1, 0, 2, 14, 1 },
    { 26, 25, 27, 15, 2 },
    { 23, 22, 24, 12, 3 },
};

inline constexpr bool kEnableActiveLow = true;

// One half-duplex wire shared by the four TMC2209 PDN_UART pins. The driver
// addresses 0..3 follow the drive index; see doc/controller_analysis.md §6.1.
inline constexpr uint8_t kTmcUartPin = 9;

inline constexpr uint8_t kCanRxPin = 4;
inline constexpr uint8_t kCanTxPin = 5;

inline constexpr uint8_t kStatusLedPin = 8;

}  // namespace board
