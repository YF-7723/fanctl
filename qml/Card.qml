import QtQuick

// 圆角卡片：两层柔和投影 + 渐变表面 + 悬停上浮
Item {
    id: root

    property Theme theme: Theme {}
    property bool hoverable: false
    readonly property alias hovered: hover.hovered

    default property alias content: body.data

    readonly property int lift: hoverable && hovered ? 3 : 0

    // 远处散光
    Rectangle {
        anchors.fill: body
        anchors.topMargin: 12 + root.lift
        anchors.leftMargin: 4
        anchors.rightMargin: 4
        radius: root.theme.radiusCard + 6
        color: root.theme.shadowFar
        opacity: root.hoverable && root.hovered ? 1.0 : 0.8
        Behavior on opacity { NumberAnimation { duration: root.theme.duration } }
        Behavior on anchors.topMargin { NumberAnimation { duration: root.theme.duration } }
    }

    // 近处投影
    Rectangle {
        anchors.fill: body
        anchors.topMargin: 4 + root.lift
        radius: root.theme.radiusCard
        color: root.theme.shadowNear
        opacity: 0.9
        Behavior on anchors.topMargin { NumberAnimation { duration: root.theme.duration } }
    }

    // 顶部 1px 高光：让卡片边缘立起来
    Rectangle {
        anchors.fill: body
        anchors.topMargin: -root.lift + 1
        anchors.leftMargin: 1
        anchors.rightMargin: 1
        height: 1
        radius: root.theme.radiusCard
        color: root.theme.dark ? "#22FFFFFF" : "#CCFFFFFF"
        opacity: 0.7
    }

    Rectangle {
        id: body
        anchors.fill: parent
        radius: root.theme.radiusCard
        border.width: 1
        border.color: root.theme.line
        y: -root.lift
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.theme.cardTop }
            GradientStop { position: 1.0; color: root.theme.card }
        }
        Behavior on y {
            NumberAnimation { duration: root.theme.duration; easing.type: Easing.OutCubic }
        }
    }

    HoverHandler { id: hover }
}
