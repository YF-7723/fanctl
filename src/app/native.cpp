#include "native.h"

#include <QWindow>

void macHideTitlebar(QWindow *window);
void macInstallStatusItem(void (*cb)(int));
void macSetStatusTitle(const char *utf8);
void macSetAppearance(QWindow *window, bool dark);
bool macStartDrag(QWindow *window);

void NativeWindow::attach(QWindow *window) {
    window_ = window;
    if (!window_) return;
    macHideTitlebar(window_);
    // 窗口重新显示时（隐藏后再打开）把标题栏设置再应用一次
    connect(window_, &QWindow::visibleChanged, this, [this](bool visible) {
        if (visible) macHideTitlebar(window_);
    });
}

void NativeWindow::setDark(bool dark) {
    if (window_) macSetAppearance(window_, dark);
}

void NativeWindow::startDrag() {
    if (!window_) return;
    // 自己处理：先激活窗口，再用当前鼠标事件发起系统拖拽。
    // 直接用 startSystemMove() 时，窗口未激活的那一下会被吞掉。
    if (!macStartDrag(window_)) window_->startSystemMove();
}

namespace {
NativeWindow *g_native = nullptr;
void trayTrampoline(int id) {
    if (g_native) g_native->handleTray(id);
}
}  // namespace

void NativeWindow::installTray() {
    g_native = this;
    macInstallStatusItem(trayTrampoline);
}

void NativeWindow::setTrayText(const QString &text) {
    macSetStatusTitle(text.toUtf8().constData());
}

void NativeWindow::handleTray(int id) { emit trayCommand(id); }
