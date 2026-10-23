import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Rectangle {
    id: root; color: HomeTheme.sidebar
    signal templateUsed()
    property string message: ""
    property string selectedId: ""
    readonly property var selected: studio.layoutTemplates.find(t=>t.id===selectedId) || studio.layoutTemplates[0] || ({})
    function templateName(row) { return HomeTheme.english && row.nameEn ? row.nameEn : HomeTheme.t(row.name || "") }
    function useTemplate(id) { if(!studio.busy){studio.useLayoutTemplate(id);root.templateUsed()} }
    FileDialog { id: importer; title: HomeTheme.t("导入构图模板"); nameFilters: [HomeTheme.t("Moss 工程或构图 JSON (*.json)")]; onAccepted: root.message=studio.importLayoutTemplate(selectedFile) }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 22; spacing: 16
        HomeHeader { Layout.fillWidth: true; title: HomeTheme.t("构图模板"); detail: String(studio.layoutTemplates.length)
            HomeButton { text: HomeTheme.t("导入模板"); iconName: "plus"; onClicked: importer.open() }
        }
        Text { visible: !!root.message; Layout.fillWidth: true; text: root.message; color: HomeTheme.danger; font.pixelSize: 12; wrapMode: Text.Wrap }
        GridView {
            id: grid; Layout.fillWidth: true; Layout.fillHeight: true; clip: true
            cellWidth: width/Math.max(1,Math.floor(width/230)); cellHeight: 252; model: studio.layoutTemplates; ScrollBar.vertical: ScrollBar { }
            delegate: AbstractButton {
                id: tile; required property var modelData
                width: grid.cellWidth-12; height: grid.cellHeight-12; padding: 10; hoverEnabled: true
                Accessible.name: modelData.name; Accessible.selected: root.selected.id===modelData.id
                onClicked: root.selectedId=modelData.id
                onDoubleClicked: root.useTemplate(modelData.id)
                background: Rectangle { radius: 10; color: HomeTheme.field; border.color: root.selected.id===tile.modelData.id || tile.visualFocus ? HomeTheme.accent : HomeTheme.line }
                contentItem: ColumnLayout {
                    spacing: 10
                    Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; color: HomeTheme.preview; radius: 6; clip: true
                        Rectangle {
                            id: paper; readonly property real scale: Math.min((parent.width-24)/tile.modelData.document.width,(parent.height-24)/tile.modelData.document.height)
                            anchors.centerIn: parent; width: tile.modelData.document.width*scale; height: tile.modelData.document.height*scale; color: HomeTheme.field; border.color: HomeTheme.outline
                            Repeater { model: tile.modelData.document.regions || []
                                Rectangle {
                                    id: region; required property var modelData
                                    x: modelData.x*paper.scale; y: modelData.y*paper.scale; width: Math.max(3,modelData.width*paper.scale); height: Math.max(3,modelData.height*paper.scale)
                                    color: Qt.alpha(modelData.color || HomeTheme.accent,.14); border.color: modelData.color || HomeTheme.accent; clip: true
                                    Text { anchors.fill: parent; anchors.margins: 3; text: region.modelData.type==="text" ? region.modelData.text || HomeTheme.t("文字") : region.modelData.name; font.pixelSize: 9; color: HomeTheme.secondary; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignHCenter }
                                }
                            }
                        }
                    }
                    RowLayout { Layout.fillWidth: true
                        ColumnLayout { Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 3
                            Text { Layout.fillWidth: true; text: root.templateName(tile.modelData); color: HomeTheme.text; font.pixelSize: 13; font.weight: Font.Medium; elide: Text.ElideRight }
                            Text { text: tile.modelData.document.width+" × "+tile.modelData.document.height; color: HomeTheme.muted; font.pixelSize: 10 }
                        }
                        IconButton { visible: !tile.modelData.builtin; glyph: "trash"; hint: HomeTheme.t("移除模板（源文件保留）"); onClicked: studio.removeLayoutTemplate(tile.modelData.id) }
                    }
                }
            }
        }
        HomeSection { visible: !!root.selected.id; Layout.fillWidth: true; padding: 10
            RowLayout { Layout.fillWidth: true; spacing: 10
                StudioIcon { Layout.preferredWidth: 18; Layout.preferredHeight: 18; name: "grid"; ink: HomeTheme.secondary }
                Text { Layout.fillWidth: true; text: root.templateName(root.selected); font.pixelSize: 12; color: HomeTheme.text }
                HomeButton { objectName: "useLibraryTemplate"; text: HomeTheme.t("使用模板"); primary: true; iconName: "arrow"; enabled: !studio.busy; onClicked: root.useTemplate(root.selected.id) }
            }
        }
    }
}
