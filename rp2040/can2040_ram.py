# Exclude the CAN2040-Lite receive/ACK path from the generated flash link so
# the RAM .data catch-all copies its text and rodata.  memmap_katapult.ld
# carries the same exclude.  The tiny IRQ-side parser, the portable handler
# and the Pico backend must all execute from RAM: at 1 Mbit/s the ACK allow
# has about 1.25 us from the terminal CRC edge to the fire check, and an XIP
# cache miss would miss it.

Import("env")

import os

GROUP = "*can2040_lite*.o *can_edge_sm.c.o *can_pio_program.c.o"
NEEDLE_TEXT = "*dvi.c.o) .text*"
INSERT_TEXT = "*dvi.c.o " + GROUP + ") .text*"
NEEDLE_RO = "*libm.a:) .rodata*"
INSERT_RO = "*libm.a: " + GROUP + ") .rodata*"


def patch(path):
    with open(path) as f:
        text = f.read()
    orig = text
    # Replace the old vanilla can2040 object name, which may still be present
    # in a build directory from before the library switch.
    if "*can2040.c.o" in text:
        text = text.replace("*can2040.c.o", GROUP)
    if INSERT_TEXT not in text:
        if NEEDLE_TEXT not in text:
            print("can2040 RAM: .text exclude site not found in", path)
        else:
            text = text.replace(NEEDLE_TEXT, INSERT_TEXT, 1)
    if INSERT_RO not in text:
        if NEEDLE_RO not in text:
            print("can2040 RAM: .rodata exclude site not found in", path)
        else:
            text = text.replace(NEEDLE_RO, INSERT_RO, 1)
    if text != orig:
        with open(path, "w") as f:
            f.write(text)
        print("can2040 RAM: excluded", GROUP, "in", path)


def patch_generated(target, source, env):
    patch(str(target[0]))


if "memmap_default.ld" in env.subst("$LDSCRIPT_PATH"):
    generated = os.path.join("$BUILD_DIR", "memmap_default.ld")
    existing = env.subst(generated)
    if os.path.exists(existing):
        patch(existing)
    env.AddPostAction(generated, patch_generated)
