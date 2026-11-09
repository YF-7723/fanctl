#!/bin/bash
# 重新加载风扇守护进程（已安装但没在跑时用这个，不用重装整个 App）
set -uo pipefail

if [ "$(id -u)" -ne 0 ]; then
    exec sudo -E "$0" "$@"
fi

PLIST=/Library/LaunchDaemons/com.fanctl.daemon.plist
BIN=/usr/local/libexec/fanctl/fanctld

if [ ! -f "$PLIST" ] || [ ! -x "$BIN" ]; then
    echo "还没有安装，请先运行 install.command" >&2
    exit 1
fi

echo "==> 停止旧实例"
launchctl bootout system/com.fanctl.daemon 2>&1 || true
for _ in $(seq 1 50); do
    launchctl print system/com.fanctl.daemon >/dev/null 2>&1 || break
    sleep 0.1
done

echo "==> 启动"
if ! OUT=$(launchctl bootstrap system "$PLIST" 2>&1); then
    echo "    bootstrap: $OUT"
    launchctl enable system/com.fanctl.daemon 2>&1 || true
    launchctl load -w "$PLIST" 2>&1 || true
fi

for _ in $(seq 1 50); do
    [ -S /var/run/fanctl.sock ] && break
    sleep 0.1
done
sleep 0.3

echo "==> launchd 状态"
launchctl print system/com.fanctl.daemon 2>&1 | head -10

echo "==> 自检"
if /usr/local/libexec/fanctl/fanctl status 2>&1 | head -6; then
    echo
    echo "守护进程已运行。打开 FanCtl.app 点「验证」即可。"
else
    echo "守护进程没有响应，最近日志：" >&2
    tail -10 /var/log/fanctl.log 2>/dev/null >&2
    exit 1
fi
