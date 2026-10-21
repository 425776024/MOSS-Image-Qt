// Adapted from VideoCut-Qt app/qml/controls/AppMenuItem.qml, at the user's request.
// Standalone Moss palette and icon bridge; native availability stays with each caller.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

MenuItem {
    id: control
    readonly property bool appearanceEnabled: control.enabled
    property bool selectionActive: checked
    property string glyph: ""
    property string shortcutText: ""
    property bool destructive: false
    implicitHeight: 28
    height: visible ? implicitHeight : 0
    implicitWidth: Math.max(160, contentItem.implicitWidth + leftPadding + rightPadding)
    leftPadding: 8
    rightPadding: 8
    topPadding: 4
    bottomPadding: 4
    font: VideoCutTheme.parameterFont
    hoverEnabled: true
    indicator: null
    arrow: null
    Accessible.name: text
    ToolTip {
        parent: control
        visible: control.hovered && control.visible && (!control.menu || control.menu.visible)
            && (label.truncated || shortcutLabel.truncated)
        text: control.shortcutText.length > 0 ? control.text + "\n" + control.shortcutText : control.text
        delay: VideoCutTheme.tooltipDelay
        // The shared attached tooltip can remain below a menu in the overlay.
        z: control.menu ? control.menu.z + 1 : 1
        width: Math.min(implicitWidth, 360, Overlay.overlay ? Math.max(0, Overlay.overlay.width - 12) : 360)
        x: (control.width - width) / 2
        y: -height - 6
    }
    contentItem: RowLayout {
        spacing: 8
        Glyph {
            visible: control.glyph.length > 0
            kind: control.glyph
            color: control.appearanceEnabled ? VideoCutTheme.secondary : VideoCutTheme.muted
            Layout.preferredWidth: VideoCutTheme.controlIconSize
            Layout.preferredHeight: VideoCutTheme.controlIconSize
        }
        Text {
            id: label
            text: control.text
            font.family: control.font.family
            font.pixelSize: control.font.pixelSize
            font.italic: control.font.italic
            font.weight: control.selectionActive ? VideoCutTheme.fontWeightSemibold : control.font.weight
            color: !control.appearanceEnabled ? VideoCutTheme.muted : control.destructive ? VideoCutTheme.danger : VideoCutTheme.text
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
        Text {
            id: shortcutLabel
            visible: control.shortcutText.length > 0
            text: control.shortcutText
            font: control.font
            color: VideoCutTheme.muted
            elide: Text.ElideRight
            Layout.maximumWidth: Math.max(0, control.availableWidth / 2)
        }
        Glyph {
            visible: control.subMenu !== null
            kind: "keyframe-next"
            color: control.appearanceEnabled ? VideoCutTheme.secondary : VideoCutTheme.muted
            Layout.preferredWidth: VideoCutTheme.smallIconSize
            Layout.preferredHeight: VideoCutTheme.smallIconSize
        }
    }
    background: Rectangle {
        radius: 3
        color: !control.appearanceEnabled ? "transparent" : control.down ? VideoCutTheme.pressed : control.selectionActive ? VideoCutTheme.controlSelection : control.highlighted ? VideoCutTheme.hover : "transparent"
    }
}
