import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property bool grid: false
    property bool history: false
    property bool sortByName: false
    property string selectedId: ""
    signal openEditor()
    signal importProject()
    signal renameRequested(int index, string name)
    readonly property var entries: {
        const source = history ? studio.history : studio.creationPages
        const result = source.map((row, index) => ({row:row, index:index, name:history ? row.title : HomeTheme.projectName(row)}))
            .filter(item => item.name.toLowerCase().includes(search.text.trim().toLowerCase()))
        return result.sort((a,b) => sortByName ? a.name.localeCompare(b.name) : String(b.row.modified || b.row.created || b.row.title || "").localeCompare(String(a.row.modified || a.row.created || a.row.title || "")))
    }
    function createProject() { root.forceActiveFocus(); studio.newPage(); openEditor() }
    function openWork(index) { root.forceActiveFocus(); if(history)studio.selectHistory(index); else studio.selectPage(index); openEditor() }
    function duplicate(index) { root.forceActiveFocus(); studio.duplicatePage(index) }
    function remove(index) { root.forceActiveFocus(); studio.deletePage(index) }
    function thumbnail(row) { return row.visual || row.image || (!row.kind || row.kind === "image" || row.kind === "edit" ? row.url : "") || "" }
    function modified(row) {
        const date = new Date(row.modified || row.created || "")
        if(isNaN(date.getTime()))return "—"
        return Qt.formatDateTime(date, "MM-dd hh:mm")
    }
    ColumnLayout {
        anchors.fill: parent; anchors.leftMargin: root.width < 1000 ? 20 : 30; anchors.rightMargin: root.width < 1000 ? 20 : 30; anchors.topMargin: 22; anchors.bottomMargin: 14; spacing: 0
        HomeHeader {
            Layout.fillWidth: true; title: "我的作品"; detail: HomeTheme.count(root.history ? studio.history.length : studio.creationPages.length, "作品")
            HomeButton { text: HomeTheme.t("导入工程"); iconName: "folder"; enabled: !studio.busy; onClicked: root.importProject() }
            HomeButton { objectName: "homeCreateProject"; text: HomeTheme.t("新建作品"); iconName: "plus"; primary: true; enabled: !studio.busy; onClicked: root.createProject() }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.topMargin: 18; spacing: 12
            HomeField { id: search; objectName: "homeWorkSearch"; search: true; Layout.fillWidth: true; placeholderText: HomeTheme.t("搜索作品"); Accessible.name: placeholderText }
            HomeButton {
                text: HomeTheme.t(root.sortByName ? "作品名称" : "最近修改"); iconName: "chevron"
                onClicked: sortMenu.popup()
                Menu { id: sortMenu
                    MenuItem { text: HomeTheme.t("最近修改"); checkable: true; checked: !root.sortByName; onTriggered: root.sortByName = false }
                    MenuItem { text: HomeTheme.t("作品名称"); checkable: true; checked: root.sortByName; onTriggered: root.sortByName = true }
                }
            }
            RowLayout { spacing: 0
                HomeButton { iconName: "grid"; hint: HomeTheme.t("缩略图视图"); active: root.grid; onClicked: root.grid = true }
                HomeButton { iconName: "list"; hint: HomeTheme.t("列表视图"); active: !root.grid; onClicked: root.grid = false }
            }
        }
        HomeTabs { Layout.topMargin: 12; labels: ["创作工程", "生成记录"]; currentIndex: root.history ? 1 : 0; onSelected: index => root.history = index === 1 }
        Rectangle { Layout.fillWidth: true; height: 1; color: HomeTheme.line }
        RowLayout {
            visible: !root.grid && root.entries.length > 0; Layout.fillWidth: true; Layout.topMargin: 12; Layout.preferredHeight: 38; spacing: 12
            Text { Layout.fillWidth: true; Layout.preferredWidth: 350; text: HomeTheme.t("作品名称"); color: HomeTheme.secondary; font.pixelSize: 15 }
            Text { Layout.preferredWidth: 110; text: HomeTheme.t("类型"); color: HomeTheme.secondary; font.pixelSize: 15 }
            Text { Layout.preferredWidth: 110; text: HomeTheme.t("画布"); color: HomeTheme.secondary; font.pixelSize: 15 }
            Text { Layout.preferredWidth: 145; text: HomeTheme.t("最近修改"); color: HomeTheme.secondary; font.pixelSize: 15 }
            Item { Layout.preferredWidth: 115 }
        }
        Item {
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.topMargin: root.grid ? 16 : 0
            HomeEmpty {
                anchors.centerIn: parent; visible: root.entries.length === 0
                title: search.text ? "没有找到匹配的作品" : "从第一件作品开始"; description: "新建图像作品，或导入已有工程继续创作。"; iconName: "document"
            }
            ListView {
                id: list; visible: !root.grid; anchors.fill: parent; clip: true; model: root.entries
                ScrollBar.vertical: ScrollBar { }
                delegate: Rectangle {
                    id: rowDelegate
                    required property var modelData
                    width: list.width; height: 80; color: root.selectedId === String(modelData.row.id || modelData.row.title) ? HomeTheme.selection : rowHover.hovered ? HomeTheme.hover : "transparent"
                    HoverHandler { id: rowHover }
                    TapHandler { onTapped: root.selectedId = String(modelData.row.id || modelData.row.title); onDoubleTapped: root.openWork(modelData.index) }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: HomeTheme.line }
                    RowLayout {
                        anchors.fill: parent; spacing: 12
                        Rectangle {
                            Layout.preferredWidth: 148; Layout.preferredHeight: 66; radius: 6; color: HomeTheme.preview; clip: true
                            Image { anchors.fill: parent; source: root.thumbnail(rowDelegate.modelData.row); fillMode: Image.PreserveAspectFit; asynchronous: true }
                            StudioIcon { visible: !root.thumbnail(rowDelegate.modelData.row); anchors.centerIn: parent; width: 29; height: 29; name: "image"; ink: HomeTheme.faint }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true; Layout.preferredWidth: 190; spacing: 5
                            Text { Layout.fillWidth: true; text: rowDelegate.modelData.name; elide: Text.ElideRight; color: HomeTheme.text; font.pixelSize: 18; font.weight: Font.DemiBold }
                            Text { text: HomeTheme.t(root.history ? "生成记录" : "本地作品"); color: HomeTheme.secondary; font.pixelSize: 14 }
                        }
                        Text { Layout.preferredWidth: 110; text: HomeTheme.t(root.history ? (modelData.row.kind === "image" ? "图像创作" : modelData.row.kind === "edit" ? "局部重绘" : "图片数字人") : modelData.row.video ? "图片数字人" : "图像创作"); color: HomeTheme.secondary; font.pixelSize: 15 }
                        Text { Layout.preferredWidth: 110; text: modelData.row.document ? modelData.row.document.width + " × " + modelData.row.document.height : "—"; color: HomeTheme.secondary; font.pixelSize: 14 }
                        Text { Layout.preferredWidth: 145; text: root.modified(modelData.row); color: HomeTheme.secondary; font.pixelSize: 15 }
                        HomeButton { text: HomeTheme.t("打开"); quiet: true; enabled: !studio.busy; onClicked: root.openWork(rowDelegate.modelData.index) }
                        HomeButton {
                            visible: !root.history; implicitWidth: 32; iconName: "more"; quiet: true; hint: HomeTheme.english ? "Project actions" : "作品管理"; enabled: !studio.busy
                            onClicked: { actions.targetIndex = rowDelegate.modelData.index; actions.targetName = rowDelegate.modelData.name; actions.popup() }
                        }
                    }
                }
            }
            GridView {
                id: gridView; anchors.fill: parent; visible: root.grid; clip: true; model: root.entries
                cellWidth: width / Math.max(1, Math.floor(width / 260)); cellHeight: cellWidth * .72 + 66
                ScrollBar.vertical: ScrollBar { }
                delegate: Item {
                    id: gridDelegate; required property var modelData; width: gridView.cellWidth; height: gridView.cellHeight
                    Rectangle {
                        anchors.fill: parent; anchors.rightMargin: 16; anchors.bottomMargin: 18; color: HomeTheme.field; border.color: tileHover.hovered ? HomeTheme.accent : HomeTheme.line; radius: 8
                        HoverHandler { id: tileHover }
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 10; spacing: 8
                            Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; color: HomeTheme.preview; radius: 5; clip: true
                                Image { anchors.fill: parent; source: root.thumbnail(gridDelegate.modelData.row); fillMode: Image.PreserveAspectFit; asynchronous: true }
                                StudioIcon { visible: !root.thumbnail(gridDelegate.modelData.row); anchors.centerIn: parent; width: 38; height: 38; name: "image"; ink: HomeTheme.faint }
                                TapHandler { onTapped: root.openWork(gridDelegate.modelData.index) }
                            }
                            RowLayout { Layout.fillWidth: true
                                Text { Layout.fillWidth: true; text: gridDelegate.modelData.name; color: HomeTheme.text; font.pixelSize: 16; font.weight: Font.Medium; elide: Text.ElideRight }
                                HomeButton { visible: !root.history; iconName: "more"; quiet: true; implicitWidth: 32; implicitHeight: 28; onClicked: { actions.targetIndex = gridDelegate.modelData.index; actions.targetName = gridDelegate.modelData.name; actions.popup() } }
                            }
                            Text { text: root.modified(gridDelegate.modelData.row); color: HomeTheme.secondary; font.pixelSize: 13 }
                        }
                    }
                }
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: HomeTheme.line }
        RowLayout { Layout.fillWidth: true; Layout.topMargin: 8
            Text { text: HomeTheme.count(root.entries.length, "作品"); color: HomeTheme.secondary; font.pixelSize: 14 }
            Item { Layout.fillWidth: true }
            HomeButton { visible: studio.canRestorePage; text: HomeTheme.t("恢复上次移除"); quiet: true; implicitHeight: 32; enabled: !studio.busy; onClicked: studio.restoreDeletedPage() }
            HomeButton { text: HomeTheme.t("打开作品目录"); iconName: "folder"; quiet: true; implicitHeight: 32; onClicked: studio.openOutputs() }
        }
    }
    Menu { id: actions; property int targetIndex: -1; property string targetName: ""
        MenuItem { text: HomeTheme.t("打开"); onTriggered: root.openWork(actions.targetIndex) }
        MenuItem { text: HomeTheme.t("复制"); onTriggered: root.duplicate(actions.targetIndex) }
        MenuItem { text: HomeTheme.t("重命名"); onTriggered: root.renameRequested(actions.targetIndex, actions.targetName) }
        MenuSeparator { }
        MenuItem { text: HomeTheme.t("移除"); onTriggered: root.remove(actions.targetIndex) }
    }
}
