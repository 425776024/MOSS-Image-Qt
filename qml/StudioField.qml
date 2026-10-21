import QtQuick
import QtQuick.Controls
TextField {
    id: control
    implicitHeight: 28
    font.pixelSize: 12
    color: enabled ? StudioTheme.text : StudioTheme.faint
    placeholderTextColor: StudioTheme.muted
    selectionColor: StudioTheme.selection
    selectedTextColor: StudioTheme.text
    selectByMouse: true
    hoverEnabled: true
    padding: 6
    topPadding: 3; bottomPadding: 3
    verticalAlignment: Text.AlignVCenter
    background: Rectangle {
        radius: 4
        color: control.enabled && control.hovered ? StudioTheme.chrome : StudioTheme.field
        border.color: control.activeFocus ? StudioTheme.accent : StudioTheme.line
    }
}
