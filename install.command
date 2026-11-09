#!/bin/bash
# Installs the root daemon + the menu bar app.  Double-click it, or run:
#   ./install.command
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
    echo "FanCtl needs administrator rights to install the fan daemon."
    exec sudo -E "$0" "$@"
fi

SRC="$(cd "$(dirname "$0")" && pwd)"
DEST=/usr/local/libexec/fanctl
APP=/Applications/FanCtl.app
PLIST=/Library/LaunchDaemons/com.fanctl.daemon.plist

if [ ! -x "$SRC/build/fanctld" ] || [ ! -d "$SRC/build/FanCtl.app" ]; then
    echo "error: build first:  ./build.sh" >&2
    exit 1
fi

echo "==> installing binaries to $DEST"
install -d -m 755 "$DEST"
install -m 755 "$SRC/build/fanctld" "$DEST/fanctld"
install -m 755 "$SRC/build/fanctl"  "$DEST/fanctl"
ln -sf "$DEST/fanctl" /usr/local/bin/fanctl

echo "==> state directory"
install -d -m 755 "/Library/Application Support/FanCtl"

echo "==> launch daemon"
cat > "$PLIST" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.fanctl.daemon</string>
    <key>ProgramArguments</key>
    <array>
        <string>/usr/local/libexec/fanctl/fanctld</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
    <key>StandardOutPath</key>
    <string>/var/log/fanctl.log</string>
    <key>StandardErrorPath</key>
    <string>/var/log/fanctl.log</string>
</dict>
</plist>
PLIST
chmod 644 "$PLIST"
chown root:wheel "$PLIST"

# bootout 是异步的：立刻 bootstrap 会撞上 "Operation already in progress"，
# 所以先等旧实例真的消失再加载，并且不再吞掉错误信息。
launchctl bootout system/com.fanctl.daemon 2>/dev/null || true
for _ in $(seq 1 50); do
    launchctl print system/com.fanctl.daemon >/dev/null 2>&1 || break
    sleep 0.1
done

if ! OUT=$(launchctl bootstrap system "$PLIST" 2>&1); then
    echo "    bootstrap 失败：$OUT"
    launchctl enable system/com.fanctl.daemon 2>/dev/null || true
    launchctl load -w "$PLIST" 2>&1 || true
fi

for _ in $(seq 1 50); do
    [ -S /var/run/fanctl.sock ] && break
    sleep 0.1
done
if [ -S /var/run/fanctl.sock ]; then
    echo "    socket 已就绪"
else
    echo "    ！socket 未出现，最近日志：" >&2
    tail -5 /var/log/fanctl.log 2>/dev/null >&2 || true
fi

echo "==> installing app"
rm -rf "$APP"
cp -R "$SRC/build/FanCtl.app" "$APP"
codesign --force --deep --sign - "$APP" >/dev/null 2>&1 || true

echo "==> login item for ${SUDO_USER:-$USER}"
TARGET_USER="${SUDO_USER:-}"
if [ -n "$TARGET_USER" ] && [ "$TARGET_USER" != "root" ]; then
    HOME_DIR=$(dscl . -read "/Users/$TARGET_USER" NFSHomeDirectory | awk '{print $2}')
    AGENT_DIR="$HOME_DIR/Library/LaunchAgents"
    mkdir -p "$AGENT_DIR"
    cat > "$AGENT_DIR/com.fanctl.app.plist" <<'AGENT'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.fanctl.app</string>
    <key>ProgramArguments</key>
    <array>
        <string>/Applications/FanCtl.app/Contents/MacOS/FanCtl</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
</dict>
</plist>
AGENT
    chown "$TARGET_USER" "$AGENT_DIR/com.fanctl.app.plist"
fi

sleep 0.5
if /usr/local/bin/fanctl ping >/dev/null 2>&1; then
    echo
    echo "FanCtl installed. Daemon is running."
    /usr/local/bin/fanctl status || true
else
    echo
    echo "warning: daemon did not answer yet — check /var/log/fanctl.log" >&2
fi
echo
echo "Open /Applications/FanCtl.app, or run:  fanctl status"
