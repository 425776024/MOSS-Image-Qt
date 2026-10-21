// Adapted from VideoCut-Qt app/qml/controls/CardSurface.qml, at the user's request.
// Standalone Moss palette and icon bridge; native availability stays with each caller.
pragma ComponentBehavior: Bound
import QtQuick

// Visual surface only. The composing card retains its input, content and model.
Rectangle {
    id: surface
    property color baseColor: VideoCutTheme.panel
    property color hoverColor: VideoCutTheme.elevated
    property bool subduedSelection: false
    property color selectedColor: subduedSelection ? VideoCutTheme.controlSelection : VideoCutTheme.selection
    property bool hovered: false
    property bool selected: false
    property bool focusVisible: false
    property bool outlined: false
    readonly property bool inspectorSurface: VideoCutTheme.isInspector(surface)

    radius: 6
    color: selected ? inspectorSurface && focusVisible ? selectedColor.tint(VideoCutTheme.hover) : selectedColor : hovered || (inspectorSurface && focusVisible) ? hoverColor : baseColor
    border.width: inspectorSurface ? 0 : outlined || focusVisible || (subduedSelection && (selected || !VideoCutTheme.dark)) ? 1 : 0
    border.color: focusVisible ? VideoCutTheme.accent
        : subduedSelection ? selected ? VideoCutTheme.controlSelectionBorder : VideoCutTheme.controlBorder : VideoCutTheme.line
}
