#!/bin/bash
# 打包成自包含 .app：把 Qt 框架 / 插件 / QML 模块全部塞进 bundle
set -euo pipefail
cd "$(dirname "$0")"

QT_BIN="${QT_PREFIX:-/opt/homebrew/opt/qt6}"
QT_LIB="$QT_BIN/lib"
QT_QML="${QT_QML_DIR:-/opt/homebrew/share/qt/qml}"
TC=/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin
APP=dist/FanCtl.app

./build.sh

rm -rf dist && mkdir -p dist
cp -R build/FanCtl.app dist/

echo "==> macdeployqt（会因 Xcode 许可缺几个框架，后面补齐）"
PATH="$TC:$PATH" "$QT_BIN/bin/macdeployqt" "$APP" -always-overwrite >/tmp/macdeployqt.log 2>&1 || true

echo "==> 清理残留的 Homebrew rpath"
"$TC/install_name_tool" -delete_rpath "$QT_BIN/lib" "$APP/Contents/MacOS/FanCtl" 2>/dev/null || true

echo "==> 拷贝 QML 模块"
mkdir -p "$APP/Contents/Resources/qml"
for m in QtQuick QtQml QtCore; do
    [ -d "$QT_QML/$m" ] && cp -R "$QT_QML/$m" "$APP/Contents/Resources/qml/"
done

echo "==> 裁剪用不到的 QML 模块"
QMLROOT="$APP/Contents/Resources/qml/QtQuick"
for m in Dialogs Effects LocalStorage NativeStyle Particles Pdf Scene2D Scene3D \
         Shapes Timeline VirtualKeyboard tooling; do
    rm -rf "$QMLROOT/$m"
done
for st in Fusion Imagine Material Universal designer iOS; do
    rm -rf "$QMLROOT/Controls/$st"
done

echo "==> 补齐运行时缺的 Qt 框架"
codesign --force --deep --sign - "$APP" >/dev/null 2>&1 || true
python3 tools/bundle_qt.py "$QT_LIB" "$APP"

# macdeployqt 会顺带拷进两个用不到的框架，删掉能省 55 MB
rm -rf "$APP/Contents/Frameworks/QtPdf.framework" \
       "$APP/Contents/Frameworks/QtWidgets.framework"

echo "==> 签名"
codesign --force --deep --sign - "$APP" >/dev/null 2>&1 || true

echo
echo "done: $APP ($(du -sh "$APP" | cut -f1))"
echo "自检：$(timeout 6 "$APP/Contents/MacOS/FanCtl" --screenshot /tmp/_pkg.png 2>&1 | grep -c ERROR || true) 个错误"
