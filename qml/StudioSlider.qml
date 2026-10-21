import QtQuick
import QtQuick.Controls

Slider {
    id: control
    implicitHeight: 28
    hoverEnabled: true
    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth; height: 2; radius: 1
        color: StudioTheme.line
        Rectangle { width: control.visualPosition * parent.width; height: parent.height; radius: 1; color: control.enabled ? StudioTheme.accent : StudioTheme.faint }
    }
    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: 6; height: 12; radius: 2
        color: !control.enabled ? StudioTheme.faint : control.visualFocus || control.pressed ? StudioTheme.accent : StudioTheme.text
        scale: control.enabled && control.visualFocus ? 1.25 : 1
    }
    HoverHandler { cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
}
