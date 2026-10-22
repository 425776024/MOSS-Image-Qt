import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Rectangle {
    id: root; objectName: "homeAvatarLibrary"
    color: HomeTheme.sidebar
    property string selectedId: ""
    property string notice: ""
    property url pendingAvatarSource
    readonly property var entries: workspace.avatars.filter(row => row.name.toLowerCase().includes(search.text.trim().toLowerCase()))
    readonly property var selected: entries.find(row => row.id === selectedId) || entries[0] || ({})
    signal useAvatar(string url)
    signal createAvatar()
    function designAvatar() { designDialog.open() }
    AvatarDesignDialog { id: designDialog; onAvatarSaved: { root.selectedId=workspace.avatars[0].id;root.notice="" } }
    function importAvatar() { imageDialog.open() }
    function previewAvatar(source) {
        pendingAvatarSource=source
        nameInput.text=decodeURIComponent(source.toString().split("/").pop()).replace(/\.[^.]+$/, "")
        notice=""
        importDialog.open()
    }
    function removeAvatar(id) { notice = workspace.removeAvatar(id) }
    FileDialog {
        id: imageDialog; objectName: "homeAvatarSourceDialog"; title: HomeTheme.t("导入形象"); nameFilters: ["Images (*.png *.jpg *.jpeg *.webp)"]
        onAccepted: root.previewAvatar(selectedFile)
    }
    HomeDialog {
        id: importDialog; objectName: "homeAvatarImportDialog"; title: HomeTheme.t("导入形象"); anchors.centerIn: Overlay.overlay; modal: true; width: 400
        dialogIcon: "person"
        defaultAction: importSave
        property bool saving: false
        onOpened: {saving=false;nameInput.forceActiveFocus();nameInput.selectAll()}
        ColumnLayout { width: parent.width; spacing: 12
            HomeImagePreview {
                id: importPreview; objectName: "homeAvatarImportPreview"; Layout.fillWidth: true; source: root.pendingAvatarSource
                HomeButton { anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 8; text: HomeTheme.t("更换图片"); compact: true; onClicked: imageDialog.open() }
            }
            HomeField { id: nameInput; objectName: "homeAvatarImportName"; Layout.fillWidth: true; placeholderText: HomeTheme.t("形象名称"); Accessible.name: placeholderText; maximumLength: 120 }
            Text { visible: root.notice !== ""; text: root.notice; color: HomeTheme.danger; wrapMode: Text.Wrap; Layout.fillWidth: true }
            RowLayout { Layout.alignment: Qt.AlignRight
                HomeButton { text: HomeTheme.t("取消"); onClicked: importDialog.close() }
                HomeButton { id: importSave; objectName: "homeAvatarImportSave"; text: HomeTheme.t("保存"); primary: true; enabled: !importDialog.saving && nameInput.text.trim() !== "" && importPreview.ready; onClicked: {if(!enabled)return;importDialog.saving=true;root.notice=workspace.importAvatar(root.pendingAvatarSource,nameInput.text);if(!root.notice)importDialog.close();else importDialog.saving=false} }
            }
        }
    }
    HomeDialog {
        id: renameDialog; objectName: "homeAvatarRenameDialog"; title: HomeTheme.t("重命名"); property string targetId: ""; anchors.centerIn: Overlay.overlay; width: 380; modal: true
        ColumnLayout { width: parent.width; spacing: 16
            HomeField { id: renameInput; Layout.fillWidth: true; maximumLength: 120; Accessible.name: HomeTheme.t("形象名称") }
            RowLayout { Layout.alignment: Qt.AlignRight
                HomeButton { text: HomeTheme.t("取消"); onClicked: renameDialog.close() }
                HomeButton { text: HomeTheme.t("保存"); primary: true; enabled: renameInput.text.trim() !== ""; onClicked: { root.notice = workspace.renameAvatar(renameDialog.targetId, renameInput.text); if(!root.notice)renameDialog.close() } }
            }
        }
    }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 22; spacing: 16
        HomeHeader { Layout.fillWidth: true; title: HomeTheme.t("我的形象"); detail: String(root.entries.length)
            HomeButton { text: HomeTheme.t("制作数字人"); iconName: "play"; enabled: !studio.busy; onClicked: root.createAvatar() }
            HomeButton { text: HomeTheme.t("文字生成形象"); iconName: "image"; primary: true; enabled: !studio.busy; onClicked: root.designAvatar() }
            HomeButton { text: HomeTheme.t("导入形象"); iconName: "plus"; enabled: !studio.busy; onClicked: root.importAvatar() }
        }
        RowLayout { Layout.fillWidth: true
            HomeField { id: search; Layout.preferredWidth: 240; search: true; placeholderText: HomeTheme.t("搜索形象"); Accessible.name: placeholderText }
            Item { Layout.fillWidth: true }
            IconButton { glyph: "folder"; hint: HomeTheme.t("打开目录"); onClicked: workspace.openDirectory(workspace.root+"/avatars") }
        }
        Item {
            Layout.fillWidth: true; Layout.fillHeight: true
            ColumnLayout { visible: root.entries.length===0; anchors.centerIn: parent; spacing: 16
                HomeEmpty { title: search.text ? HomeTheme.t("没有匹配的形象") : HomeTheme.t("还没有形象"); description: search.text ? "" : HomeTheme.t("输入文字生成形象，或导入图片，制作自己的数字人。"); iconName: "person" }
                RowLayout { visible: !search.text; Layout.alignment: Qt.AlignHCenter
                    HomeButton { text: HomeTheme.t("文字生成形象"); iconName: "image"; primary: true; enabled: !studio.busy; onClicked: root.designAvatar() }
                    HomeButton { text: HomeTheme.t("导入形象"); iconName: "plus"; enabled: !studio.busy; onClicked: root.importAvatar() }
                }
            }
            GridView {
                id: list; anchors.fill: parent; clip: true; model: root.entries
                cellWidth: width/Math.max(1,Math.floor(width/190)); cellHeight: 236
                ScrollBar.vertical: ScrollBar { }
                delegate: AbstractButton {
                    id: row; required property var modelData
                    width: list.cellWidth-12; height: list.cellHeight-12; padding: 8; hoverEnabled: true
                    readonly property bool chosen: root.selected.id===modelData.id
                    Accessible.name: modelData.name; Accessible.selected: chosen
                    onClicked: root.selectedId=modelData.id
                    onDoubleClicked: if(!studio.busy)root.useAvatar(modelData.url)
                    background: Rectangle { radius: 10; color: HomeTheme.field; border.color: row.chosen || row.visualFocus ? HomeTheme.accent : row.hovered ? HomeTheme.outline : HomeTheme.line }
                    contentItem: ColumnLayout { spacing: 8
                        Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: 6; color: HomeTheme.preview; clip: true
                            Image { anchors.fill: parent; anchors.margins: 1; source: row.modelData.url; fillMode: Image.PreserveAspectCrop; asynchronous: true; sourceSize: Qt.size(400,400) }
                        }
                        RowLayout { Layout.fillWidth: true; spacing: 4
                            Text { Layout.fillWidth: true; Layout.minimumWidth: 0; text: row.modelData.name; elide: Text.ElideRight; color: HomeTheme.text; font.pixelSize: 13; font.weight: Font.Medium }
                            IconButton { glyph: "more"; hint: HomeTheme.t("操作"); onClicked: {actions.target=row.modelData;const at=row.mapToItem(root,row.width-actions.width,row.height+4);actions.popup(at.x,at.y)} }
                        }
                    }
                    ToolTip.visible: hovered; ToolTip.delay: 700; ToolTip.text: modelData.name
                }
            }
        }
        HomeSection {
            visible: !!root.selected.id; Layout.fillWidth: true; padding: 10
            RowLayout { Layout.fillWidth: true; spacing: 10
                StudioIcon { name: "person"; Layout.preferredWidth: 18; Layout.preferredHeight: 18; ink: HomeTheme.secondary }
                Text { Layout.fillWidth: true; text: root.selected.name || ""; elide: Text.ElideRight; font.pixelSize: 12; color: HomeTheme.text }
                HomeButton { text: HomeTheme.t("用于创作"); iconName: "arrow"; primary: true; enabled: !studio.busy; onClicked: root.useAvatar(root.selected.url) }
            }
        }
        RowLayout { visible: !!root.notice || workspace.canRestoreAvatar; Layout.fillWidth: true
            Text { Layout.fillWidth: true; text: root.notice; wrapMode: Text.Wrap; color: HomeTheme.danger; font.pixelSize: 12 }
            HomeButton { visible: workspace.canRestoreAvatar; quiet: true; text: HomeTheme.t("恢复上次移除"); onClicked: root.notice=workspace.restoreAvatar() }
        }
    }
    HomeMenu { id: actions; property var target: ({})
        AppMenuItem { text: HomeTheme.t("用于创作"); enabled: !studio.busy; onTriggered: root.useAvatar(actions.target.url) }
        AppMenuItem { text: HomeTheme.t("重命名"); onTriggered: { renameDialog.targetId = actions.target.id; renameInput.text = actions.target.name; renameDialog.open() } }
        AppMenuItem { text: HomeTheme.t("移除"); destructive: true; onTriggered: root.removeAvatar(actions.target.id) }
    }
}
