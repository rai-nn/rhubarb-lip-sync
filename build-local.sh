#!/bin/bash

# Build script for Rhubarb Lip Sync
# Builds the main executable and copies it to the visemes server

set -e

echo "🔨 Building Rhubarb Lip Sync..."

# Store the script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
TARGET_DIR="$SCRIPT_DIR/../lib/generators/implementations/rhubarb-lite/binary"
APP_INFO_FILE="$SCRIPT_DIR/appInfo.cmake"

# Parse command line arguments
SKIP_VERSION_BUMP=false
for arg in "$@"; do
  if [ "$arg" == "--no-bump" ]; then
    SKIP_VERSION_BUMP=true
  fi
done

# Read current version from appInfo.cmake
MAJOR=$(grep "set(appVersionMajor" "$APP_INFO_FILE" | sed -E 's/.*appVersionMajor ([0-9]+).*/\1/')
MINOR=$(grep "set(appVersionMinor" "$APP_INFO_FILE" | sed -E 's/.*appVersionMinor ([0-9]+).*/\1/')
PATCH=$(grep "set(appVersionPatch" "$APP_INFO_FILE" | sed -E 's/.*appVersionPatch ([0-9]+).*/\1/')
SUFFIX=$(grep "set(appVersionSuffix" "$APP_INFO_FILE" | sed -E 's/.*appVersionSuffix "(.*)".*/\1/')

# Auto-bump patch version unless --no-bump is passed
if [ "$SKIP_VERSION_BUMP" = false ]; then
  echo "📝 Bumping patch version..."
  
  # Increment patch version
  NEW_PATCH=$((PATCH + 1))
  
  # Update appInfo.cmake
  sed -i.bak "s/set(appVersionPatch [0-9]*)/set(appVersionPatch $NEW_PATCH)/" "$APP_INFO_FILE"
  rm "${APP_INFO_FILE}.bak"
  
  # Use the new patch version for display
  PATCH=$NEW_PATCH
  echo "📌 Building version: ${MAJOR}.${MINOR}.${PATCH}${SUFFIX:+$SUFFIX}"
else
  echo "📌 Building version: ${MAJOR}.${MINOR}.${PATCH}${SUFFIX:+$SUFFIX} (no version bump)"
fi

# Clean previous build if requested
CLEAN_BUILD=false
for arg in "$@"; do
  if [ "$arg" == "--clean" ]; then
    CLEAN_BUILD=true
  fi
done

if [ "$CLEAN_BUILD" = true ]; then
    echo "🧹 Cleaning previous build..."
    rm -rf "$SCRIPT_DIR/build"
fi

# Create build directory if it doesn't exist
if [ ! -d "$SCRIPT_DIR/build" ]; then
    echo "📁 Creating build directory..."
    mkdir "$SCRIPT_DIR/build"
fi

cd "$SCRIPT_DIR/build"

# Configure with CMake if needed
if [ ! -f "CMakeCache.txt" ]; then
    echo "⚙️  Configuring with CMake..."
    if [[ "$OSTYPE" == "darwin"* ]]; then
        # macOS
        cmake .. -G Xcode
    else
        # Linux/Unix
        cmake .. -D CMAKE_BUILD_TYPE=Release
    fi
fi

# Build the rhubarb executable only (not the full package)
echo "🔧 Building rhubarb executable..."
cmake --build . --config Release --target rhubarb

# Check if build was successful
if [ ! -f "$SCRIPT_DIR/build/rhubarb/rhubarb" ]; then
    echo "❌ Build failed: rhubarb executable not found"
    exit 1
fi

echo "✅ Build successful!"

# Create target directory if it doesn't exist
if [ ! -d "$TARGET_DIR" ]; then
    echo "📁 Creating target directory: $TARGET_DIR"
    mkdir -p "$TARGET_DIR"
fi

# Copy the binary
echo "📦 Copying binary to visemes server..."
cp "$SCRIPT_DIR/build/rhubarb/rhubarb" "$TARGET_DIR/"

# Copy resource files if they exist and aren't already there
if [ -d "$SCRIPT_DIR/lib/pocketsphinx-rev13216/model" ] && [ ! -d "$TARGET_DIR/res" ]; then
    echo "📦 Copying resource files..."
    mkdir -p "$TARGET_DIR/res"
    cp -r "$SCRIPT_DIR/lib/pocketsphinx-rev13216/model" "$TARGET_DIR/res/"
fi

# Make the binary executable
chmod +x "$TARGET_DIR/rhubarb"

echo "✅ Rhubarb binary deployed to: $TARGET_DIR/rhubarb"

# Optional: Run a quick test
TEST_BUILD=false
for arg in "$@"; do
  if [ "$arg" == "--test" ]; then
    TEST_BUILD=true
  fi
done

if [ "$TEST_BUILD" = true ]; then
    echo "🧪 Running quick test..."
    VERSION_OUTPUT=$("$TARGET_DIR/rhubarb" --version 2>&1)
    echo "📌 Installed version: $VERSION_OUTPUT"
fi

echo "🎉 Build and deployment complete!"