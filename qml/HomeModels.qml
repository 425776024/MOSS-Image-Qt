import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    id: root
    objectName: "homeModels"
    signal openEditor()
    property string deleteId: ""
    property string deleteTitle: ""
    property bool optionsExpanded: false
    property bool filesExpanded: false
    readonly property var groups: studio.models.groups
    readonly property bool locked: studio.busy || studio.models.busy
    readonly property var resources: [
        {id:"image",title:"图像创作",icon:"image",ids:["image-q4","image-q8","image-shared"]},
        {id:"ditto",title:"数字人",icon:"person",ids:["ditto"]},
        {id:"voice",title:"语音与音色",icon:"audio",ids:["voice"]},
        {id:"sfx",title:"音效",icon:"volume",ids:["sfx"]}
    ]
    readonly property real occupiedBytes: groups.reduce((sum,g)=>sum+g.installedBytes,0)
    function group(id) {return groups.find(g=>g.id===id) || {installedBytes:0,totalBytes:0,ready:false}}
    function selectedIds(row) {return row.id==="image" ? ["image-shared",studio.imageQuant==="Q4_K" ? "image-q4" : "image-q8"] : row.ids}
    function installed(row) {return row.ids.reduce((sum,id)=>sum+group(id).installedBytes,0)}
    function ready(row) {return selectedIds(row).every(id=>group(id).ready)}
    function size(bytes) {return bytes>=1e9 ? (bytes/1e9).toFixed(2)+" GB" : bytes>=1e6 ? (bytes/1e6).toFixed(1)+" MB" : "0 MB"}
    function space(row) {
        const current=installed(row)
        return current>0 ? HomeTheme.t("占用")+" "+size(current)
            : size(selectedIds(row).reduce((sum,id)=>sum+group(id).totalBytes,0))
    }
    function confirmDelete(id,title) {deleteId=id;deleteTitle=title;deleteDialog.open()}
    function removeSelected() {
        const ids=deleteId==="image" ? ["image-q4","image-q8","image-shared"] : [deleteId]
        for(const id of ids)studio.models.removeGroup(id)
        deleteDialog.close()
    }
    FolderDialog { id: modelDialog; title: HomeTheme.t("选择模型存放目录"); onAccepted: studio.setModelRoot(selectedFolder) }
    HomeDialog {
        id: deleteDialog; objectName: "deleteResourceDialog"
        title: HomeTheme.t("删除")+" "+HomeTheme.t(root.deleteTitle)+"？"
        dialogIcon: "trash"; tone: HomeTheme.danger; defaultAction: deleteConfirm
        contentItem: ColumnLayout {
            spacing: 12
            Text { Layout.fillWidth: true; text: HomeTheme.t("删除后需重新下载，作品不受影响。"); wrapMode: Text.Wrap; color: HomeTheme.secondary; font.pixelSize: 13 }
            RowLayout { Layout.alignment: Qt.AlignRight
                HomeButton { text: HomeTheme.t("取消"); onClicked: deleteDialog.close() }
                HomeButton { id: deleteConfirm; text: HomeTheme.t("删除"); primary: true; enabled: !root.locked; onClicked: root.removeSelected() }
            }
        }
    }
    HomeMenu {
        id: resourceOptions
        AppMenuItem { text: HomeTheme.t("打开资源文件夹"); glyph: "folder"; onTriggered: studio.models.openRoot() }
        AppMenuItem { text: HomeTheme.t("下载设置"); glyph: "settings"; onTriggered: root.optionsExpanded=!root.optionsExpanded }
        AppMenuItem { text: HomeTheme.t("文件管理"); glyph: "document"; onTriggered: root.filesExpanded=!root.filesExpanded }
        AppMenuItem { text: HomeTheme.t("清理下载缓存"); glyph: "trash"; enabled: !root.locked && studio.models.partialBytes>0; onTriggered: studio.models.cleanPartials() }
        AppMenuItem { text: HomeTheme.t("刷新"); glyph: "refresh"; enabled: !root.locked; onTriggered: studio.models.refresh() }
    }
    ScrollView {
        id: view; anchors.fill: parent; contentWidth: availableWidth; clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            width: Math.min(760,view.availableWidth); x: (view.availableWidth-width)/2; spacing: 12
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                Text { text: HomeTheme.t("创作资源"); color: HomeTheme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
                Item { Layout.fillWidth: true }
                Text { text: HomeTheme.t("占用")+" "+root.size(root.occupiedBytes); color: HomeTheme.secondary; font.pixelSize: 11 }
                IconButton { objectName: "resourceSettingsMenu"; glyph: "more"; hint: HomeTheme.t("资源选项"); onClicked: resourceOptions.popup() }
            }
            HomeSection {
                Layout.fillWidth: true; padding: 12; contentSpacing: 0
                RowLayout {
                    Layout.fillWidth: true; Layout.bottomMargin: 8; spacing: 8
                    Text { Layout.fillWidth: true; text: HomeTheme.t("图像质量"); color: HomeTheme.text; font.pixelSize: 13 }
                    HomeCombo {
                        objectName: "imageQualitySelect"; Layout.preferredWidth: 142; implicitHeight: 28; enabled: !root.locked
                        model: [HomeTheme.t("标准"),HomeTheme.t("高精度")]
                        currentIndex: studio.imageQuant==="Q4_K" ? 0 : 1
                        onActivated: studio.imageQuant=currentIndex===0 ? "Q4_K" : "Q8_0"
                    }
                }
                Repeater {
                    model: root.resources
                    ColumnLayout {
                        id: resource; required property var modelData
                        objectName: "resourceRow-"+modelData.id
                        Layout.fillWidth: true; spacing: 0
                        readonly property bool available: root.ready(modelData)
                        readonly property real bytes: root.installed(modelData)
                        Rectangle { Layout.fillWidth: true; height: VideoCutTheme.hairline; color: HomeTheme.line }
                        RowLayout {
                            Layout.fillWidth: true; Layout.topMargin: 10; Layout.bottomMargin: 10; spacing: 10
                            StudioIcon { name: resource.modelData.icon; ink: HomeTheme.secondary; Layout.preferredWidth: 18; Layout.preferredHeight: 18 }
                            ColumnLayout {
                                Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 3
                                Text { Layout.fillWidth: true; text: HomeTheme.t(resource.modelData.title); color: HomeTheme.text; font.pixelSize: 13; elide: Text.ElideRight }
                                Text { text: root.space(resource.modelData); color: HomeTheme.secondary; font.pixelSize: 11 }
                            }
                            Text { text: HomeTheme.t(resource.available ? "可用" : resource.bytes>0 ? "待补齐" : "未下载"); color: HomeTheme.secondary; font.pixelSize: 11 }
                            HomeButton { objectName: "downloadResource-"+resource.modelData.id; visible: !resource.available; text: HomeTheme.t("下载"); implicitHeight: 28; enabled: !root.locked; onClicked: studio.models.requestDownload(resource.modelData.id) }
                            IconButton { objectName: "resourceMenu-"+resource.modelData.id; glyph: "more"; hint: HomeTheme.t("更多操作"); onClicked: rowMenu.popup() }
                            HomeMenu {
                                id: rowMenu
                                AppMenuItem { text: HomeTheme.t("检查资源"); enabled: !root.locked && resource.bytes>0; onTriggered: studio.models.verifyGroup(resource.modelData.id) }
                                AppMenuItem { text: HomeTheme.t("重新下载"); enabled: !root.locked; onTriggered: studio.models.requestDownload(resource.modelData.id) }
                                AppMenuItem { text: HomeTheme.t("删除"); destructive: true; enabled: !root.locked && resource.bytes>0; onTriggered: root.confirmDelete(resource.modelData.id,resource.modelData.title) }
                            }
                        }
                    }
                }
            }
            RowLayout {
                visible: studio.models.busy; Layout.fillWidth: true; spacing: 10
                Text { text: HomeTheme.t("正在准备…"); color: HomeTheme.secondary; font.pixelSize: 12 }
                Basic.ProgressBar {
                    id: resourceProgress; Layout.fillWidth: true; Layout.preferredHeight: 6; value: studio.models.progress; padding: 0
                    Accessible.name: HomeTheme.t("下载进度")
                    background: Rectangle { implicitHeight: 6; radius: 3; color: HomeTheme.dialogInset }
                    contentItem: Item { Rectangle { width: resourceProgress.visualPosition*parent.width; height: parent.height; radius: 3; color: HomeTheme.accent } }
                }
                Text { text: Math.floor(studio.models.progress*100)+"%"; color: HomeTheme.secondary; font.pixelSize: 11 }
                HomeButton { text: HomeTheme.t("取消"); compact: true; onClicked: studio.models.cancel() }
            }
            Text { visible: !!studio.models.error; Layout.fillWidth: true; text: HomeTheme.t("资源操作未完成，请重试"); color: HomeTheme.danger; font.pixelSize: 12 }
            HomeSection {
                visible: root.optionsExpanded; Layout.fillWidth: true; padding: 12; contentSpacing: 8
                RowLayout { Layout.fillWidth: true
                    Text { Layout.fillWidth: true; text: HomeTheme.t("下载设置"); color: HomeTheme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
                    IconButton { glyph: "close"; hint: HomeTheme.t("关闭"); onClicked: root.optionsExpanded=false }
                }
                RowLayout { Layout.fillWidth: true; spacing: 12
                    Text { text: HomeTheme.t("下载源"); color: HomeTheme.secondary; font.pixelSize: 12 }
                    HomeCombo { Layout.fillWidth: true; implicitHeight: 28; enabled: !root.locked; model: studio.models.sourceNames.map(n=>HomeTheme.t(n)); currentIndex: studio.models.sourceValues.indexOf(studio.models.endpoint); onActivated: studio.models.endpoint=studio.models.sourceValues[currentIndex] }
                }
                HomeButton { visible: studio.models.rootSelectable; text: HomeTheme.t("更改目录"); quiet: true; compact: true; enabled: !root.locked; onClicked: modelDialog.open() }
            }
            HomeSection {
                visible: root.filesExpanded; Layout.fillWidth: true; padding: 12; contentSpacing: 4
                RowLayout { Layout.fillWidth: true
                    Text { Layout.fillWidth: true; text: HomeTheme.t("文件管理"); color: HomeTheme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
                    IconButton { glyph: "close"; hint: HomeTheme.t("关闭"); onClicked: root.filesExpanded=false }
                }
                Repeater {
                    model: root.groups
                    RowLayout {
                        id: fileRow; required property var modelData; Layout.fillWidth: true; spacing: 10
                        Text { Layout.fillWidth: true; Layout.minimumWidth: 0; text: fileRow.modelData.title; elide: Text.ElideRight; color: HomeTheme.secondary; font.pixelSize: 12 }
                        Text { text: root.size(fileRow.modelData.installedBytes); color: HomeTheme.secondary; font.pixelSize: 11 }
                        IconButton { glyph: "info"; hint: HomeTheme.t("来源"); onClicked: Qt.openUrlExternally(fileRow.modelData.sourceUrl) }
                        IconButton { glyph: "trash"; hint: HomeTheme.t("删除"); enabled: !root.locked && fileRow.modelData.installedBytes>0; onClicked: root.confirmDelete(fileRow.modelData.id,fileRow.modelData.title) }
                    }
                }
            }
            HomeRemoteGpuSettings { visible: studio.remoteGpu.available; Layout.fillWidth: true; onResumeRequested: root.openEditor() }
        }
    }
}
