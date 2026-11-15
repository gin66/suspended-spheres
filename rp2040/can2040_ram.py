# Exclude can2040 from the generated flash link so the RAM .data catch-all
# copies its text and rodata. memmap_katapult.ld carries the same exclude.

Import("env")

import os

NEEDLE_TEXT = "*dvi.c.o) .text*"
INSERT_TEXT = "*dvi.c.o *can2040.c.o) .text*"
NEEDLE_RO = "*libm.a:) .rodata*"
INSERT_RO = "*libm.a: *can2040.c.o) .rodata*"


def patch(path):
    with open(path) as f:
        text = f.read()
    orig = text
    if "*can2040.c.o) .text*" not in text:
        if NEEDLE_TEXT not in text:
            print("can2040 RAM: .text exclude site not found in", path)
        else:
            text = text.replace(NEEDLE_TEXT, INSERT_TEXT, 1)
    if "*can2040.c.o) .rodata*" not in text:
        if NEEDLE_RO not in text:
            print("can2040 RAM: .rodata exclude site not found in", path)
        else:
            text = text.replace(NEEDLE_RO, INSERT_RO, 1)
    if text != orig:
        with open(path, "w") as f:
            f.write(text)
        print("can2040 RAM: excluded can2040.c.o in", path)


def patch_generated(target, source, env):
    patch(str(target[0]))


if "memmap_default.ld" in env.subst("$LDSCRIPT_PATH"):
    generated = os.path.join("$BUILD_DIR", "memmap_default.ld")
    existing = env.subst(generated)
    if os.path.exists(existing):
        patch(existing)
    env.AddPostAction(generated, patch_generated)
