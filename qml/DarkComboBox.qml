import QtQuick
import QtQuick.Controls

// 完全自绘的下拉框：不受 Qt 样式调色板影响，深色配色可控
Item {
    id: ctl

    property Theme theme: Theme {}
    property var model: []
    property string textRole: "display"
    property string placeholder: ""
    property int currentIndex: 0

    readonly property var current: (currentIndex >= 0 && currentIndex < model.length)
                                   ? model[currentIndex] : null
    readonly property string currentKey: (current && current.key !== undefined)
                                          ? String(current.key) : ""
    readonly property string currentText: {
        if (!current || currentIndex < 0) return ctl.placeholder;
        const v = current[ctl.textRole];
        if (v !== undefined && v !== null) return String(v);
        return current.display !== undefined ? String(current.display) : "";
    }

    signal activated(int index)

    implicitHeight: 34
    implicitWidth: 200

    function syncToKey(key) {
        for (let i = 0; i < ctl.model.length; ++i) {
            if (ctl.model[i].key === key) {
                ctl.currentIndex = i;
                return;
            }
        }
    }

    Rectangle {
        id: box
        anchors.fill: parent
        radius: 8
        color: ctl.theme.cardAlt
        border.width: 1
        border.color: pop.visible ? ctl.theme.accent : ctl.theme.line

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.right: chevron.left
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: ctl.currentText
            color: ctl.theme.text
            font.pixelSize: 13
            elide: Text.ElideRight
        }

        Text {
            id: chevron
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: "⌄"
            color: ctl.theme.sub
            font.pixelSize: 15
            rotation: pop.visible ? 180 : 0
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: pop.visible ? pop.close() : pop.open()
        }
    }

    Popup {
        id: pop
        y: box.height + 4
        width: ctl.width
        height: Math.min(300, list.contentHeight + 10)
        padding: 5
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: ctl.theme.card
            radius: 8
            border.width: 1
            border.color: ctl.theme.line
        }

        contentItem: ListView {
            id: list
            clip: true
            model: ctl.model
            boundsBehavior: Flickable.StopAtBounds
            currentIndex: ctl.currentIndex

            delegate: Rectangle {
                width: list.width
                height: 30
                radius: 6
                color: (hover.containsMouse || index === ctl.currentIndex) ? ctl.theme.cardAlt : "transparent"

                Text {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    verticalAlignment: Text.AlignVCenter
                    text: modelData[ctl.textRole] !== undefined ? modelData[ctl.textRole]
                                                                : modelData.display
                    color: ctl.theme.text
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }

                MouseArea {
                    id: hover
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        ctl.currentIndex = index;
                        ctl.activated(index);
                        pop.close();
                    }
                }
            }
        }
    }
}
