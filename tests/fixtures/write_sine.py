#!/usr/bin/env python3
"""write_sine.py — write a version-1 .anim.json for the golden sine.

Usage:  python3 write_sine.py <output_path>

Writes one sine layer with the parameters from the whitepaper §6.9:
  amplitude_um  50 000  (50 mm)
  period_ms     5 000
  wavelength_cols 10
  axis          "col"

Peak speed approx 0.05 m * 2pi / 5 s approx 0.063 m/s, inside the ordinary tier.
"""

import json
import sys

def main():
    if len(sys.argv) < 2:
        print("Usage: write_sine.py <output_path>", file=sys.stderr)
        sys.exit(1)

    out_path = sys.argv[1]

    obj = {
        "version": 1,
        "grid": [10, 12],
        "dt_ms": 10,
        "n": 3,
        "duration_ms": 1000,
        "tier": "ordinary",
        "layers": [
            {
                "op": "sine",
                "amplitude_um": 50000,
                "period_ms": 5000,
                "wavelength_cols": 10,
                "axis": "col"
            }
        ]
    }

    with open(out_path, "w") as f:
        json.dump(obj, f, indent=2)
        f.write("\n")

if __name__ == "__main__":
    main()
