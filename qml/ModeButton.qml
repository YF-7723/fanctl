import QtQuick

// 顶部的模式按钮：选中填充强调色，按下缩小，悬停变亮
Rectangle {
    id: root

    property Theme theme: Theme {}
    property string text: ""
    property bool checked: false
    signal clicked()

    implicitHeight: 38
    implicitWidth: 104
    radius: 12

    color: checked ? theme.accent
                   : (press.containsMouse ? theme.cardAlt
                                          : theme.alpha(theme.cardAlt, 0))
    border.width: 1
    border.color: checked ? theme.accent : theme.line
    scale: press.pressed ? 0.96 : 1.0

    Behavior on color { ColorAnimation { duration: theme.duration } }
    Behavior on scale { NumberAnimation { duration: 90; easing.type: Easing.OutCubic } }

    Text {
        anchors.centerIn: parent
        text: root.text
        color: root.checked ? "#FFFFFF" : root.theme.text
        font.pixelSize: 13
        font.weight: root.checked ? Font.DemiBold : Font.Normal
    }

    MouseArea {
        id: press
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
