import QtQuick
import QtQuick.Layouts

// 右侧固定温度栏：不随模式变化
Card {
    id: root

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: controller.english ? "Temperature" : "温度"
                color: root.theme.text
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            Item { Layout.fillWidth: true }
            Text {
                text: controller.highlights.length + (controller.english ? " items" : " 项")
                color: root.theme.sub
                font.pixelSize: 10
            }
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: controller.highlights
            spacing: 10
            boundsBehavior: Flickable.StopAtBounds

            delegate: ColumnLayout {
                width: ListView.view.width
                spacing: 5

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: modelData.label
                        color: root.theme.sub
                        font.pixelSize: 11
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Text {
                        text: modelData.value.toFixed(1) + "°"
                        color: root.theme.text
                        font.pixelSize: 14
                        font.weight: Font.Medium
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 4
                    radius: 2
                    color: root.theme.track

                    Rectangle {
                        height: parent.height
                        radius: 2
                        color: modelData.value > 85 ? root.theme.warn : root.theme.accent
                        width: parent.width * Math.max(0, Math.min(1, modelData.value / 110))
                        Behavior on width { NumberAnimation { duration: 400; easing.type: Easing.OutCubic } }
                    }
                }
            }
        }
    }
}
