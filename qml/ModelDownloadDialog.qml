import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

HomeDialog {
    id: dialog
    objectName: "modelDownloadDialog"
    title: "准备模型"
    width: Math.min(580,Overlay.overlay ? Overlay.overlay.width-40 : 580)
    closePolicy: studio.models.busy ? Popup.NoAutoClose : Popup.CloseOnEscape
    Connections { target: studio.models; function onDownloadRequested() { dialog.open() } }
    onClosed: studio.models.dismissRequest()
    contentItem: ColumnLayout {
        spacing: 14
        Text { Layout.fillWidth: true; text: studio.models.requestDescription; wrapMode: Text.Wrap; color: HomeTheme.text; font.pixelSize: 13 }
        RowLayout {
            visible: studio.models.requested.indexOf("image")>=0; Layout.fillWidth: true; enabled: !studio.models.busy
            HomeChoiceCard { Layout.fillWidth: true; title: "Q4 · 轻量"; detail: "整套约 9.28 GB"; selected: studio.imageQuant==="Q4_K"; onClicked: studio.imageQuant="Q4_K" }
            HomeChoiceCard { Layout.fillWidth: true; title: "Q8 · 较高精度"; detail: "整套约 11.66 GB"; selected: studio.imageQuant==="Q8_0"; onClicked: studio.imageQuant="Q8_0" }
        }
        Label { text: "下载源"; color: HomeTheme.secondary }
        ComboBox {
            objectName: "modelDownloadSource"; Layout.fillWidth: true; enabled: !studio.models.busy
            model: ["Hugging Face · huggingface.co", "国内镜像 · hf-mirror.com"]
            currentIndex: studio.models.endpoint==="https://hf-mirror.com" ? 1 : 0
            onActivated: studio.models.endpoint=currentIndex===1 ? "https://hf-mirror.com" : "https://huggingface.co"
        }
        Text { Layout.fillWidth: true; text: "保存到："+studio.modelRoot; wrapMode: Text.WrapAnywhere; color: HomeTheme.secondary; font.pixelSize: 11 }
        Text { Layout.fillWidth: true; text: "各模型保留上游许可，模型来源与版本可在模型管理中查看。"; wrapMode: Text.Wrap; color: HomeTheme.secondary; font.pixelSize: 11 }
        ProgressBar { Layout.fillWidth: true; visible: studio.models.busy; value: studio.models.progress }
        Text { Layout.fillWidth: true; visible: text.length>0; text: studio.models.error || studio.models.status; wrapMode: Text.Wrap; color: studio.models.error ? HomeTheme.danger : HomeTheme.secondary; font.pixelSize: 12 }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            HomeButton { text: studio.models.busy ? "取消下载" : "关闭"; onClicked: studio.models.busy ? studio.models.cancel() : dialog.close() }
            HomeButton { objectName: "confirmModelDownload"; visible: !studio.models.busy; text: "下载 / 修复"; primary: true; onClicked: studio.models.startDownload() }
        }
    }
}
