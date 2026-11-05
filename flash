#!/bin/bash
# Wrapper script for flashing CH32V30x via WCH-Link using minichlink
# minichlink is a simple, reliable tool from ch32fun project

ELF="$1"
BIN="${ELF%.elf}.bin"

echo "Flashing CH32V30x firmware via WCH-Link using minichlink..."
echo "Using binary: $BIN"
echo "Flash address: 0x00000000"
echo ""

# Use minichlink to write the binary to flash
# -w writes binary file to specified address
minichlink -w "$BIN" 0x00000000

RESULT=$?

if [ $RESULT -eq 0 ]; then
    echo ""
    echo "✅ Flash successful!"
    echo "Firmware programmed to CH32V30x via minichlink"
else
    echo ""
    echo "❌ Flash failed"
    echo ""
    echo "Troubleshooting:"
    echo "1. Ensure WCH-Link is connected and powered"
    echo "2. Check USB permissions (may need udev rules)"
    echo "3. Try power-cycling the target board"
    echo "4. Verify the binary file exists: $BIN"
    echo ""
    echo "Alternative: Use WCH-LinkUtility for GUI flashing"
fi

exit $RESULT

