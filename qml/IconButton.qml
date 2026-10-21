// Adapted from VideoCut-Qt app/qml/controls/IconButton.qml, at the user's request.
// Standalone Moss palette and icon bridge; native availability stays with each caller.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

Button {
    id: control
    readonly property bool appearanceEnabled: control.enabled
    property string glyph: "media"
    property string hint: ""
    property bool active: false
    property real iconSize: VideoCutTheme.controlIconSize
    property color iconColor: VideoCutTheme.secondary
    implicitWidth: 24
    implicitHeight: 24
    padding: Math.min(4, Math.max(0, (Math.min(width, height) - iconSize) / 2))
    leftPadding: padding
    rightPadding: padding
    topPadding: padding
    bottomPadding: padding
    hoverEnabled: true
    Accessible.name: hint
    ToolTip.visible: hovered && hint.length > 0
    ToolTip.text: hint
    ToolTip.delay: VideoCutTheme.tooltipDelay
    contentItem: Item {
        implicitWidth: control.iconSize
        implicitHeight: control.iconSize
        Glyph {
            objectName: "buttonGlyph"
            anchors.centerIn: parent
            width: Math.max(0, Math.min(control.iconSize, parent.width, parent.height))
            height: width
            kind: control.glyph
            color: !control.appearanceEnabled ? VideoCutTheme.muted : control.active ? VideoCutTheme.accent : control.iconColor
        }
    }
    background: Rectangle {
        radius: Math.min(4, control.height / 2)
        color: control.down && control.appearanceEnabled ? VideoCutTheme.pressed
            : control.active ? VideoCutTheme.selection : (control.hovered || (control.visualFocus && VideoCutTheme.isInspector(control))) && control.enabled ? VideoCutTheme.hover : "transparent"
        border.color: control.activeFocus ? VideoCutTheme.accent : "transparent"
        border.width: VideoCutTheme.isInspector(control) ? 0 : VideoCutTheme.hairline
    }
    HoverHandler {
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }
}
