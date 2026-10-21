import QtQuick
import QtQuick.Controls

Dialog {
    id: root
    modal: true; padding: 18; width: Math.min(440,Overlay.overlay ? Overlay.overlay.width-40 : 440)
    anchors.centerIn: Overlay.overlay
    background: Rectangle { radius: 12; color: HomeTheme.field; border.color: HomeTheme.outline }
    header: Label { text: root.title; color: HomeTheme.text; font.pixelSize: 16; font.weight: Font.DemiBold; padding: 18; bottomPadding: 6; wrapMode: Text.Wrap }
}
