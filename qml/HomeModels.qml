import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    id: root
    signal openEditor()
    property string deleteId: ""
    property string deleteTitle: ""
    FolderDialog { id: modelDialog; title: "选择模型存放目录"; onAccepted: studio.setModelRoot(selectedFolder) }
    HomeDialog {
        id: deleteDialog; title: "删除 "+root.deleteTitle
        contentItem: ColumnLayout {
            spacing: 16
            Text { Layout.fillWidth: true; text: studio.models.deletionDescription(root.deleteId); wrapMode: Text.Wrap; color: HomeTheme.text }
            RowLayout { Layout.alignment: Qt.AlignRight
                HomeButton { text: "取消"; onClicked: deleteDialog.close() }
                HomeButton { text: "删除模型文件"; enabled: !studio.busy; onClicked: {studio.models.removeGroup(root.deleteId);deleteDialog.close()} }
            }
        }
    }
    ScrollView {
        id: view; anchors.fill: parent; contentWidth: availableWidth; clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            width: Math.min(840,view.availableWidth); spacing: 14
            HomeSection {
                Layout.fillWidth: true; title: "模型资源"; iconName: "model"
                Text { Layout.fillWidth: true; text: "按需下载，文件保存在本机。Q4 与 Q8 的共用组件单独管理。"; color: HomeTheme.secondary; font.pixelSize: 12; wrapMode: Text.Wrap }
                RowLayout { Layout.fillWidth: true
                    Text { Layout.fillWidth: true; Layout.minimumWidth: 0; text: studio.modelRoot; elide: Text.ElideMiddle; color: HomeTheme.secondary; font.pixelSize: 11 }
                    IconButton { glyph: "folder"; hint: "打开模型目录"; onClicked: studio.models.openRoot() }
                    HomeButton { text: "更改目录"; quiet: true; enabled: !studio.busy; onClicked: modelDialog.open() }
                }
                ComboBox {
                    Layout.fillWidth: true; enabled: !studio.busy
                    model: ["Hugging Face · huggingface.co", "国内镜像 · hf-mirror.com"]
                    currentIndex: studio.models.endpoint==="https://hf-mirror.com" ? 1 : 0
                    onActivated: studio.models.endpoint=currentIndex===1 ? "https://hf-mirror.com" : "https://huggingface.co"
                }
                RowLayout { Layout.fillWidth: true; enabled: !studio.busy
                    HomeChoiceCard { objectName: "imageQuantQ4"; Layout.fillWidth: true; title: "MOSS Q4"; detail: "整套约 9.28 GB"; selected: studio.imageQuant==="Q4_K"; onClicked: studio.imageQuant="Q4_K" }
                    HomeChoiceCard { objectName: "imageQuantQ8"; Layout.fillWidth: true; title: "MOSS Q8"; detail: "整套约 11.66 GB"; selected: studio.imageQuant==="Q8_0"; onClicked: studio.imageQuant="Q8_0" }
                    HomeButton { text: "准备所选版本"; primary: true; onClicked: studio.models.requestDownload("image") }
                }
            }
            Repeater {
                model: studio.models.groups
                HomeSection {
                    id: card; required property var modelData
                    Layout.fillWidth: true; title: modelData.title; iconName: "model"
                    RowLayout { Layout.fillWidth: true
                        Text { Layout.fillWidth: true; text: card.modelData.description; wrapMode: Text.Wrap; color: HomeTheme.secondary; font.pixelSize: 12 }
                        Text { text: card.modelData.ready ? "已安装" : card.modelData.installedBytes>0 ? "待补齐" : "未安装"; color: card.modelData.ready ? HomeTheme.accent : HomeTheme.muted; font.pixelSize: 12 }
                    }
                    Text { text: "本机占用 / 完整大小："+card.modelData.sizeText; color: HomeTheme.secondary; font.pixelSize: 11 }
                    Text { Layout.fillWidth: true; text: card.modelData.sources; wrapMode: Text.Wrap; color: HomeTheme.muted; font.pixelSize: 10 }
                    RowLayout { Layout.alignment: Qt.AlignRight; enabled: !studio.busy
                        HomeButton { text: "来源"; quiet: true; onClicked: Qt.openUrlExternally(card.modelData.sourceUrl) }
                        HomeButton { text: "校验"; quiet: true; enabled: card.modelData.installedBytes>0; onClicked: studio.models.verifyGroup(card.modelData.id) }
                        HomeButton { text: "删除"; quiet: true; enabled: card.modelData.installedBytes>0; onClicked: {root.deleteId=card.modelData.id;root.deleteTitle=card.modelData.title;deleteDialog.open()} }
                        HomeButton { text: card.modelData.ready ? "修复" : "下载"; primary: !card.modelData.ready; onClicked: studio.models.requestDownload(card.modelData.id) }
                    }
                }
            }
            HomeSection {
                Layout.fillWidth: true; title: "下载与清理"; iconName: "download"
                ProgressBar { Layout.fillWidth: true; visible: studio.models.busy; value: studio.models.progress }
                Text { Layout.fillWidth: true; text: studio.models.error || studio.models.status || "暂无下载任务"; wrapMode: Text.Wrap; color: studio.models.error ? HomeTheme.danger : HomeTheme.secondary; font.pixelSize: 12 }
                RowLayout { Layout.fillWidth: true
                    Text { Layout.fillWidth: true; text: "可续传的残留："+(studio.models.partialBytes/1048576).toFixed(1)+" MB"; color: HomeTheme.secondary; font.pixelSize: 12 }
                    HomeButton { text: "清理下载残留"; enabled: !studio.busy&&studio.models.partialBytes>0; onClicked: studio.models.cleanPartials() }
                    HomeButton { visible: studio.models.busy; text: "取消"; onClicked: studio.models.cancel() }
                    HomeButton { text: "刷新"; enabled: !studio.busy; onClicked: studio.models.refresh() }
                }
            }
            HomeRemoteGpuSettings { visible: studio.remoteGpu.available; Layout.fillWidth: true; onResumeRequested: root.openEditor() }
        }
    }
}
