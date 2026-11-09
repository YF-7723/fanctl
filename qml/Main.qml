import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore

Window {
    id: root

    // 固定尺寸：不允许缩放，也不允许全屏
    readonly property int fixedWidth: 920
    readonly property int fixedHeight: 560

    width: fixedWidth
    height: fixedHeight
    minimumWidth: fixedWidth
    maximumWidth: fixedWidth
    minimumHeight: fixedHeight
    maximumHeight: fixedHeight
    visible: true
    title: "FanCtl 风扇控制"
    color: rootTheme.bg

    Theme { id: rootTheme }



    Settings {
        category: "appearance"
        property alias darkMode: rootTheme.dark
    }

    readonly property int modeIndex: controller.mode === "manual" ? 1
                                     : (controller.mode === "curve" ? 2 : 0)

    property string toastText: ""

    function averageTarget() {
        if (controller.fans.length === 0) return 2000;
        let sum = 0;
        for (let i = 0; i < controller.fans.length; ++i) {
            const f = controller.fans[i];
            sum += (f.target > 0 ? f.target : f.actual);
        }
        return Math.round(sum / controller.fans.length);
    }

    Connections {
        target: controller
        function onToast(message) {
            root.toastText = message;
            toastTimer.restart();
        }
    }

    Timer {
        id: toastTimer
        interval: 3200
        onTriggered: root.toastText = ""
    }

    // 关闭窗口只隐藏，App 继续在菜单栏运行
    onClosing: (close) => {
        close.accepted = false;
        root.hide();
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: rootTheme.bgTop }
            GradientStop { position: 1.0; color: rootTheme.bg }
        }
    }

    // 标题栏隐藏后，内容延伸到顶部；留出红绿灯的位置
    Component.onCompleted: Native.setDark(rootTheme.dark)

    Connections {
        target: rootTheme
        function onDarkChanged() { Native.setDark(rootTheme.dark); }
    }

    // 点任意空白处让输入框失焦（否则光标会一直在里面闪）
    Item {
        id: focusSink
        anchors.fill: parent
        focus: true
    }

    MouseArea {
        anchors.fill: parent
        onPressed: focusSink.forceActiveFocus()
    }

    // 标题栏隐藏后，自己接管顶部拖拽区。
    // 放在内容下方：空白处按下即拖动，按钮/滑杆在上层自己处理点击。
    MouseArea {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 128
        acceptedButtons: Qt.LeftButton
        preventStealing: true
        onPressed: {
            focusSink.forceActiveFocus();
            Native.startDrag();
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        anchors.bottomMargin: 20
        anchors.topMargin: 44
        spacing: 16

        // ------------------------------------------------------------ 左：标题 + 模式 + 功能区
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Text {
                    text: controller.english ? "Fan Control" : "风扇控制"
                    color: rootTheme.text
                    font.pixelSize: 22
                    font.weight: Font.DemiBold
                }

                Rectangle {
                    radius: 10
                    implicitHeight: 20
                    implicitWidth: statusLabel.implicitWidth + 18
                    color: controller.outdated ? rootTheme.accentSoft
                         : (controller.connected ? rootTheme.okSoft : rootTheme.accentSoft)
                    Text {
                        id: statusLabel
                        anchors.centerIn: parent
                        text: controller.outdated
                              ? (controller.english ? "Update helper" : "组件需更新")
                              : (controller.connected
                                 ? (controller.english ? "Connected" : "已连接")
                                 : (controller.english ? "Offline" : "未连接"))
                        color: controller.outdated ? rootTheme.warn
                             : (controller.connected ? rootTheme.ok : rootTheme.warn)
                        font.pixelSize: 10
                    }
                }

                Item { Layout.fillWidth: true }

                ModeButton {
                    theme: rootTheme
                    text: controller.english ? "中" : "EN"
                    implicitWidth: 40
                    implicitHeight: 34
                    onClicked: controller.setEnglish(!controller.english)
                }

                IconButton {
                    theme: rootTheme
                    glyph: rootTheme.dark ? "☀" : "☾"
                    onClicked: rootTheme.dark = !rootTheme.dark
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                ModeButton {
                    theme: rootTheme
                    text: controller.english ? "Auto" : "自动"
                    checked: root.modeIndex === 0
                    onClicked: controller.setAutomatic()
                }
                ModeButton {
                    theme: rootTheme
                    text: controller.english ? "Manual" : "手动"
                    checked: root.modeIndex === 1
                    onClicked: controller.setManual(root.averageTarget())
                }
                ModeButton {
                    theme: rootTheme
                    text: controller.english ? "Curve" : "曲线"
                    checked: root.modeIndex === 2
                    onClicked: controller.enterCurveMode()
                }

                Item { Layout.fillWidth: true }
            }

            Card {
                id: functionCard
                Layout.fillWidth: true
                Layout.fillHeight: true
                theme: rootTheme

                StackLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    currentIndex: root.modeIndex

                    AutoPanel { theme: rootTheme; fans: controller.fans }
                    ManualPanel { theme: rootTheme }
                    CurvePanel { theme: rootTheme }
                }
            }
        }

        // ------------------------------------------------------------ 右：固定温度栏
        TempSidebar {
            Layout.preferredWidth: 272
            Layout.fillHeight: true
            theme: rootTheme
        }
    }

    // ---------------------------------------------------------------- 未连接
    Rectangle {
        anchors.fill: parent
        visible: !controller.connected
        color: rootTheme.dark ? "#F51A1611" : "#F5FBF6EF"

        Card {
            anchors.centerIn: parent
            width: Math.min(parent.width - 120, 420)
            height: onboarding.implicitHeight + 48
            theme: rootTheme

            ColumnLayout {
                id: onboarding
                anchors.fill: parent
                anchors.margins: 24
                spacing: 14

                Text {
                    Layout.fillWidth: true
                    text: controller.installed
                          ? (controller.english ? "Helper isn't running" : "后台组件没有运行")
                          : (controller.english ? "Welcome to FanCtl" : "欢迎使用 FanCtl")
                    color: rootTheme.text
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                }

                Text {
                    Layout.fillWidth: true
                    text: controller.installed
                          ? (controller.english
                             ? "Fan control needs a small background helper. Start it with one administrator approval."
                             : "风扇控制需要一个小型后台组件。点下面的按钮启动它，需要一次管理员授权。")
                          : (controller.english
                             ? "FanCtl installs a small background helper to write fan speeds. One approval, then it just works."
                             : "FanCtl 需要安装一个小型后台组件来写入风扇转速。只需要授权一次，之后打开 App 就能直接用。")
                    color: rootTheme.sub
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    radius: 12
                    color: rootTheme.accent
                    scale: enableArea.pressed ? 0.98 : 1.0

                    Behavior on scale { NumberAnimation { duration: 90 } }

                    Text {
                        anchors.centerIn: parent
                        text: controller.busy
                              ? (controller.english ? "Working…" : "正在处理…")
                              : (controller.installed
                                 ? (controller.english ? "Start" : "启动")
                                 : (controller.english ? "Enable fan control" : "启用风扇控制"))
                        color: "#FFFFFF"
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }

                    MouseArea {
                        id: enableArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        enabled: !controller.busy
                        onClicked: controller.enableControl()
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: controller.english
                          ? "Closing the window keeps FanCtl in the menu bar. Choose Quit there to exit and restore system control."
                          : "关闭窗口后仍在菜单栏运行；点菜单栏图标选「退出」才会结束并恢复系统控制。"
                    color: rootTheme.sub
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }

    // ---------------------------------------------------------------- 提示
    Rectangle {
        visible: root.toastText.length > 0
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 22
        width: Math.min(parent.width - 80, toastLabel.implicitWidth + 36)
        height: 36
        radius: 18
        color: rootTheme.dark ? "#F22C2620" : "#F2FFFCF7"
        border.width: 1
        border.color: rootTheme.line

        Text {
            id: toastLabel
            anchors.centerIn: parent
            text: root.toastText
            color: rootTheme.text
            font.pixelSize: 12
        }
    }
}
