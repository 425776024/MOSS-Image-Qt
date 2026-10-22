import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

HomeDialog {
    id: dialog
    objectName: "modelDownloadDialog"
    property bool termsExpanded: false
    property bool sourceExpanded: false
    property bool detailsExpanded: false
    property bool wasDownloading: false
    readonly property bool ready: wasDownloading && !studio.models.busy && !studio.models.error && studio.models.progress>=1
    title: HomeTheme.t(ready ? "准备完成" : studio.models.error ? "下载未完成" : "准备创作资源")
    dialogIcon: ready ? "check" : "download"
    tone: studio.models.error ? HomeTheme.danger : HomeTheme.accent
    width: Math.min(400,Overlay.overlay ? Overlay.overlay.width-40 : 400)
    closePolicy: studio.models.busy ? Popup.NoAutoClose : Popup.CloseOnEscape
    defaultAction: studio.models.busy ? null : ready ? downloadClose : downloadConfirm
    Connections {
        target: studio.models
        function onDownloadRequested() { dialog.open() }
        function onChanged() { if(studio.models.busy)dialog.wasDownloading=true }
    }
    onOpened: {termsConsent.checked=false;termsExpanded=false;sourceExpanded=false;detailsExpanded=false;wasDownloading=studio.models.busy}
    onClosed: studio.models.dismissRequest()
    contentItem: ColumnLayout {
        spacing: 14
        Label {
            Layout.fillWidth: true
            text: dialog.ready ? HomeTheme.t("已就绪，请重新点击生成")
                : studio.models.error ? HomeTheme.t(studio.models.error.indexOf("磁盘")>=0 ? "空间不足，请清理后重试" : "下载中断，请重试")
                : studio.models.busy ? HomeTheme.t(studio.models.status.indexOf("校验")===0 ? "正在校验…" : studio.models.status.indexOf("取消")>=0 ? "正在取消…" : "正在下载…")
                : HomeTheme.t("首次使用需下载")+" "+studio.models.requestInfo.size
            wrapMode: Text.Wrap; color: HomeTheme.secondary; font.pixelSize: 13
        }
        RowLayout {
            visible: studio.models.busy
            Layout.fillWidth: true; spacing: 12
            ProgressBar {
                id: downloadProgress; objectName: "modelDownloadProgress"
                Layout.fillWidth: true; Layout.preferredHeight: 6
                value: studio.models.progress; padding: 0
                background: Rectangle { implicitHeight: 6; radius: 3; color: HomeTheme.dark ? HomeTheme.surface : "#d8e2e8" }
                contentItem: Item { Rectangle { width: downloadProgress.visualPosition*parent.width; height: parent.height; radius: 3; color: HomeTheme.accent } }
            }
            Label { objectName: "modelDownloadPercent"; text: Math.floor(downloadProgress.position*100)+"%"; color: HomeTheme.text; font.pixelSize: 13; font.weight: Font.Medium; Layout.preferredWidth: 38; horizontalAlignment: Text.AlignRight }
        }
        // Consent remains explicit; the full terms open only when requested.
        ColumnLayout {
            visible: studio.models.sfxTermsRequired && !studio.models.busy && !dialog.ready
            Layout.fillWidth: true; spacing: 6
            RowLayout {
                Layout.fillWidth: true
                AppCheckBox { id: termsConsent; objectName: "soundModelTermsConsent"; text: HomeTheme.t("同意音效使用条款") }
                Item { Layout.fillWidth: true }
                HomeButton { text: HomeTheme.t(dialog.termsExpanded ? "收起" : "查看"); compact: true; quiet: true; onClicked: dialog.termsExpanded=!dialog.termsExpanded }
            }
            ScrollView {
                visible: dialog.termsExpanded
                Layout.fillWidth: true; Layout.preferredHeight: 150; contentWidth: availableWidth; clip: true
                TextArea { text: studio.models.modelTerms(HomeTheme.english); textFormat: TextEdit.MarkdownText; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; color: HomeTheme.secondary; font.pixelSize: 12; onLinkActivated: link=>Qt.openUrlExternally(link); background: Rectangle { radius: 8; color: HomeTheme.preview } }
            }
        }
        HomeCombo {
            visible: dialog.sourceExpanded && !studio.models.busy
            Layout.fillWidth: true; Layout.preferredHeight: 32
            model: studio.models.sourceNames.map(function(name) {return HomeTheme.t(name)})
            currentIndex: studio.models.sourceValues.indexOf(studio.models.endpoint)
            onActivated: studio.models.endpoint=studio.models.sourceValues[currentIndex]
        }
        ColumnLayout {
            visible: !!studio.models.error && !studio.models.busy
            Layout.fillWidth: true; spacing: 6
            HomeButton {
                objectName: "modelDownloadDetailsToggle"
                text: HomeTheme.t(dialog.detailsExpanded ? "收起详情" : "查看详情")
                quiet: true; compact: true
                onClicked: dialog.detailsExpanded=!dialog.detailsExpanded
            }
            ScrollView {
                visible: dialog.detailsExpanded
                Layout.fillWidth: true; Layout.preferredHeight: 100
                contentWidth: availableWidth; clip: true
                TextArea {
                    objectName: "modelDownloadErrorDetails"
                    text: studio.models.error; textFormat: TextEdit.PlainText
                    readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                    color: HomeTheme.secondary; font.pixelSize: 12
                    background: Rectangle { radius: 8; color: HomeTheme.preview }
                    Accessible.name: HomeTheme.t("下载未完成")
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            HomeButton { visible: !!studio.models.error && !studio.models.busy; text: HomeTheme.t("更换下载源"); quiet: true; compact: true; onClicked: dialog.sourceExpanded=!dialog.sourceExpanded }
            Item { Layout.fillWidth: true }
            HomeButton { id: downloadClose; text: HomeTheme.t(studio.models.busy ? "取消下载" : dialog.ready ? "完成" : "稍后"); primary: dialog.ready; implicitHeight: 28; onClicked: studio.models.busy ? studio.models.cancel() : dialog.close() }
            HomeButton {
                id: downloadConfirm
                objectName: "confirmModelDownload"; visible: !studio.models.busy && !dialog.ready
                implicitHeight: 28
                text: HomeTheme.t(studio.models.error ? "重试" : "下载"); primary: true
                enabled: !studio.models.sfxTermsRequired || termsConsent.checked
                onClicked: {if(studio.models.sfxTermsRequired)studio.models.acceptSfxTerms();studio.models.startDownload()}
            }
        }
    }
}
