pragma Singleton
import QtQuick

QtObject {
    // 背景色
    readonly property color bgDark:     "#111111"
    readonly property color bgPanel:    "#1a1a1a"
    readonly property color bgBase:     "#1e1e1e"
    readonly property color bgMid:      "#232323"
    readonly property color bgHeader:   "#252525"
    readonly property color bgWidget:   "#2d2d2d"
    readonly property color bgInput:    "#3c3c3c"
    readonly property color bgHover:    "#4a4a4a"

    // 强调色
    readonly property color accent:     "#0078d4"
    readonly property color accentHover:"#1a8ae6"
    readonly property color accentDark: "#005fa3"

    // 文本色
    readonly property color textPrimary:   "#cccccc"
    readonly property color textSecondary: "#aaaaaa"
    readonly property color textMuted:     "#888888"
    readonly property color textDim:       "#666666"
    readonly property color textDisabled:  "#555555"
    readonly property color textBright:    "#dddddd"
    readonly property color textWhite:     "#ffffff"
    readonly property color textCustomAttr:"#9cdcfe"
    readonly property color textValue:     "#dcdcaa"

    // 边框色
    readonly property color border:        "#555555"
    readonly property color borderFocus:   "#0078d4"
    readonly property color borderDim:     "#333333"

    // 菜单/控件尺寸
    readonly property int menuItemWidth:  180
    readonly property int menuItemHeight: 28
    readonly property int controlHeight:  26
    readonly property string fontFamily:  "Noto Sans CJK SC"
    readonly property int fontSize:       12
    readonly property int fontSizeSmall:  11
    readonly property int fontSizeTiny:   10
}
