pragma Singleton
import QtQuick

QtObject {
    readonly property bool dark: HomeTheme.dark
    readonly property color background: HomeTheme.surface
    readonly property color chrome: HomeTheme.sidebar
    readonly property color panel: dark ? HomeTheme.surface : HomeTheme.field
    readonly property color elevated: dark ? "#30333C" : "#EEF0F7"
    readonly property color field: dark ? "#181B22" : "#FFFFFF"
    readonly property color hover: HomeTheme.hover
    readonly property color line: HomeTheme.outline
    readonly property color text: HomeTheme.text
    readonly property color secondary: HomeTheme.secondary
    readonly property color muted: HomeTheme.secondary
    readonly property color faint: HomeTheme.faint
    readonly property color accent: HomeTheme.accent
    readonly property color accentHover: dark ? Qt.lighter(accent, 1.1) : Qt.darker(accent, 1.07)
    readonly property color accentInk: HomeTheme.accentInk
    readonly property color selection: HomeTheme.selection
    readonly property color warning: dark ? "#F2C66D" : "#936025"
    readonly property color danger: HomeTheme.danger
    readonly property color canvas: dark ? "#15181E" : "#FFFFFF"
    readonly property color glassLine: dark ? "#42FFFFFF" : "#B0C4CADB"
}
