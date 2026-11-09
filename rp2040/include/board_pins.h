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
};

inline constexpr uint8_t kDriveCount = 4;

// Silkscreen Drive0..Drive3. The four EN lines are active low.
inline constexpr DrivePins kDrives[kDriveCount] = {
    { 6, 3, 7, 13 },
    { 1, 0, 2, 14 },
    { 26, 25, 27, 15 },
    { 23, 22, 24, 12 },
};

inline constexpr bool kEnableActiveLow = true;

inline constexpr uint8_t kCanRxPin = 4;
inline constexpr uint8_t kCanTxPin = 5;

inline constexpr uint8_t kStatusLedPin = 8;

}  // namespace board
