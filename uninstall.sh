#!/bin/bash
# Remove the Zebra P330i CUPS driver and queue.
# Usage: sudo ./uninstall.sh
set -uo pipefail

if [[ $EUID -ne 0 ]]; then
  echo "Run with sudo: sudo ./uninstall.sh" >&2
  exit 1
fi

QUEUE="${QUEUE:-Zebra_P330i}"
lpadmin -x "$QUEUE" 2>/dev/null && echo "Removed queue '$QUEUE'"
rm -f /Library/Printers/PPDs/Contents/Resources/Zebra-P330i.ppd
rm -rf /Library/Printers/Zebra/P330i
rmdir /Library/Printers/Zebra 2>/dev/null || true
echo "Driver removed."
