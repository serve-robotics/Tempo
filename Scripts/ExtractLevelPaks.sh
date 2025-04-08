#!/usr/bin/env bash

# ExtractLevelPaks.sh - Extracts pak files while preserving human-readable level names
# Unlike ExtractReleasePaks.sh, this script keeps the renamed level names for easier management

set -e

# Check arguments
if [ $# -lt 2 ]; then
  echo "Usage: $0 <Package directory> <Output directory> [Metadata directory]"
  exit 1
fi

if [ ! -d "$1" ]; then
  echo "Package directory $1 does not exist"
  exit 1
fi

if [ ! -d "$2" ]; then
  mkdir -p "$2"
fi

PACKAGE_PATH=$(realpath "$1")
OUTPUT_PATH=$(realpath "$2")
METADATA_PATH="$PACKAGE_PATH/Metadata"

# If a third argument is provided, use it as the metadata path
if [ $# -ge 3 ] && [ -d "$3" ]; then
  METADATA_PATH=$(realpath "$3")
fi

# Determine platform
PLATFORM=""
if [[ "$OSTYPE" = "msys" ]]; then
  PLATFORM="Win64"
elif [[ "$OSTYPE" = "darwin"* ]]; then
  PLATFORM="Mac"
elif [[ "$OSTYPE" = "linux-gnu"* ]]; then
  PLATFORM="Linux"
else
  echo "Unsupported platform"
  exit 1
fi

echo "======== Extracting Level Paks ========"
echo "Package directory: $PACKAGE_PATH"
echo "Output directory: $OUTPUT_PATH"
echo "Metadata directory: $METADATA_PATH"
echo "Platform: $PLATFORM"

# PAK_PATH will be in different locations depending on platform
# Find directory containing pak files
PAK_PATH=$(find "$PACKAGE_PATH" -name "*.pak" -exec dirname {} \; -quit)

if [ ! -d "$PAK_PATH" ]; then
  echo "Could not find pak path"
  exit 1
fi

# Create output structure
OUTPUT_PAKS_DIR="$OUTPUT_PATH/$PLATFORM/VayuSim/Content/Paks"
mkdir -p "$OUTPUT_PAKS_DIR"

# Copy all pak files
echo "Copying pak, ucas, and utoc files to output directory..."
find "$PAK_PATH" \( -name "*.pak" -o -name "*.ucas" -o -name "*.utoc" \) -exec cp {} "$OUTPUT_PAKS_DIR" \;

# Copy metadata
echo "Copying metadata..."
if [ -d "$METADATA_PATH" ]; then
  mkdir -p "$OUTPUT_PATH/$PLATFORM/Metadata"
  cp -r "$METADATA_PATH/ChunkManifest" "$OUTPUT_PATH/$PLATFORM/Metadata/" 2>/dev/null || true
  cp "$PACKAGE_PATH/AssetRegistry.bin" "$OUTPUT_PATH/$PLATFORM/" 2>/dev/null || true
else
  echo "Warning: Metadata directory not found at $METADATA_PATH"
fi

# Create a manifest of available levels
echo "Creating level manifest..."
LEVEL_MANIFEST="$OUTPUT_PATH/available_levels.txt"
echo "# Available levels in this package" > "$LEVEL_MANIFEST"
echo "# Format: <pak_file>: <level_name>" >> "$LEVEL_MANIFEST"
echo "# Generated on $(date)" >> "$LEVEL_MANIFEST"
echo "" >> "$LEVEL_MANIFEST"

find "$OUTPUT_PAKS_DIR" -name "pakchunk-*.pak" | sort | while read -r pakfile; do
  filename=$(basename "$pakfile")
  level_name="${filename#pakchunk-}"
  level_name="${level_name%-$PLATFORM.pak}"
  echo "$filename: $level_name" >> "$LEVEL_MANIFEST"
done

# Count the paks
PAK_COUNT=$(find "$OUTPUT_PAKS_DIR" -name "*.pak" | wc -l)
LEVEL_PAK_COUNT=$(find "$OUTPUT_PAKS_DIR" -name "pakchunk-*.pak" | wc -l)

echo "======== Extraction Complete ========"
echo "Total pak files: $PAK_COUNT"
echo "Level pak files: $LEVEL_PAK_COUNT"
echo "Output directory: $OUTPUT_PATH"
echo "Level manifest: $LEVEL_MANIFEST"
echo "====================================" 