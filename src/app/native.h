#pragma once

#include <QObject>

class QWindow;

// 把 QML 窗口的属性同步给原生 NSWindow（隐藏标题栏、跟随主题外观）
class NativeWindow : public QObject {
    Q_OBJECT
public:
    explicit NativeWindow(QObject *parent = nullptr) : QObject(parent) {}

    void attach(QWindow *window);

    Q_INVOKABLE void setDark(bool dark);
    Q_INVOKABLE void installTray();
    Q_INVOKABLE void setTrayText(const QString &text);
    void handleTray(int id);
    Q_INVOKABLE void startDrag();

signals:
    void trayCommand(int id);

private:
    QWindow *window_ = nullptr;
};
