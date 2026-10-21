import QtQuick
import QtQuick.Controls

TextField {
    id: root
    property bool search: false
    implicitHeight: 32; leftPadding: search ? 32 : 10; rightPadding: search && text ? 30 : 10
    font.pixelSize: 13; color: HomeTheme.text; placeholderTextColor: HomeTheme.faint; selectByMouse: true
    selectionColor: HomeTheme.accent; selectedTextColor: HomeTheme.accentInk
    background: Rectangle { radius: 7; color: HomeTheme.field; border.width: root.activeFocus ? 1.5 : 1; border.color: root.activeFocus ? HomeTheme.accent : HomeTheme.outline }
    StudioIcon { visible: root.search; name: "search"; x: root.height<=32 ? 10 : 14; anchors.verticalCenter: parent.verticalCenter; ink: HomeTheme.secondary; width: root.height<=32 ? 16 : 23; height: width }
    HomeButton { visible: root.search && root.text !== ""; anchors.right: parent.right; width: root.height<=32 ? 28 : 38; height: parent.height; quiet: true; iconName: "close"; hint: HomeTheme.english ? "Clear search" : "清除搜索"; onClicked: root.clear() }
}
