#pragma once

#include <stdint.h>

namespace can_node {

// Standard 11-bit identifier layout from doc/Fly_Micro4_addressing.md.
// [10:4] field, [3:0] type.
inline constexpr uint8_t field(uint16_t id) { return (id >> 4) & 0x7F; }
inline constexpr uint8_t type(uint16_t id) { return id & 0x0F; }

enum Type : uint8_t {
    kMotion = 0,
    kSync = 2,
    kStatus = 3,
    kCommand = 4,
    kHome = 5,
    kParameter = 6,
    kFault = 7,
    kDisc = 8,
    kAssign = 10,
    kAsk = 11,
    kVersionReq = 12,
    kVersionRsp = 13,
};

struct Frame {
    uint16_t id;
    uint8_t dlc;
    uint8_t data[8];
};

struct Stats {
    uint32_t rx;
    uint32_t tx;
    uint32_t error;
    uint32_t dropped;
};

void begin();
bool ready();
bool poll(Frame* out);
Stats stats();

}  // namespace can_node
