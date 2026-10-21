import QtQuick
import QtQuick.Controls

CheckBox {
    id: control
    implicitHeight: 28
    spacing: 7
    font.pixelSize: 12
    indicator: Rectangle {
        x: control.leftPadding; y: (control.height-height)/2; width: 12; height: 12; radius: 3
        color: control.enabled && control.checked ? StudioTheme.accent : StudioTheme.elevated
        border.color: control.visualFocus ? StudioTheme.accent : StudioTheme.line
        StudioIcon { anchors.centerIn: parent; width: 10; height: 10; name: "check"; ink: control.enabled ? StudioTheme.accentInk : StudioTheme.muted; visible: control.checked }
    }
    contentItem: Text {
        text: control.text; font: control.font; color: control.enabled ? StudioTheme.text : StudioTheme.faint
        leftPadding: control.indicator.width + control.spacing
        verticalAlignment: Text.AlignVCenter
    }
}
