#!/bin/bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Matthew Puckett
# Build and install the Zebra P330i CUPS driver on macOS (Apple Silicon or Intel).
# Usage: sudo ./install.sh        (QUEUE=MyName sudo -E ./install.sh to rename the queue)
set -euo pipefail
cd "$(dirname "$0")"

if [[ $EUID -ne 0 ]]; then
  echo "Run with sudo: sudo ./install.sh" >&2
  exit 1
fi

QUEUE="${QUEUE:-Zebra_P330i}"
DEST=/Library/Printers/Zebra/P330i
PPD_DIR=/Library/Printers/PPDs/Contents/Resources

if ! command -v clang >/dev/null 2>&1; then
  echo "clang not found. Install the Xcode Command Line Tools: xcode-select --install" >&2
  exit 1
fi

echo "Building rastertoepcl..."
clang -O2 -Wall -Wno-deprecated-declarations -o rastertoepcl rastertoepcl.c -lcups

echo "Installing filter and PPD..."
install -d -o root -g wheel -m 755 "$DEST"
install -o root -g wheel -m 755 rastertoepcl "$DEST/rastertoepcl"
install -o root -g wheel -m 644 Zebra-P330i.ppd "$PPD_DIR/Zebra-P330i.ppd"

URI="$(lpinfo -v 2>/dev/null | awk '/usb:\/\/.*P330i/{print $2; exit}')"
if [[ -z "$URI" ]]; then
  echo "Driver installed, but no P330i was found on USB."
  echo "Connect and power on the printer, then run this script again to add the queue."
  exit 0
fi

lpadmin -x "$QUEUE" 2>/dev/null || true
lpadmin -p "$QUEUE" -E -v "$URI" -P "$PPD_DIR/Zebra-P330i.ppd" \
        -D "Zebra P330i Card Printer" -L "USB" \
        -o printer-error-policy=abort-job
cupsenable "$QUEUE"
cupsaccept "$QUEUE"
echo "Installed queue '$QUEUE' -> $URI"
