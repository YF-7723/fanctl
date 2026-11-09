// FanCtl — QML 前端入口。界面在 qml/ 下，逻辑在 core/FanController。
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QLocalServer>
#include <QLocalSocket>
#include <QDir>
#include <QPalette>
#include <QQuickWindow>
#include <QTimer>
#include <QImage>

#include "fancontroller.h"
#include "native.h"

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setQuitOnLastWindowClosed(false);   // 关窗口不退出
    QCoreApplication::setOrganizationName("fanctl");
    QCoreApplication::setApplicationName("FanCtl");
    QQuickStyle::setStyle("Basic");

    // 深色主题：让 ComboBox / SpinBox / Button 这些内置控件跟着走
    QPalette pal;
    pal.setColor(QPalette::Window, QColor("#141416"));
    pal.setColor(QPalette::WindowText, QColor("#F2F2F7"));
    pal.setColor(QPalette::Base, QColor("#1D1D21"));
    pal.setColor(QPalette::AlternateBase, QColor("#26262B"));
    pal.setColor(QPalette::Text, QColor("#F2F2F7"));
    pal.setColor(QPalette::Button, QColor("#26262B"));
    pal.setColor(QPalette::ButtonText, QColor("#F2F2F7"));
    pal.setColor(QPalette::Highlight, QColor("#0A84FF"));
    pal.setColor(QPalette::HighlightedText, QColor("#FFFFFF"));
    pal.setColor(QPalette::Mid, QColor("#3A3A40"));
    pal.setColor(QPalette::Dark, QColor("#2C2C31"));
    app.setPalette(pal);

    // 单实例：已经在运行就唤出它的窗口，避免出现第二个菜单栏图标
    {
        QLocalSocket probe;
        probe.connectToServer("com.fanctl.app");
        if (probe.waitForConnected(200)) {
            probe.write("show\n");
            probe.flush();
            probe.waitForBytesWritten(200);
            return 0;
        }
    }

    FanController controller;

    NativeWindow native;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("controller", &controller);
    engine.rootContext()->setContextProperty("Native", &native);
    engine.load(QUrl("qrc:/qml/Main.qml"));
    if (engine.rootObjects().isEmpty()) return 1;

    QWindow *window = qobject_cast<QWindow *>(engine.rootObjects().value(0));
    if (window) native.attach(window);
    native.installTray();

    auto showWindow = [window] {
        if (!window) return;
        window->show();
        window->raise();
        window->requestActivate();
    };

    QLocalServer server;
    QLocalServer::removeServer("com.fanctl.app");
    server.listen("com.fanctl.app");
    QObject::connect(&server, &QLocalServer::newConnection, &app, [&server, showWindow] {
        while (QLocalSocket *c = server.nextPendingConnection()) {
            QObject::connect(c, &QLocalSocket::readyRead, c, [c, showWindow] {
                c->readAll();
                showWindow();
                c->disconnectFromServer();
            });
            QObject::connect(c, &QLocalSocket::disconnected, c, &QObject::deleteLater);
        }
    });

    auto updateTray = [&controller, &native] {
        QString text;
        if (!controller.connected()) {
            text = "离线";
        } else if (controller.mode() == QLatin1String("auto")) {
            text = "自动";
        } else {
            int rpm = 0;
            for (const QVariant &f : controller.fans())
                rpm = qMax(rpm, f.toMap().value("actual").toInt());
            text = rpm >= 1000 ? QString::number(rpm / 1000.0, 'f', 1) + "k"
                               : QString::number(rpm);
        }
        native.setTrayText(text);
    };

    QObject::connect(&controller, &FanController::changed, &app, updateTray);
    updateTray();

    QObject::connect(&native, &NativeWindow::trayCommand, &app, [&](int id) {
        switch (id) {
        case 0: showWindow(); break;
        case 1: controller.setAutomatic(); break;
        case 4:
            controller.quitCleanly();
            QCoreApplication::exit(0);   // quit() 在某些情况下不会真正退出事件循环
            break;
        default: break;
        }
    });

    QObject::connect(&app, &QCoreApplication::aboutToQuit, &controller,
                     &FanController::quitCleanly);

    // 自检用：--screenshot <path> 渲染一帧后退出
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--screenshot") {
            const QString path = argv[i + 1];
            QTimer::singleShot(1800, &app, [&engine, path] {
                if (auto *w = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0)))
                    w->grabWindow().save(path);
                QCoreApplication::exit(0);
            });
        }
    }
    return app.exec();
}
