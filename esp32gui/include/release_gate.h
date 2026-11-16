#pragma once

/* The field image is esp32can with SS_RELEASE and no mock transport.
 * firmware_release_whitepaper.md §3.1 and firmware_release_tdd.md S5. */
#if defined(SS_CAN_MOCK) && defined(SS_RELEASE)
#error "release image must not contain the mock CAN transport"
#endif
