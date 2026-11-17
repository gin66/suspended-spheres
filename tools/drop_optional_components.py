#!/usr/bin/env python3
"""Remove unused optional components from the pioarduino Arduino package.

The Arduino 3.3.x component manifest declares ESP RainMaker, esp-zigbee-lib
and esp-zboss-lib for every ESP32-S3 build.  A clean GitHub build therefore
compiles them even though this project does not include either stack.

Run this after the PlatformIO packages are installed and before `pio run`:

    pio pkg install -d esp32gui -e esp32can
    python tools/drop_optional_components.py

The script is idempotent.  It edits the package manifest in the PlatformIO
core directory; a fresh package install restores the original.
"""

import os
import re
import sys

DROP = {
    "espressif/esp_rainmaker",
    "espressif/esp-zigbee-lib",
    "espressif/esp-zboss-lib",
    "espressif/esp_diag_data_store",
    "espressif/esp_diagnostics",
    "espressif/esp_insights",
}

core = os.environ.get("PLATFORMIO_CORE_DIR") or os.path.expanduser("~/.platformio")
framework = os.environ.get("FRAMEWORK_ARDUINO_DIR") or os.path.join(
    core, "packages", "framework-arduinoespressif32"
)
manifest = os.path.join(framework, "idf_component.yml")

if not os.path.isfile(manifest):
    print(f"drop_optional_components: {manifest} not found")
    sys.exit(1)

with open(manifest, "r", encoding="utf-8") as f:
    lines = f.read().splitlines(keepends=True)

out = []
skip = False
removed = []
for line in lines:
    match = re.match(r"^  ([^\s:#]+):\s*$", line)
    if match:
        skip = match.group(1) in DROP
        if skip:
            removed.append(match.group(1))
    if not skip:
        out.append(line)

if removed:
    with open(manifest, "w", encoding="utf-8") as f:
        f.writelines(out)
    print("drop_optional_components: removed " + ", ".join(removed))
else:
    print("drop_optional_components: nothing to remove")
