// Adapted from VideoCut-Qt app/qml/controls/AppInputSurface.qml, at the user's request.
// Standalone Moss palette and icon bridge; native availability stays with each caller.
pragma ComponentBehavior: Bound
import QtQuick

Rectangle {
    id: surface
    property bool focused: false
    property bool outlined: false
    property bool borderless: false
    property bool hovered: false
    property bool readOnly: false
    property bool invalid: false
    property bool available: enabled
    property color baseColor: VideoCutTheme.field
    property color strokeColor: VideoCutTheme.line
    readonly property bool inspectorSurface: VideoCutTheme.isInspector(surface)
    implicitHeight: VideoCutTheme.inputHeight
    radius: 4
    // Editing another control never changes this field's background.
    color: hovered || (focused && inspectorSurface) ? baseColor === VideoCutTheme.field ? VideoCutTheme.inputHover : baseColor.tint(VideoCutTheme.hover) : baseColor
    // A containing card owns the sole outline for borderless inputs, including
    // focus and validation states. Never restore an inner border on focus.
    border.width: !borderless && ((focused && !inspectorSurface) || invalid || outlined || !VideoCutTheme.dark) ? VideoCutTheme.hairline : 0
    border.color: invalid && enabled ? VideoCutTheme.danger : focused && !inspectorSurface && enabled ? VideoCutTheme.accent : strokeColor
}
