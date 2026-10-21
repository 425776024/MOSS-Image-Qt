// Adapted from VideoCut-Qt app/qml/controls/AppCheckBox.qml, at the user's request.
// Standalone Moss palette and icon bridge; native availability stays with each caller.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

// Styling only; the owner keeps checked/nextCheckState and command admission.
CheckBox {
    id: input
    readonly property bool appearanceEnabled: input.enabled
    property bool valueKnown: true
    font: VideoCutTheme.bodyFont
    hoverEnabled: true
    implicitHeight: Math.max(VideoCutTheme.controlHeight, implicitContentHeight + topPadding + bottomPadding)
    implicitWidth: text.length ? implicitContentWidth + leftPadding + rightPadding : 22
    padding: 4
    leftPadding: text.length ? 24 : 0
    rightPadding: text.length ? 4 : 0
    indicator: Rectangle {
        width: 12; height: 12
        // Keep the visible edge on the same origin as form labels. The
        // wider control still provides the full pointer and keyboard target.
        x: 0; y: (input.height - height) / 2
        radius: 3
        color: input.appearanceEnabled && input.checkState === Qt.Checked ? VideoCutTheme.accent : VideoCutTheme.elevated
        border.width: VideoCutTheme.hairline
        border.color: !VideoCutTheme.isInspector(input) && (input.activeFocus || (input.appearanceEnabled && input.hovered)) ? VideoCutTheme.accent : VideoCutTheme.borderSubtle
        Text {
            anchors.centerIn: parent
            text: input.valueKnown && input.checkState === Qt.Checked ? "✓" : ""
            font: VideoCutTheme.microFont
            color: input.appearanceEnabled && input.checkState === Qt.Checked ? VideoCutTheme.accentInk : VideoCutTheme.muted
        }
        Rectangle {
            anchors.centerIn: parent
            visible: !input.valueKnown || input.checkState === Qt.PartiallyChecked
            width: 5; height: 5
            radius: input.valueKnown ? 1 : 2.5
            color: input.valueKnown ? VideoCutTheme.muted : "transparent"
            border.width: input.valueKnown ? 0 : VideoCutTheme.hairline
            border.color: VideoCutTheme.muted
        }
    }
    background: Rectangle { radius: 4; color: input.visualFocus && VideoCutTheme.isInspector(input) ? VideoCutTheme.hover : "transparent" }
    contentItem: Text {
        text: input.text
        font: input.font
        color: input.appearanceEnabled ? VideoCutTheme.text : VideoCutTheme.muted
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.Wrap
    }
    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: 4
        color: "transparent"
        border.color: VideoCutTheme.accent
        border.width: VideoCutTheme.hairline
        visible: input.enabled && input.activeFocus && !VideoCutTheme.isInspector(input)
    }
}
