#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Print the serial port of the single connected Espressif USB device (VID
# 303A). With none or several connected it lists what it sees and fails, so
# flashing never has to guess which board to overwrite. Needs ESP-IDF's
# Python (pyserial) on PATH: source scripts/idf_env.sh first.

set -euo pipefail

python - <<'PY'
import sys
from serial.tools import list_ports

ports = [p for p in list_ports.comports() if p.vid == 0x303A]
if len(ports) == 1:
    print(ports[0].device)
    sys.exit(0)
if not ports:
    print("No Espressif USB device (303A:*) found. Is the Tab5 connected?", file=sys.stderr)
else:
    print("Several Espressif devices are connected; pass the Tab5's port explicitly:", file=sys.stderr)
    for p in ports:
        print(f"  {p.device}  {p.description}  serial={p.serial_number}", file=sys.stderr)
sys.exit(1)
PY
