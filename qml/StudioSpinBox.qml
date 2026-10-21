import QtQuick
import QtQuick.Controls

SpinBox {
    id: control
    implicitWidth: 100
    implicitHeight: 28
    leftPadding: 25; rightPadding: 25
    font.pixelSize: 12
    contentItem: TextInput {
        text: control.textFromValue(control.value, control.locale)
        font: control.font; color: control.enabled ? StudioTheme.secondary : StudioTheme.faint
        horizontalAlignment: Qt.AlignHCenter; verticalAlignment: Qt.AlignVCenter
        readOnly: !control.editable; validator: control.validator
        selectByMouse: true; selectionColor: StudioTheme.selection
        selectedTextColor: StudioTheme.text
        inputMethodHints: Qt.ImhFormattedNumbersOnly
    }
    background: Rectangle { color: StudioTheme.field; border.color: control.activeFocus ? StudioTheme.accent : StudioTheme.line; radius: 4 }
    up.indicator: Rectangle {
        x: control.width - width; width: 24; height: control.height; radius: 4
        color: control.up.pressed ? StudioTheme.hover : "transparent"
        Text { anchors.centerIn: parent; text: "+"; color: control.enabled ? StudioTheme.secondary : StudioTheme.faint; font.pixelSize: 14 }
    }
    down.indicator: Rectangle {
        width: 24; height: control.height; radius: 4
        color: control.down.pressed ? StudioTheme.hover : "transparent"
        Text { anchors.centerIn: parent; text: "−"; color: control.enabled ? StudioTheme.secondary : StudioTheme.faint; font.pixelSize: 14 }
    }
}
