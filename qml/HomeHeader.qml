import QtQuick
import QtQuick.Layouts

RowLayout {
    id: root; objectName: "homePageHeader"
    property string title: ""
    property string detail: ""
    default property alias actions: actionsRow.data
    Layout.fillHeight: false
    Layout.preferredHeight: 32; Layout.maximumHeight: 32
    spacing: 10
    Text { text: HomeTheme.t(root.title); color: HomeTheme.text; font.pixelSize: 19; font.weight: Font.DemiBold }
    Text { visible: root.detail !== ""; text: root.detail; color: HomeTheme.muted; font.pixelSize: 12; elide: Text.ElideRight; Layout.maximumWidth: root.width*.3 }
    Item { Layout.fillWidth: true; Layout.preferredHeight: 32
        WindowDragArea { anchors.fill: parent }
    }
    RowLayout { id: actionsRow; spacing: 8 }
}
