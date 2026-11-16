#include "katapult_enter.h"

#include <Arduino.h>
#include <hardware/watchdog.h>
#include <pico/stdlib.h>
#include <pico/sync.h>

#include "motion_core.h"

namespace katapult_enter {
namespace {

/* RP2040 Katapult keeps its stage-2 in the first 256 bytes at 0x10000000;
 * its vector table is at CONFIG_FLASH_BOOT_ADDRESS = 0x10000100.  The
 * application is at 0x10004000.  The signature is nine bytes before the
 * bootloader reset handler (katapult_whitepaper.md §6.2). */
constexpr uintptr_t kVectorBase = 0x10000100u;

}  // namespace

bool request(void) {
    const uint32_t *vectors = reinterpret_cast<const uint32_t *>(kVectorBase);
    const uint32_t stack_pointer = vectors[0];
    const uint32_t reset_handler = vectors[1];

    const uintptr_t sig_address =
        mc_katapult_signature_address(reset_handler);
    const uintptr_t slot_address = (uintptr_t)stack_pointer;
    const int sig_aligned = ((sig_address & 7u) == 0) ? 1 : 0;
    const int slot_aligned = ((slot_address & 7u) == 0) ? 1 : 0;

    uint64_t signature = 0;
    if (sig_aligned) {
        signature = *reinterpret_cast<const uint64_t *>(sig_address);
    }
    if (!mc_katapult_request_ok(signature, sig_aligned, slot_aligned)) {
        return false;
    }

    save_and_disable_interrupts();
    *reinterpret_cast<volatile uint64_t *>(slot_address) = MC_CANBOOT_REQUEST;
    watchdog_reboot(0, 0, 0);
    for (;;)
        tight_loop_contents();
    return true;   /* not reached */
}

}  // namespace katapult_enter
