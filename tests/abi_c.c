/* abi_c.c — H4: the public C ABI links from a C caller.
 *
 * Compiled as C, not C++.  This proves the header exports unmangled names
 * with the declared integer widths.  Return values are irrelevant here;
 * a link failure is the assertion.
 */

#include "motion_core.h"

int mc_abi_probe(void) {
    mc_segment_t seg = {1, 2, 3, 4};
    mc_segment_t out_seg = {0, 0, 0, 0};
    uint8_t frame[8] = {0};
    int16_t steps[4] = {0};
    uint16_t ticks = 0;

    (void)mc_pack_frame(0, &seg, frame);
    (void)mc_unpack_frame(frame, 8, 0, &out_seg);
    (void)mc_trajectory_push(0, 0, 0, 1000, &seg);
    (void)mc_eval(0, 0);
    (void)mc_step_chunk(steps, &ticks, 0, 2000);

    return 0;
}
