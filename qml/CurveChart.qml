import QtQuick

// 可拖拽的折线图：横轴温度、纵轴转速，双击空白加点、双击节点删点
Item {
    id: chart

    property Theme theme: Theme {}
    property var points: []          // [[温度, 转速], ...]
    property int maxRpm: 6000
    property real currentTemp: NaN   // 当前温度竖线
    property int actualRpm: 0        // 当前实际转速（浮层显示，不占布局）

    signal edited(var pts)

    readonly property real tMin: 20
    readonly property real tMax: 100
    readonly property real padLeft: 42
    readonly property real padBottom: 22
    readonly property real padTop: 8
    readonly property real padRight: 10

    property var pts: []
    property bool dragging: false

    Component.onCompleted: resetFromModel()
    onPointsChanged: if (!dragging) resetFromModel()
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()
    onMaxRpmChanged: canvas.requestPaint()

    Connections {
        target: chart.theme
        function onDarkChanged() { canvas.requestPaint(); }
    }

    function resetFromModel() {
        const copy = [];
        for (let i = 0; i < points.length; ++i)
            copy.push([Number(points[i][0]), Number(points[i][1])]);
        pts = copy;
        canvas.requestPaint();
    }

    // 坐标换算
    function plotW() { return width - padLeft - padRight; }
    function plotH() { return height - padTop - padBottom; }
    function xOf(t) { return padLeft + (t - tMin) / (tMax - tMin) * plotW(); }
    function yOf(r) { return padTop + plotH() - Math.max(0, Math.min(1, r / maxRpm)) * plotH(); }
    function tOf(x) { return tMin + (x - padLeft) / plotW() * (tMax - tMin); }
    function rOf(y) { return (1 - (y - padTop) / plotH()) * maxRpm; }

    function hitTest(x, y) {
        for (let i = 0; i < pts.length; ++i) {
            const dx = xOf(pts[i][0]) - x, dy = yOf(pts[i][1]) - y;
            if (dx * dx + dy * dy <= 196) return i;   // 14px 半径
        }
        return -1;
    }

    function movePoint(i, x, y) {
        let t = Math.round(tOf(x));
        let r = Math.round(rOf(y));
        const lo = (i > 0) ? pts[i - 1][0] + 1 : tMin + 1;
        const hi = (i < pts.length - 1) ? pts[i + 1][0] - 1 : tMax - 1;
        t = Math.max(lo, Math.min(hi, t));
        r = Math.max(0, Math.min(maxRpm, r));
        const copy = pts.slice();
        copy[i] = [t, r];
        pts = copy;
        canvas.requestPaint();
    }

    function addPoint(x, y) {
        let t = Math.round(tOf(x));
        let r = Math.round(rOf(y));
        t = Math.max(tMin + 1, Math.min(tMax - 1, t));
        r = Math.max(0, Math.min(maxRpm, r));
        const copy = pts.slice();
        copy.push([t, r]);
        copy.sort(function (a, b) { return a[0] - b[0]; });
        pts = copy;
        canvas.requestPaint();
    }

    function removePoint(i) {
        if (pts.length <= 2) return;
        const copy = pts.slice();
        copy.splice(i, 1);
        pts = copy;
        canvas.requestPaint();
        edited(pts);
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        renderTarget: Canvas.FramebufferObject

        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            const w = width, h = height;
            const pw = chart.plotW(), ph = chart.plotH();
            const accent = chart.theme.accent;
            const line = chart.theme.line;
            const sub = chart.theme.sub;

            // 网格
            ctx.strokeStyle = line;
            ctx.lineWidth = 1;
            ctx.beginPath();
            for (let t = chart.tMin; t <= chart.tMax; t += 20) {
                const x = Math.round(chart.xOf(t)) + 0.5;
                ctx.moveTo(x, chart.padTop);
                ctx.lineTo(x, chart.padTop + ph);
            }
            for (let k = 0; k <= 4; ++k) {
                const y = Math.round(chart.padTop + ph * k / 4) + 0.5;
                ctx.moveTo(chart.padLeft, y);
                ctx.lineTo(chart.padLeft + pw, y);
            }
            ctx.stroke();

            // 坐标文字
            ctx.fillStyle = sub;
            ctx.font = "10px sans-serif";
            ctx.textAlign = "center";
            for (let t = chart.tMin; t <= chart.tMax; t += 20)
                ctx.fillText(t + "°", chart.xOf(t), h - 6);

            ctx.textAlign = "right";
            const steps = [0, 0.25, 0.5, 0.75, 1];
            for (let i = 0; i < steps.length; ++i) {
                const v = Math.round(chart.maxRpm * steps[i]);
                ctx.fillText(v >= 1000 ? (v / 1000).toFixed(1) + "k" : v,
                             chart.padLeft - 8, chart.yOf(v) + 3);
            }

            // 当前温度竖线
            if (isFinite(chart.currentTemp)) {
                const cx = Math.round(chart.xOf(chart.currentTemp)) + 0.5;
                ctx.strokeStyle = chart.theme.warn;
                ctx.setLineDash([3, 3]);
                ctx.beginPath();
                ctx.moveTo(cx, chart.padTop);
                ctx.lineTo(cx, chart.padTop + ph);
                ctx.stroke();
                ctx.setLineDash([]);
            }

            if (chart.pts.length < 2) return;

            // 曲线下方填充
            ctx.beginPath();
            ctx.moveTo(chart.xOf(chart.pts[0][0]), chart.padTop + ph);
            for (let i = 0; i < chart.pts.length; ++i)
                ctx.lineTo(chart.xOf(chart.pts[i][0]), chart.yOf(chart.pts[i][1]));
            ctx.lineTo(chart.xOf(chart.pts[chart.pts.length - 1][0]), chart.padTop + ph);
            ctx.closePath();
            ctx.fillStyle = Qt.rgba(accent.r, accent.g, accent.b, 0.12);
            ctx.fill();

            // 折线
            ctx.beginPath();
            for (let i = 0; i < chart.pts.length; ++i) {
                const x = chart.xOf(chart.pts[i][0]);
                const y = chart.yOf(chart.pts[i][1]);
                if (i === 0) ctx.moveTo(x, y);
                else ctx.lineTo(x, y);
            }
            ctx.strokeStyle = accent;
            ctx.lineWidth = 2.5;
            ctx.lineJoin = "round";
            ctx.lineCap = "round";
            ctx.stroke();
        }
    }

    // 节点（只画，不接事件；拖动由下面的 MouseArea 统一处理）
    Repeater {
        model: chart.pts

        delegate: Rectangle {
            x: chart.xOf(modelData[0]) - width / 2
            y: chart.yOf(modelData[1]) - height / 2
            width: 13
            height: 13
            radius: 6.5
            color: "#FFFFFF"
            border.width: 2.5
            border.color: chart.theme.accent
            scale: dragArea.hoveredIndex === index ? 1.25 : 1.0
            Behavior on scale { NumberAnimation { duration: 90 } }
        }
    }

    // 实际转速浮层：贴在图表左上角，不额外占用布局空间
    Rectangle {
        x: chart.padLeft + 10
        y: chart.padTop + 10
        width: rpmText.implicitWidth + 18
        height: 22
        radius: 11
        color: chart.theme.accentSoft
        border.width: 1
        border.color: Qt.rgba(chart.theme.accent.r, chart.theme.accent.g,
                              chart.theme.accent.b, 0.25)

        Text {
            id: rpmText
            anchors.centerIn: parent
            text: (controller.english ? "Actual " : "实际 ") + chart.actualRpm + " rpm"
            color: chart.theme.accent
            font.pixelSize: 11
            font.weight: Font.Medium
        }
    }

    MouseArea {
        id: dragArea
        anchors.fill: parent
        hoverEnabled: true

        property int active: -1
        property int hoveredIndex: -1

        onPressed: (mouse) => {
            active = chart.hitTest(mouse.x, mouse.y);
            if (active >= 0) {
                chart.dragging = true;
                chart.movePoint(active, mouse.x, mouse.y);
            }
        }
        onPositionChanged: (mouse) => {
            if (active >= 0) chart.movePoint(active, mouse.x, mouse.y);
            else hoveredIndex = chart.hitTest(mouse.x, mouse.y);
        }
        onReleased: {
            if (active >= 0) {
                active = -1;
                chart.dragging = false;
                chart.edited(chart.pts);
            }
        }
        onDoubleClicked: (mouse) => {
            const idx = chart.hitTest(mouse.x, mouse.y);
            if (idx >= 0) chart.removePoint(idx);
            else {
                chart.addPoint(mouse.x, mouse.y);
                chart.edited(chart.pts);
            }
        }
    }
}
