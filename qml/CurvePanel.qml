import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root

    property Theme theme: Theme {}
    property var localPoints: []
    property string selectedPreset: ""

    spacing: 12

    function evalAt(temp) {
        if (!localPoints || localPoints.length < 2 || !isFinite(temp)) return 0;
        if (temp <= localPoints[0][0]) return localPoints[0][1];
        const last = localPoints.length - 1;
        if (temp >= localPoints[last][0]) return localPoints[last][1];
        for (let i = 1; i <= last; ++i) {
            if (temp <= localPoints[i][0]) {
                const t0 = localPoints[i - 1][0], t1 = localPoints[i][0];
                const r0 = localPoints[i - 1][1], r1 = localPoints[i][1];
                const k = (t1 > t0) ? (temp - t0) / (t1 - t0) : 0;
                return r0 + k * (r1 - r0);
            }
        }
        return localPoints[last][1];
    }

    function currentTemp() {
        const key = sensorCombo.currentKey;
        for (let i = 0; i < controller.sensors.length; ++i)
            if (controller.sensors[i].key === key) return controller.sensors[i].value;
        return NaN;
    }

    function syncFromController() {
        if (chart.dragging) return;
        const src = controller.curvePoints;
        if (src && src.length >= 2) {
            const copy = [];
            for (let i = 0; i < src.length; ++i)
                copy.push([Number(src[i][0]), Number(src[i][1])]);
            localPoints = copy;
        }
        sensorCombo.syncToKey(controller.curveSensor);
    }

    function commitSave() {
        const name = nameInput.text.trim();
        if (name.length === 0) return;
        root.selectedPreset = name;
        controller.savePreset(name, sensorCombo.currentKey, root.localPoints);
        for (let i = 0; i < controller.presets.length; ++i)
            if (controller.presets[i].name === name) presetCombo.currentIndex = i;
        namePopup.close();
    }

    Component.onCompleted: syncFromController()

    Connections {
        target: controller
        function onCurvePointsChanged() { root.syncFromController(); }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        DarkComboBox {
            id: sensorCombo
            Layout.preferredWidth: 230
            theme: root.theme
            model: controller.highlights
            onActivated: controller.setCurve(currentKey, root.localPoints)
            Component.onCompleted: syncToKey(controller.curveSensor)
        }

        Item { Layout.fillWidth: true }

        DarkComboBox {
            id: presetCombo
            Layout.preferredWidth: 112
            theme: root.theme
            placeholder: controller.english ? "Preset" : "预设"
            model: controller.presets
            textRole: "name"
            currentIndex: -1
            onActivated: {
                if (currentIndex >= 0) {
                    root.selectedPreset = model[currentIndex].name;
                    controller.applyPreset(model[currentIndex].name);
                }
            }
        }

        ModeButton {
            theme: root.theme
            text: controller.english ? "Save" : "保存"
            implicitWidth: 56
            implicitHeight: 34
            onClicked: {
                nameInput.text = root.selectedPreset.length > 0 ? root.selectedPreset : "";
                namePopup.open();
            }
        }

        ModeButton {
            theme: root.theme
            text: controller.english ? "Delete" : "删除"
            implicitWidth: 56
            implicitHeight: 34
            enabled: root.selectedPreset.length > 0
            onClicked: {
                controller.deletePreset(root.selectedPreset);
                root.selectedPreset = "";
                presetCombo.currentIndex = -1;
            }
        }
    }

    CurveChart {
        id: chart
        Layout.fillWidth: true
        Layout.fillHeight: true
        theme: root.theme
        points: root.localPoints
        currentTemp: root.currentTemp()
        maxRpm: controller.fans.length > 0 ? controller.fans[0].max : 6000
        actualRpm: controller.fans.length > 0 ? controller.fans[0].actual : 0

        onEdited: (pts) => {
            root.localPoints = pts;
            controller.setCurve(sensorCombo.currentKey, pts);
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 10

        Text {
            text: !isFinite(root.currentTemp())
                  ? (controller.english ? "Current temperature unavailable" : "当前温度不可用")
                  : (controller.english
                     ? "Now " + root.currentTemp().toFixed(1) + "°C → target " + Math.round(root.evalAt(root.currentTemp())) + " rpm"
                     : "当前 " + root.currentTemp().toFixed(1) + "°C → 目标 " + Math.round(root.evalAt(root.currentTemp())) + " rpm")
            color: root.theme.text
            font.pixelSize: 12
        }

        Item { Layout.fillWidth: true }

        Text {
            text: controller.english ? "Drag nodes · double-click to add · double-click a node to remove"
                                              : "拖动节点调整　·　双击空白加点　·　双击节点删点"
            color: root.theme.sub
            font.pixelSize: 10
        }
    }

    Popup {
        id: namePopup
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: 300
        modal: true
        focus: true
        padding: 18

        background: Rectangle {
            color: root.theme.card
            radius: 14
            border.width: 1
            border.color: root.theme.line
        }

        contentItem: ColumnLayout {
            spacing: 12

            Text {
                text: controller.english ? "Save as preset" : "保存为预设"
                color: root.theme.text
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }

            TextField {
                id: nameInput
                Layout.fillWidth: true
                placeholderText: controller.english ? "Preset name" : "预设名称"
                color: root.theme.text
                font.pixelSize: 13
                selectByMouse: true
                onAccepted: root.commitSave()
                background: Rectangle {
                    color: root.theme.cardAlt
                    radius: 8
                    border.width: 1
                    border.color: nameInput.activeFocus ? root.theme.accent : root.theme.line
                }
            }

            RowLayout {
                Item { Layout.fillWidth: true }
                ModeButton {
                    theme: root.theme
                    text: controller.english ? "Cancel" : "取消"
                    implicitWidth: 64
                    onClicked: namePopup.close()
                }
                ModeButton {
                    id: saveButton
                    theme: root.theme
                    text: controller.english ? "Save" : "保存"
                    implicitWidth: 64
                    onClicked: root.commitSave()
                }
            }
        }
    }
}
