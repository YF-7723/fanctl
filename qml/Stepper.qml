import QtQuick
import QtQuick.Layouts

Rectangle {
    id: ctl

    property Theme theme: Theme {}
    property int value: 0
    property int from: 0
    property int to: 100
    property int step: 1
    property string suffix: ""

    signal edited(int v)

    implicitHeight: 34
    implicitWidth: 130
    radius: theme.radiusCtl
    color: theme.cardAlt
    border.width: 1
    border.color: theme.line

    function apply(v) {
        const clamped = Math.max(ctl.from, Math.min(ctl.to, v));
        if (clamped === ctl.value) return;
        ctl.value = clamped;
        ctl.edited(clamped);
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            Layout.fillHeight: true
            Layout.preferredWidth: 30
            Text {
                anchors.centerIn: parent
                text: "−"
                color: ctl.theme.sub
                font.pixelSize: 16
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: ctl.apply(ctl.value - ctl.step)
            }
        }

        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: ctl.value + ctl.suffix
            color: ctl.theme.text
            font.pixelSize: 13
        }

        Item {
            Layout.fillHeight: true
            Layout.preferredWidth: 30
            Text {
                anchors.centerIn: parent
                text: "+"
                color: ctl.theme.sub
                font.pixelSize: 15
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: ctl.apply(ctl.value + ctl.step)
            }
        }
    }
}
