import QtQuick

// 圆形图标按钮（主题切换等）
Rectangle {
    id: root

    property Theme theme: Theme {}
    property string glyph: ""
    signal clicked()

    implicitWidth: 34
    implicitHeight: 34
    radius: 17
    color: press.containsMouse ? theme.cardAlt : theme.alpha(theme.cardAlt, 0)
    border.width: 1
    border.color: theme.line
    scale: press.pressed ? 0.92 : 1.0

    Behavior on color { ColorAnimation { duration: theme.duration } }
    Behavior on scale { NumberAnimation { duration: 90 } }

    Text {
        anchors.centerIn: parent
        text: root.glyph
        color: root.theme.text
        font.pixelSize: 15
    }

    MouseArea {
        id: press
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
