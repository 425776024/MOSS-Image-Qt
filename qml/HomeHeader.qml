import QtQuick
import QtQuick.Layouts

RowLayout {
    id: root
    property string title: ""
    property string detail: ""
    default property alias actions: actionsRow.data
    spacing: 10
    Text { text: HomeTheme.t(root.title); color: HomeTheme.text; font.pixelSize: 19; font.weight: Font.DemiBold }
    Text { visible: root.detail !== ""; text: root.detail; color: HomeTheme.muted; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true }
    Item { visible: root.detail === ""; Layout.fillWidth: true }
    RowLayout { id: actionsRow; spacing: 8 }
}
