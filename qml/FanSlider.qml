import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 手动模式下的单台风扇：滑杆 + 数字输入框
// 滑杆 = 目标转速，白色刻度 = 实际转速
Item {
    id: card

    property Theme theme: Theme {}
    property var fan: null
    property int desired: 0

    // 拖动中 / 刚松手：此时不接受后台刷新回写，避免手柄闪回
    property bool adjusting: false
    property bool initialized: false

    signal dragStarted()
    signal dragFinished(int index, int rpm)
    signal rpmEntered(int index, int rpm)

    implicitHeight: 118

    function clampValue(v) {
        const lo = slider.from, hi = slider.to;
        return Math.max(lo, Math.min(hi, Math.round(v)));
    }

    // 输入框 / 回车提交
    function submit(v) {
        if (!fan) return;
        const clamped = clampValue(v);
        card.desired = clamped;
        slider.value = clamped;
        card.adjusting = true;
        settle.restart();
        card.rpmEntered(card.fan.index, clamped);
    }

    // 取一个可信的数值：后台偶尔会给出 undefined/NaN，此时宁可不动
    function safeTarget() {
        if (!fan) return NaN;
        const t = Number(fan.target);
        if (isFinite(t) && t > 0) return t;
        const a = Number(fan.actual);
        return isFinite(a) ? a : NaN;
    }

    // 只有初始化、或面板重新出现时才从后台取一次值
    function resetToDaemon() {
        if (!fan || slider.pressed) return;
        const t = safeTarget();
        if (!isFinite(t)) return;
        const c = clampValue(t);
        desired = c;
        slider.value = c;
        adjusting = false;
        settle.stop();
        initialized = true;
    }

    Component.onCompleted: {
        if (fan) resetToDaemon();
    }

    // 手动模式下滑杆由用户独占：后台刷新不再回写，从根上消除松手后的闪回
    onFanChanged: {
        if (!card.fan) return;
        if (!card.initialized) {
            resetToDaemon();
            return;
        }
        if (card.adjusting && !slider.pressed &&
                Math.abs(Number(card.fan.target) - card.desired) <= 2) {
            card.adjusting = false;
            settle.stop();
        }
    }

    Timer {
        id: settle
        interval: 900
        onTriggered: card.adjusting = false
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                text: (controller.english ? "Fan " : "风扇 ") + (card.fan ? card.fan.index : "")
                color: card.theme.text
                font.pixelSize: 13
            }

            Rectangle {
                radius: 5
                implicitWidth: 36
                implicitHeight: 18
                color: card.fan && card.fan.forced ? card.theme.okSoft : card.theme.cardAlt
                Text {
                    anchors.centerIn: parent
                    text: card.fan && card.fan.forced
                          ? (controller.english ? "Forced" : "强制")
                          : (controller.english ? "Auto" : "自动")
                    color: card.fan && card.fan.forced ? card.theme.ok : card.theme.sub
                    font.pixelSize: 10
                }
            }

            Item { Layout.fillWidth: true }

            Row {
                spacing: 5
                Rectangle {
                    width: 6; height: 6; radius: 3
                    anchors.verticalCenter: parent.verticalCenter
                    color: card.theme.sub
                }
                Text {
                    text: (controller.english ? "Actual " : "实际 ") + (card.fan ? card.fan.actual : 0)
                    color: card.theme.sub
                    font.pixelSize: 11
                }
            }

            // 直接输入目标转速
            Rectangle {
                implicitWidth: 88
                implicitHeight: 30
                radius: 8
                color: card.theme.cardAlt
                border.width: 1
                border.color: rpmInput.activeFocus ? card.theme.accent : card.theme.line

                MouseArea {
                    anchors.fill: parent
                    onClicked: rpmInput.forceActiveFocus()
                }

                TextField {
                    id: rpmInput
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 6
                    verticalAlignment: TextInput.AlignVCenter
                    horizontalAlignment: TextInput.AlignRight
                    color: card.theme.text
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    selectByMouse: true
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: IntValidator { bottom: Math.round(slider.from); top: Math.round(slider.to) }
                    text: card.desired
                    background: null

                    onActiveFocusChanged: {
                        if (activeFocus) selectAll();
                        else if (text.length > 0) {
                            const v = parseInt(text);
                            if (isFinite(v) && v !== card.desired) card.submit(v);
                        }
                    }
                    onAccepted: {
                        card.submit(parseInt(text));
                        focus = false;
                    }

                    Keys.onEscapePressed: {
                        text = card.desired;
                        focus = false;
                    }
                }
            }

            Text {
                text: "rpm"
                color: card.theme.sub
                font.pixelSize: 11
            }
        }

        Slider {
            id: slider
            Layout.fillWidth: true
            implicitHeight: 26
            from: card.fan ? card.fan.min : 0
            to: card.fan ? Math.max(card.fan.max, card.fan.min + 1) : 6000
            stepSize: 25
            // 不绑定 desired：拖动的值只由用户产生，避免绑定回写

            onMoved: card.desired = Math.round(value)
            onPressedChanged: {
                if (pressed) {
                    card.adjusting = true;
                    card.dragStarted();
                } else {
                    card.desired = Math.round(value);
                    card.adjusting = true;
                    settle.restart();
                    card.dragFinished(card.fan.index, Math.round(value));
                }
            }

            background: Rectangle {
                x: slider.leftPadding
                y: slider.topPadding + slider.availableHeight / 2 - height / 2
                width: slider.availableWidth
                height: 5
                radius: 2.5
                color: card.theme.track

                Rectangle {
                    width: slider.visualPosition * parent.width
                    height: parent.height
                    radius: 2.5
                    color: card.theme.accent
                }

                Rectangle {
                    visible: card.fan && card.fan.actual > 0
                    x: {
                        if (!card.fan) return 0;
                        const span = Math.max(1, slider.to - slider.from);
                        return Math.max(0, Math.min(parent.width - width,
                            (card.fan.actual - slider.from) / span * parent.width));
                    }
                    y: -3
                    width: 2
                    height: parent.height + 6
                    radius: 1
                    color: card.theme.dark ? "#F5F5F7" : "#3A3A40"
                    opacity: 0.5
                }
            }

            handle: Rectangle {
                x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
                y: slider.topPadding + slider.availableHeight / 2 - height / 2
                width: 20
                height: 20
                radius: 10
                color: "#FFFFFF"
                border.width: 3
                border.color: card.theme.accent
                scale: slider.pressed ? 1.12 : 1.0
                Behavior on scale { NumberAnimation { duration: 90 } }
            }
        }
    }

    // 后台刷新时同步输入框（正在编辑则不动）
    Connections {
        target: card
        function onDesiredChanged() {
            if (!rpmInput.activeFocus) rpmInput.text = card.desired;
        }
    }
}
