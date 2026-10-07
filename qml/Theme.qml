pragma Singleton
import QtQuick

// Colors measured from the Gesture HW1 app screenshots. Every QML file takes
// its colors, sizes and fonts from here — no literal colors elsewhere.
QtObject {
    // surfaces
    readonly property color bgWindow: "#000000"
    readonly property color bgPanel: "#2D2D2D"
    readonly property color bgSunken: "#212121"
    readonly property color bgHeader: "#222222"
    readonly property color bgDeep: "#1B1B1B"
    readonly property color bgHover: "#383838"
    readonly property color bgPressed: "#161616"
    readonly property color bgViewport: "#4A4A4A"
    readonly property color divider: "#373438"
    readonly property color border: "#3E3E3E"
    readonly property color grid: "#5B5B5B"

    // text
    readonly property color textStrong: "#F2F2F2"
    readonly property color textPrimary: "#E0E0E0"
    readonly property color textSecondary: "#ACACAC"
    readonly property color textMuted: "#646464"
    readonly property color textOnAccent: "#1B1B1B"

    // accents
    readonly property color accentGreen: "#95F996"
    readonly property color accentGreenHover: "#B3FBB4"
    readonly property color okGreen: "#8FD874"
    readonly property color ledGreen: "#06EA4B"
    readonly property color liveRed: "#D1533B"
    readonly property color playhead: "#EFA719"
    readonly property color hotspot: "#F58456"
    readonly property color clip: "#756BB9"
    readonly property color clipDark: "#4F4584"
    readonly property color clipSelected: "#9A91D6"
    readonly property color barFill: "#246962"
    readonly property color barText: "#44938A"
    readonly property color infoBlue: "#8BCBF3"
    readonly property color warnAmber: "#EFA719"
    readonly property color limitOlive: "#565425"

    // one color per joint: T2, T1, I, M, R, P
    readonly property var jointColors: ["#9E6362", "#A8886C", "#A9AB6B", "#67A585", "#6383A1", "#8466A8"]
    // extra plot colors for live telemetry channels
    readonly property var seriesColors: ["#9E6362", "#A8886C", "#A9AB6B", "#67A585", "#6383A1",
                                         "#8466A8", "#A5617F", "#5FA3A0", "#9C8F5E", "#7B9A5A",
                                         "#B07A5A", "#6D8FB3"]

    // metrics
    readonly property int radius: 6
    readonly property int radiusSmall: 3
    readonly property int gap: 6
    readonly property int pad: 10
    readonly property int rowHeight: 24
    readonly property int controlHeight: 24
    readonly property int labelWidth: 132

    // type
    readonly property string fontFamily: "Noto Sans"
    readonly property string monoFamily: "Noto Sans Mono"
    readonly property int fontSize: 12
    readonly property int fontSmall: 11
    readonly property int fontTiny: 10
    readonly property int fontLarge: 18
    readonly property int fontHuge: 24

    // MCB MCUs in the console / port status
    function mcuColor(t) { return t === "U6" ? clipSelected : (t === "U7" ? hotspot : textSecondary) }
    function jointColor(i) { return jointColors[((i % jointColors.length) + jointColors.length) % jointColors.length] }
    function seriesColor(i) { return seriesColors[((i % seriesColors.length) + seriesColors.length) % seriesColors.length] }
    function alpha(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }
}
