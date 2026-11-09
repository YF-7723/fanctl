import QtQuick

// 日间：暖白纸张感；夜间：暖褐深色。所有颜色/圆角/动效集中在这里
QtObject {
    id: t

    property bool dark: false

    // 背景：带轻微上下渐变，避免大片纯色显得空
    readonly property color bgTop:      dark ? "#17130E" : "#FBF6EF"
    readonly property color bg:         dark ? "#100E0A" : "#F7F1E8"

    // 卡片：顶部略亮、底部略暗，制造体积感
    readonly property color cardTop:    dark ? "#2E281F" : "#FFFDF9"
    readonly property color card:       dark ? "#231E17" : "#FFFBF4"
    readonly property color cardAlt:    dark ? "#2C2620" : "#F3EADD"

    readonly property color line:       dark ? "#453B2D" : "#E8DACA"
    readonly property color text:       dark ? "#F7F0E6" : "#2C2620"
    readonly property color sub:        dark ? "#AC9B89" : "#8A7C6B"
    readonly property color accent:     dark ? "#FFA366" : "#DE6F2B"
    readonly property color accentSoft: dark ? "#3B2A1B" : "#FBE6D3"
    readonly property color ok:         dark ? "#57C98A" : "#2F8B57"
    readonly property color okSoft:     dark ? "#1B3A2A" : "#E2F3E9"
    readonly property color warn:       dark ? "#F2A75C" : "#B85F14"
    readonly property color track:      dark ? "#40382E" : "#E4D6C5"

    // 阴影分两层：近处实、远处散
    readonly property color shadowNear: dark ? "#000000AA" : "#8C5A2E2E"
    readonly property color shadowFar:  dark ? "#00000077" : "#8C5A2E18"

    // 同色不同透明度：颜色过渡时不会经过黑色
    function alpha(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }

    readonly property int radiusCard: 18
    readonly property int radiusCtl: 10
    readonly property int duration: 160
}
