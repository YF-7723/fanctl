import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: root

    property Theme theme: Theme {}

    spacing: 10

    RowLayout {
        Layout.fillWidth: true
        Text {
            text: controller.english ? "Drag the slider or type a target speed" : "拖动滑杆，或直接输入目标转速"
            color: root.theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
        Item { Layout.fillWidth: true }
        Text {
            text: controller.english ? "Blue knob = target · white tick = actual"
                                              : "蓝色圆点 = 目标　·　白色刻度 = 实际"
            color: root.theme.sub
            font.pixelSize: 10
        }
    }

    // 每次切进手动模式，重新从后台同步一次
    onVisibleChanged: {
        if (!visible) return;
        for (let i = 0; i < fanRepeater.count; ++i) {
            const item = fanRepeater.itemAt(i);
            if (item) item.resetToDaemon();
        }
    }

    Repeater {
        id: fanRepeater
        model: controller.fans.length

        delegate: FanSlider {
            Layout.fillWidth: true
            theme: root.theme
            fan: index < controller.fans.length ? controller.fans[index] : null
            onDragStarted: controller.setPaused(true)
            onDragFinished: (i, rpm) => {
                controller.setPaused(false);
                controller.setFan(i, rpm);
            }
            onRpmEntered: (i, rpm) => controller.setFan(i, rpm)
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
        text: controller.english ? "Closing the window keeps FanCtl running in the menu bar."
                                             : "关闭窗口后仍在菜单栏运行，退出时才恢复系统控制。"
        color: root.theme.sub
        font.pixelSize: 11
    }
}
