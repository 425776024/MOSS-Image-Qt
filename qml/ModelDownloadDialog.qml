import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

HomeDialog {
    id: dialog
    objectName: "modelDownloadDialog"
    property bool termsExpanded: false
    property bool sourceExpanded: false
    property bool wasDownloading: false
    property var availableResources: []
    signal setupFinished()
    readonly property bool ready: wasDownloading && !studio.models.busy && !studio.models.error && studio.models.progress>=1
    title: HomeTheme.t(ready ? "准备完成" : studio.models.error ? "下载未完成" : "准备创作资源")
    dialogIcon: ready ? "check" : "download"
    tone: studio.models.error ? HomeTheme.danger : HomeTheme.accent
    width: Math.min(440,Overlay.overlay ? Overlay.overlay.width-40 : 440)
    closePolicy: studio.models.busy ? Popup.NoAutoClose : Popup.CloseOnEscape
    defaultAction: studio.models.busy ? null : ready ? downloadClose : downloadConfirm
    Connections {
        target: studio.models
        function onDownloadRequested() { dialog.open() }
        function onChanged() { if(studio.models.busy)dialog.wasDownloading=true }
    }
    onOpened: {
        availableResources=studio.models.resources
        termsConsent.checked=false;termsExpanded=false;sourceExpanded=false;wasDownloading=studio.models.busy
        downloadConfirm.forceActiveFocus(Qt.TabFocusReason)
    }
    onClosed: {
        const initial=studio.models.initialSetup
        studio.models.dismissRequest()
        if(initial)setupFinished()
    }
    contentItem: ColumnLayout {
        spacing: 14
        Label {
            Layout.fillWidth: true
            text: dialog.ready ? HomeTheme.t(studio.models.initialSetup ? "可以开始创作了" : "已就绪，请重新点击生成")
                : studio.models.error ? HomeTheme.t(studio.models.error.indexOf("磁盘")>=0 ? "空间不足，请清理后重试" : "下载中断，请重试")
                : studio.models.busy ? HomeTheme.t(studio.models.status.indexOf("校验")===0 ? "正在校验…" : studio.models.status.indexOf("取消")>=0 ? "正在取消…" : "正在下载…")
                : HomeTheme.t("选择需要的创作资源，可稍后在设置中下载。")
            wrapMode: Text.Wrap; color: HomeTheme.secondary; font.pixelSize: 13
        }
        ColumnLayout {
            visible: !studio.models.busy && !dialog.ready
            Layout.fillWidth: true; spacing: 2
            Repeater {
                model: dialog.availableResources
                RowLayout {
                    id: resource
                    required property var modelData
                    Layout.fillWidth: true; spacing: 12
                    AppCheckBox {
                        objectName: "setupResource-"+resource.modelData.id
                        Layout.fillWidth: true
                        text: HomeTheme.t(resource.modelData.title)
                        checked: resource.modelData.ready || studio.models.requested.indexOf(resource.modelData.id)>=0
                        enabled: !resource.modelData.ready
                        onClicked: {
                            let selected=studio.models.requested.slice()
                            if(checked)selected.push(resource.modelData.id)
                            else selected=selected.filter(id=>id!==resource.modelData.id)
                            studio.models.setRequested(selected)
                        }
                    }
                    Label {
                        text: resource.modelData.ready ? HomeTheme.t("可用") : resource.modelData.size
                        color: HomeTheme.secondary; font.pixelSize: 12
                    }
                }
            }
            Label {
                objectName: "setupDownloadSize"
                Layout.fillWidth: true; Layout.topMargin: 6
                text: HomeTheme.t("所需下载")+" "+studio.models.requestInfo.size
                color: HomeTheme.secondary; font.pixelSize: 12
            }
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
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            HomeButton { visible: !!studio.models.error && !studio.models.busy; text: HomeTheme.t("更换下载源"); quiet: true; compact: true; onClicked: dialog.sourceExpanded=!dialog.sourceExpanded }
            Item { Layout.fillWidth: true }
            HomeButton { id: downloadClose; objectName: "closeModelDownload"; text: HomeTheme.t(studio.models.busy ? "取消下载" : dialog.ready ? "完成" : "稍后"); primary: dialog.ready; implicitHeight: 28; onClicked: studio.models.busy ? studio.models.cancel() : dialog.close() }
            HomeButton {
                id: downloadConfirm
                objectName: "confirmModelDownload"; visible: !studio.models.busy && !dialog.ready
                implicitHeight: 28
                text: HomeTheme.t(studio.models.error ? "重试" : "下载"); primary: true
                enabled: studio.models.requested.length>0 && (!studio.models.sfxTermsRequired || termsConsent.checked)
                onClicked: {if(studio.models.sfxTermsRequired)studio.models.acceptSfxTerms();studio.models.startDownload()}
            }
        }
    }
}
