#!/bin/bash
# Removes FanCtl and hands the fans back to macOS.
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
    exec sudo -E "$0" "$@"
fi

echo "==> restoring automatic fan control"
/usr/local/bin/fanctl auto 2>/dev/null || true

echo "==> stopping daemon"
launchctl bootout system/com.fanctl.daemon 2>/dev/null || \
    launchctl unload -w /Library/LaunchDaemons/com.fanctl.daemon.plist 2>/dev/null || true

rm -f /Library/LaunchDaemons/com.fanctl.daemon.plist
rm -f /usr/local/bin/fanctl
rm -rf /usr/local/libexec/fanctl
rm -rf /Applications/FanCtl.app
rm -rf "/Library/Application Support/FanCtl"

TARGET_USER="${SUDO_USER:-}"
if [ -n "$TARGET_USER" ] && [ "$TARGET_USER" != "root" ]; then
    HOME_DIR=$(dscl . -read "/Users/$TARGET_USER" NFSHomeDirectory | awk '{print $2}')
    rm -f "$HOME_DIR/Library/LaunchAgents/com.fanctl.app.plist"
fi

echo "FanCtl removed. Fans are back under macOS control."
