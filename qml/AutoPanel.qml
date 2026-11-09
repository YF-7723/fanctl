import QtQuick
import QtQuick.Layouts

// 自动模式：只读展示系统当前把风扇放在什么转速
ColumnLayout {
    id: root

    property Theme theme: Theme {}
    property var fans: []

    spacing: 18

    RowLayout {
        Layout.fillWidth: true
        Text {
            text: controller.english ? "Fan speed is managed by macOS" : "由 macOS 管理风扇转速"
            color: root.theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
        Item { Layout.fillWidth: true }
        Rectangle {
            radius: 9
            implicitWidth: 62
            implicitHeight: 20
            color: root.theme.accentSoft
            Text {
                anchors.centerIn: parent
                text: controller.english ? "Read-only" : "只读"
                color: root.theme.accent
                font.pixelSize: 10
            }
        }
    }

    Repeater {
        model: root.fans.length

        delegate: ColumnLayout {
            Layout.fillWidth: true
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: (controller.english ? "Fan " : "风扇 ") + index
                    color: root.theme.sub
                    font.pixelSize: 12
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: index < root.fans.length ? root.fans[index].actual : 0
                    color: root.theme.text
                    font.pixelSize: 34
                    font.weight: Font.Light
                }
                Text {
                    text: "rpm"
                    color: root.theme.sub
                    font.pixelSize: 12
                    Layout.alignment: Qt.AlignBottom
                    Layout.bottomMargin: 6
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 6
                radius: 3
                color: root.theme.track

                Rectangle {
                    height: parent.height
                    radius: 3
                    color: root.theme.accent
                    width: {
                        if (index >= root.fans.length) return 0;
                        const f = root.fans[index];
                        const span = Math.max(1, f.max - f.min);
                        return parent.width * Math.max(0, Math.min(1, (f.actual - f.min) / span));
                    }
                    Behavior on width { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
                }
            }

            Text {
                Layout.fillWidth: true
                text: index < root.fans.length
                      ? (controller.english ? "Range " : "可调范围 ")
                        + root.fans[index].min + " – " + root.fans[index].max + " rpm"
                      : ""
                color: root.theme.sub
                font.pixelSize: 10
            }
        }
    }

    Item { Layout.fillHeight: true }

    Rectangle {
        Layout.fillWidth: true
        height: 1
        color: root.theme.line
    }

    Text {
        Layout.fillWidth: true
        text: controller.english ? "Switch to Manual to set a fixed speed, or Curve to follow temperature."
                                             : "想自己定转速，切到「手动」；想按温度自动调，切到「曲线」。"
        color: root.theme.sub
        font.pixelSize: 11
        wrapMode: Text.WordWrap
    }
}
