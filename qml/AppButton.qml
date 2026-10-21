// Adapted from VideoCut-Qt app/qml/controls/AppButton.qml, at the user's request.
// Standalone Moss palette and icon bridge; native availability stays with each caller.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Button {
    id: root
    readonly property bool appearanceEnabled: root.enabled
    property string iconName: ""
    property bool primary: false
    property bool active: false
    property bool quiet: false
    property bool compact: false
    property real iconSize: compact ? VideoCutTheme.smallIconSize : VideoCutTheme.controlIconSize
    property string hint: ""
    property int textElide: Text.ElideRight
    property real textMaximumWidth: Infinity
    implicitHeight: compact ? 24 : VideoCutTheme.controlHeight
    readonly property bool iconOnly: iconName.length > 0 && text.length === 0
    implicitWidth: iconOnly ? implicitHeight : contentItem.implicitWidth + leftPadding + rightPadding
    padding: 0
    leftPadding: iconOnly ? 0 : compact ? 6 : 10
    rightPadding: leftPadding
    font: compact ? VideoCutTheme.compactButtonFont : VideoCutTheme.buttonFont
    hoverEnabled: true
    Accessible.name: text.length ? text : hint
    ToolTip.visible: hovered && (hint.length > 0 || buttonLabel.truncated)
    ToolTip.text: root.hint.length > 0 ? root.hint : root.text
    ToolTip.delay: VideoCutTheme.tooltipDelay
    contentItem: RowLayout {
        spacing: root.compact ? 4 : 6
        Glyph {
            objectName: "buttonGlyph"
            visible: root.iconName.length > 0
            kind: root.iconName
            color: !root.appearanceEnabled ? VideoCutTheme.muted : root.primary ? VideoCutTheme.accentInk : VideoCutTheme.text
            Layout.alignment: Qt.AlignCenter
            Layout.minimumWidth: 0
            Layout.minimumHeight: 0
            Layout.preferredWidth: Math.min(root.iconSize, root.availableWidth)
            Layout.preferredHeight: Math.min(root.iconSize, root.availableHeight)
        }
        Text {
            id: buttonLabel; objectName: "appButtonLabel"
            visible: root.text.length > 0
            text: root.text
            color: !root.appearanceEnabled ? VideoCutTheme.muted : root.primary ? VideoCutTheme.accentInk : VideoCutTheme.text
            font: root.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            maximumLineCount: 1
            elide: root.textElide
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.maximumWidth: root.textMaximumWidth
        }
    }
    background: Rectangle {
        radius: root.compact ? 5 : 6
        color: root.primary && !root.appearanceEnabled ? VideoCutTheme.elevated
            : root.primary && root.appearanceEnabled ? (root.down ? VideoCutTheme.accent.darker(1.15)
                : root.hovered ? VideoCutTheme.accent.lighter(1.08) : VideoCutTheme.accent)
            : root.down && root.appearanceEnabled ? VideoCutTheme.pressed : (root.hovered || (root.visualFocus && VideoCutTheme.isInspector(root))) && root.appearanceEnabled ? VideoCutTheme.hover
            : root.active ? VideoCutTheme.controlSelection : root.quiet ? "transparent" : VideoCutTheme.panel
        border.color: root.activeFocus && !VideoCutTheme.isInspector(root) ? VideoCutTheme.accent : !root.appearanceEnabled && !root.quiet ? VideoCutTheme.borderSubtle : root.primary || root.quiet ? "transparent" : VideoCutTheme.borderSubtle
        border.width: VideoCutTheme.hairline
        opacity: root.appearanceEnabled ? 1 : 0.7
    }
    HoverHandler {
        cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }
}
