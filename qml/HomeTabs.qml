import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Control {
    id: root
    property var labels: []
    property int currentIndex: 0
    property bool segmented: false
    signal selected(int index)
    implicitWidth: tabs.implicitWidth + 4; implicitHeight: 32; padding: 2
    background: Rectangle { radius: 7; color: HomeTheme.preview; border.color: HomeTheme.line }
    contentItem: RowLayout {
        id: tabs; spacing: 2
        Repeater {
            model: root.labels
            AbstractButton {
                id: tab; required property string modelData; required property int index
                readonly property bool chosen: root.currentIndex === index
                implicitWidth: Math.max(66,label.implicitWidth+24); implicitHeight: 28
                Layout.fillWidth: root.segmented; hoverEnabled: true
                Accessible.name: HomeTheme.t(modelData); Accessible.role: Accessible.PageTab; Accessible.selected: chosen
                background: Rectangle { radius: 5; color: tab.chosen ? HomeTheme.field : tab.hovered ? HomeTheme.hover : "transparent"; border.color: tab.visualFocus ? HomeTheme.accent : tab.chosen ? HomeTheme.outline : "transparent" }
                contentItem: Text { id: label; text: HomeTheme.t(tab.modelData); font.pixelSize: 12; font.weight: tab.chosen ? Font.Medium : Font.Normal; color: tab.chosen ? HomeTheme.text : HomeTheme.secondary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                onClicked: root.selected(index)
            }
        }
    }
}
