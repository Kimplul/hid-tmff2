#!/bin/bash
# Build, reload, and verify T500RS driver modules
# Usage: sudo ./build_reload_verify.sh [insmod params]

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}T500RS Driver Build, Reload, and Verify Script${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then 
    echo -e "${RED}ERROR: Please run as root (use sudo)${NC}"
    exit 1
fi

# Resolve repo root and local .ko path
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$(cd "$SCRIPT_DIR"/.. && pwd)"
LOCAL_KO="$REPO_ROOT/hid-tmff-new.ko"

# Step 1: Build
echo -e "${YELLOW}[1/8] Building driver...${NC}"
if make -C "$REPO_ROOT" -j4; then
    echo -e "${GREEN}  ✓ Build succeeded${NC}"
else
    echo -e "${RED}  ✗ Build failed — aborting${NC}"
    exit 1
fi

if [ ! -f "$LOCAL_KO" ]; then
    echo -e "${RED}  ✗ Module not found after build: $LOCAL_KO${NC}"
    exit 1
fi

# Step 2: Clean up T500RS device and sysfs files
echo -e "${YELLOW}[2/8] Cleaning up T500RS device...${NC}"
# Find T500RS HID device in sysfs
T500RS_DEVICE=$(find /sys/bus/hid/devices/ -name "*044F:B65E*" 2>/dev/null | head -1)
if [ -n "$T500RS_DEVICE" ]; then
    DEVICE_NAME=$(basename "$T500RS_DEVICE")
    echo "  - Found HID device: $DEVICE_NAME"
    # Manually remove leftover sysfs files (from failed probe)
    echo "  - Removing leftover sysfs files..."
    for attr in gain range spring_level damper_level friction_level alternate_modes; do
        if [ -e "$T500RS_DEVICE/$attr" ]; then
            rm -f "$T500RS_DEVICE/$attr" 2>/dev/null || true
        fi
    done
    # Unbind from current driver if bound
    if [ -e "$T500RS_DEVICE/driver" ]; then
        CURRENT_DRIVER=$(basename $(readlink "$T500RS_DEVICE/driver"))
        echo "  - Unbinding from $CURRENT_DRIVER..."
        echo "$DEVICE_NAME" > "$T500RS_DEVICE/driver/unbind" 2>/dev/null || true
        sleep 0.5
    fi
    echo -e "${GREEN}  ✓ Device cleaned up${NC}"
else
    echo "  - T500RS HID device not found"
fi
echo ""

# Step 3: Unload modules
echo -e "${YELLOW}[3/8] Unloading modules...${NC}"
# Try to unload in reverse dependency order
if lsmod | grep -q "hid_tmff_new"; then
    echo "  - Removing hid_tmff_new..."
    modprobe -r hid_tmff_new || {
        echo -e "${RED}  WARNING: Failed to remove hid_tmff_new (may be in use)${NC}"
        echo "  Trying to force removal..."
        rmmod -f hid_tmff_new 2>/dev/null || true
    }
else
    echo "  - hid_tmff_new not loaded"
fi

if lsmod | grep -q "usb_tminit_new"; then
    echo "  - Removing usb_tminit_new..."
    modprobe -r usb_tminit_new || true
else
    echo "  - usb_tminit_new not loaded"
fi

if lsmod | grep -q "hid_tminit_new"; then
    echo "  - Removing hid_tminit_new..."
    modprobe -r hid_tminit_new || true
else
    echo "  - hid_tminit_new not loaded"
fi

echo -e "${GREEN}  ✓ Modules unloaded${NC}"
echo ""

# Step 4: Wait a moment for cleanup
echo -e "${YELLOW}[4/8] Waiting for cleanup...${NC}"
sleep 2
echo -e "${GREEN}  ✓ Done${NC}"
echo ""

# Step 5: Load init modules
echo -e "${YELLOW}[5/8] Loading init modules...${NC}"
modprobe hid_tminit_new && echo -e "${GREEN}  ✓ hid_tminit_new loaded${NC}" || echo -e "${RED}  ✗ Failed to load hid_tminit_new${NC}"
modprobe usb_tminit_new && echo -e "${GREEN}  ✓ usb_tminit_new loaded${NC}" || echo -e "${RED}  ✗ Failed to load usb_tminit_new${NC}"
echo ""

# Step 6: Wait for init to complete
echo -e "${YELLOW}[6/8] Waiting for device initialization...${NC}"
sleep 3
echo -e "${GREEN}  ✓ Done${NC}"
echo ""

# Step 7: Load main driver (from local build directory)
echo -e "${YELLOW}[7/8] Loading main driver from local build...${NC}"
if [ -f "$LOCAL_KO" ]; then
    echo "  - Loading $LOCAL_KO with params: $*"
    insmod "$LOCAL_KO" "$@" && echo -e "${GREEN}  ✓ hid_tmff_new loaded from local build${NC}" || {
        echo -e "${RED}  ✗ Failed to load hid_tmff_new${NC}"
        exit 1
    }
else
    echo -e "${RED}  ✗ Module not found: $LOCAL_KO${NC}"
    echo "  Run 'make' first to build the module"
    exit 1
fi
echo ""

# Step 8: Verify loaded module matches local build
echo -e "${YELLOW}[8/8] Verifying loaded module matches local build...${NC}"
LOCAL_SRCVER=$(modinfo -F srcversion "$LOCAL_KO" 2>/dev/null || echo "unknown")
LOADED_SRCVER=$(cat /sys/module/hid_tmff_new/srcversion 2>/dev/null || echo "none")
INSTALLED_PATH=$(modinfo -n hid_tmff_new 2>/dev/null || echo "unknown")

printf "  - Local .ko srcversion:   %s\n" "$LOCAL_SRCVER"
printf "  - Loaded module srcversion:%s\n" "$LOADED_SRCVER"
printf "  - Installed candidate:     %s\n" "$INSTALLED_PATH"

if [ "$LOCAL_SRCVER" != "$LOADED_SRCVER" ]; then
    echo -e "${RED}  ✗ Mismatch: loaded module is not the local build${NC}"
    exit 1
else
    echo -e "${GREEN}  ✓ Loaded module matches local build${NC}"
fi

echo ""

# Show T500RS logs
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}T500RS Device Status:${NC}"
echo -e "${BLUE}========================================${NC}"
if dmesg | grep -E "T500RS driver version|T500RS initialized successfully" | tail -n 10; then
    :
fi

# Check for errors
if dmesg | tail -50 | grep -qi "error\|fail\|bug\|oops"; then
    echo -e "${RED}⚠ WARNING: Errors detected in kernel log!${NC}"
    dmesg | tail -50 | grep -i "error\|fail\|bug\|oops" | tail -10
    echo ""
fi

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Build, reload, and verification complete!${NC}"
echo -e "${GREEN}========================================${NC}"

