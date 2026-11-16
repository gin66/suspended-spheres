#pragma once

namespace katapult_enter {

/* Disable the four drivers, check the Katapult signature through the R3
 * core helper, write the request word and reset.  Returns false and leaves
 * the application running when the bootloader is absent or a pointer is
 * misaligned.  Never called from the CAN interrupt. */
bool request(void);

}  // namespace katapult_enter
