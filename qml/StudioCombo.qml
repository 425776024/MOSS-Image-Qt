import QtQuick
import QtQuick.Controls
ComboBox {
    id: control
    implicitHeight: 28
    font.pixelSize: 12
    leftPadding: 8; rightPadding: 26
    hoverEnabled: true
    contentItem: Text {
        text: control.displayText; font: control.font; color: control.enabled ? StudioTheme.secondary : StudioTheme.faint
        verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
    }
    background: Rectangle { radius: 4; color: control.enabled && control.hovered ? StudioTheme.chrome : StudioTheme.field; border.color: control.activeFocus ? StudioTheme.accent : StudioTheme.line }
    indicator: StudioIcon { x: parent.width-width-9; anchors.verticalCenter: parent.verticalCenter; width: 14; height: 14; name: "chevron" }
    delegate: ItemDelegate {
        id: option
        required property int index
        required property var modelData
        width: control.width - 6; height: 28
        text: control.textRole ? modelData[control.textRole] : modelData
        Accessible.name: text
        contentItem: Text { text: option.text; font: control.font; color: StudioTheme.text; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
        highlighted: control.highlightedIndex === index
        background: Rectangle { color: option.highlighted ? StudioTheme.hover : StudioTheme.elevated; radius: 3 }
    }
    popup: Popup {
        y: control.height + 3; width: control.width; padding: 3
        implicitHeight: Math.min(contentItem.implicitHeight + 6, 300)
        contentItem: ListView { clip: true; implicitHeight: contentHeight; model: control.popup.visible ? control.delegateModel : null; currentIndex: control.highlightedIndex; ScrollIndicator.vertical: ScrollIndicator {} }
        background: Rectangle { color: StudioTheme.elevated; border.color: StudioTheme.line; radius: 4 }
    }
}
