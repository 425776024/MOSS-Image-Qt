import QtQuick
import QtQuick.Layouts

CardSurface {
    id: root
    property string title: ""
    property string iconName: ""
    property int padding: 16
    property int contentSpacing: 12
    default property alias body: contents.data
    implicitHeight: contents.implicitHeight + padding * 2
    outlined: true; radius: 10; baseColor: HomeTheme.field
    ColumnLayout {
        id: contents
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        anchors.margins: root.padding; spacing: root.contentSpacing
        RowLayout {
            visible: !!root.title; Layout.fillWidth: true; spacing: 7
            StudioIcon { visible: !!root.iconName; name: root.iconName; ink: HomeTheme.secondary; Layout.preferredWidth: 16; Layout.preferredHeight: 16 }
            Text { Layout.fillWidth: true; text: HomeTheme.t(root.title); color: HomeTheme.text; font.pixelSize: 13; font.weight: Font.DemiBold; elide: Text.ElideRight }
        }
    }
}
