import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Button {
    id: control
    property bool primary: false
    property bool active: false
    property bool quiet: false
    property string iconName: ""
    property string hint: ""
    readonly property bool iconOnly: iconName !== "" && text === ""
    implicitHeight: 28
    implicitWidth: iconOnly ? implicitHeight : Math.max(28, contentItem.implicitWidth + leftPadding + rightPadding)
    padding: 0
    leftPadding: iconOnly ? 0 : 8
    rightPadding: leftPadding
    font.pixelSize: 12
    hoverEnabled: true
    Accessible.name: hint || text
    ToolTip.visible: hovered && (hint !== "" || buttonLabel.truncated)
    ToolTip.text: hint || text
    ToolTip.delay: 600
    contentItem: RowLayout {
        spacing: 6
        StudioIcon {
            visible: control.iconName !== ""; name: control.iconName
            Layout.alignment: Qt.AlignCenter
            Layout.preferredWidth: 14; Layout.preferredHeight: 14
            ink: !control.enabled ? StudioTheme.faint : control.primary ? StudioTheme.accentInk : control.active ? StudioTheme.accent : StudioTheme.secondary
        }
        Text {
            id: buttonLabel
            visible: control.text !== ""; text: control.text; font: control.font
            Layout.fillWidth: true
            color: !control.enabled ? StudioTheme.faint : control.primary ? StudioTheme.accentInk : control.active ? StudioTheme.accent : StudioTheme.text
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }
    background: Rectangle {
        radius: 4
        color: control.primary ? (control.enabled ? control.down ? Qt.darker(StudioTheme.accent, 1.15) : control.hovered ? StudioTheme.accentHover : StudioTheme.accent : StudioTheme.elevated) :
               control.active ? StudioTheme.selection : control.down ? StudioTheme.elevated : control.hovered ? StudioTheme.hover : control.quiet ? "transparent" : StudioTheme.panel
        border.color: control.visualFocus ? StudioTheme.accent : control.primary || control.quiet || control.active ? "transparent" : StudioTheme.line
        Behavior on color { ColorAnimation { duration: 100 } }
    }
    HoverHandler { cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
}
