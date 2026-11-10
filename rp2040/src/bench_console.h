#pragma once

// USB-serial bring-up console for current and StallGuard tuning. It drives
// the same stepper_axes and tmc2209 modules as the CAN path. This is a bench
// interface, not a production operator interface.

namespace bench_console {

void begin();
void poll();

// Set by the `reinit` command. The main loop stops the axes and re-runs the
// TMC bring-up, so the node can be reconfigured after the motor supply is
// applied without unplugging USB.
bool take_reinit_request();

// Runtime serial-log switch. False suppresses the periodic CAN statistics and
// the per-command logs; CAN traffic itself is unchanged.
bool logs_enabled();

}  // namespace bench_console
