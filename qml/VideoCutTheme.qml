pragma Singleton
import QtQuick

// Palette and control metrics adapted from VideoCut-Qt Theme.qml and
// AppearanceController.cpp. Moss owns the persisted appearance preference.
QtObject {
    readonly property bool dark: workspace.appearanceMode === "dark" || (workspace.appearanceMode === "system" && Application.styleHints.colorScheme === Qt.Dark)
    readonly property var palettes: ({
        dark: {background:"#101113",panel:"#18191c",elevated:"#222328",sidebar:"#141518",field:"#111214",accent:"#34d1bf",ink:"#071311"},
        graphite: {background:"#151515",panel:"#1c1c1e",elevated:"#262628",sidebar:"#18181a",field:"#171719",accent:"#c0c6ce",ink:"#141516"},
        teal: {background:"#071614",panel:"#0d211d",elevated:"#15302a",sidebar:"#0a1a17",field:"#0b1c18",accent:"#43d6aa",ink:"#031511"},
        blue: {background:"#1c1c1e",panel:"#252527",elevated:"#303033",sidebar:"#202022",field:"#1e1e20",accent:"#409cff",ink:"#ffffff"}
    })
    readonly property var palette: palettes[workspace.themePreset] || palettes.dark
    readonly property color background: dark ? palette.background : "#fcfcfd"
    readonly property color panel: dark ? palette.panel : "#ffffff"
    readonly property color elevated: dark ? palette.elevated : "#f0f2f4"
    readonly property color sidebar: dark ? palette.sidebar : "#f2f3f5"
    readonly property color field: dark ? palette.field : "#ffffff"
    readonly property color chrome: dark ? "#131416" : "#f4f5f7"
    readonly property color line: dark ? "#20ffffff" : "#15000000"
    readonly property color borderSubtle: dark ? "#2bffffff" : "#20000000"
    readonly property color text: dark ? "#ebffffff" : "#202329"
    readonly property color secondary: dark ? "#b8ffffff" : "#60656f"
    readonly property color muted: dark ? "#80ffffff" : "#737b86"
    readonly property color faint: dark ? "#57ffffff" : "#838994"
    readonly property color accent: dark ? palette.accent : workspace.themePreset === "blue" ? "#007aff" : workspace.themePreset === "graphite" ? "#60656f" : "#008c7e"
    readonly property color accentInk: dark ? palette.ink : "#ffffff"
    readonly property color selection: Qt.alpha(accent,dark ? .18 : .08)
    readonly property color controlSelection: Qt.alpha(accent,dark ? .15 : .09)
    readonly property color controlSelectionBorder: Qt.alpha(accent,.45)
    readonly property color controlBorder: borderSubtle
    readonly property color hover: dark ? "#12ffffff" : "#08000000"
    readonly property color pressed: dark ? "#20ffffff" : "#10000000"
    readonly property color inputHover: dark ? palette.elevated : "#f2f3f5"
    readonly property color sliderField: dark ? "#0dffffff" : "#08000000"
    readonly property color parameterProgress: Qt.alpha(accent,.12)
    readonly property color danger: dark ? "#ff9a9a" : "#bf3e48"
    readonly property real controlHeight: 28
    readonly property real inputHeight: 24
    readonly property real smallIconSize: 14
    readonly property real controlIconSize: 16
    readonly property real hairline: 1 / Math.max(1,Screen.devicePixelRatio)
    readonly property int tooltipDelay: 600
    readonly property int fontWeightSemibold: Font.DemiBold
    readonly property font bodyFont: Qt.font({family:".AppleSystemUIFont",pixelSize:13})
    readonly property font buttonFont: Qt.font({family:".AppleSystemUIFont",pixelSize:13,weight:Font.Medium})
    readonly property font compactButtonFont: Qt.font({family:".AppleSystemUIFont",pixelSize:12})
    readonly property font microFont: Qt.font({family:".AppleSystemUIFont",pixelSize:10})
    readonly property font parameterFont: bodyFont
    function isInspector(item) { return false }
}
