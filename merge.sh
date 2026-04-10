#!/bin/bash
# Create a merged binary (bootloader + partition table + OTA data + app)
# that can be flashed at offset 0x0 for web flasher / deployment use.
#   Output: build/reservoir.bin (overwrites the app-only binary)
set -e

BUILD_DIR="build"

echo "Creating merged firmware binary..."

# merge_bin can't read and write the same file, so use a temp name
esptool.py --chip esp32s3 merge_bin -o "$BUILD_DIR/reservoir_merged.bin" \
    --flash_mode dio --flash_size 8MB \
    0x0 "$BUILD_DIR/bootloader/bootloader.bin" \
    0x8000 "$BUILD_DIR/partition_table/partition-table.bin" \
    0xe000 "$BUILD_DIR/ota_data_initial.bin" \
    0x10000 "$BUILD_DIR/reservoir.bin"

mv "$BUILD_DIR/reservoir_merged.bin" "$BUILD_DIR/reservoir.bin"

echo ""
echo "Merged binary ready:"
ls -lh "$BUILD_DIR/reservoir.bin"
