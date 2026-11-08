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
    readonly property var availableResources: studio.models.resources
    signal setupFinished()
    readonly property bool ready: wasDownloading && !studio.models.busy && !studio.models.error && !studio.models.paused && studio.models.progress>=1
    title: HomeTheme.t(ready ? "准备完成" : studio.models.error ? "下载未完成" : studio.models.paused ? "下载已暂停" : "准备创作资源")
    dialogIcon: ready ? "check" : "download"
    tone: studio.models.error ? HomeTheme.danger : HomeTheme.accent
    width: Math.min(440,Overlay.overlay ? Overlay.overlay.width-40 : 440)
    closePolicy: studio.models.busy ? Popup.NoAutoClose : Popup.CloseOnEscape
    defaultAction: studio.models.busy ? null : ready ? downloadClose : downloadConfirm
    Connections {
        target: studio.models
        function onDownloadRequested() { dialog.open() }
        function onChanged() {
            if(studio.models.busy)dialog.wasDownloading=true
            if(studio.models.paused)dialog.sourceExpanded=true
            else if(studio.models.busy)dialog.sourceExpanded=false
        }
    }
    onOpened: {
        detailsExpanded=false
        termsConsent.checked=false;termsExpanded=false;sourceExpanded=studio.models.paused;wasDownloading=studio.models.busy||studio.models.paused
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
                : studio.models.error ? HomeTheme.t(studio.models.error.indexOf("磁盘")>=0 ? "空间不足，请清理后重试"
                    : studio.models.error.indexOf("应用内模型配置校验失败")>=0 ? "资源校验失败，请更新应用后重试" : "下载中断，请重试")
                : studio.models.paused ? HomeTheme.t(studio.models.busy ? "正在暂停…" : "下载较慢，已暂停。请选择下载源继续。")
                : studio.models.busy ? HomeTheme.t(studio.models.status.indexOf("校验")===0 ? "正在校验…" : studio.models.status.indexOf("取消")>=0 ? "正在取消…" : "正在下载…")
                : HomeTheme.t("选择需要的创作资源，可稍后在设置中下载。")
            wrapMode: Text.Wrap; color: HomeTheme.secondary; font.pixelSize: 13
        }
        ColumnLayout {
            visible: !studio.models.busy && !dialog.ready && !studio.models.paused
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
                        objectName: "setupResourceSize-"+resource.modelData.id
                        text: resource.modelData.ready ? HomeTheme.t("可用") : HomeTheme.t("完整资源")+" "+resource.modelData.totalSize
                        color: HomeTheme.secondary; font.pixelSize: 12
                    }
                }
            }
            Label {
                objectName: "setupDownloadSize"
                Layout.fillWidth: true; Layout.topMargin: 6
                text: HomeTheme.t("待补齐")+" "+studio.models.requestInfo.size
                color: HomeTheme.secondary; font.pixelSize: 12
            }
        }
        ColumnLayout {
            visible: !!studio.models.error && !studio.models.busy
            Layout.fillWidth: true; spacing: 4
            HomeButton {
                objectName: "toggleDownloadErrorDetails"
                text: HomeTheme.t(dialog.detailsExpanded ? "收起详情" : "查看详情")
                quiet: true; compact: true
                onClicked: dialog.detailsExpanded=!dialog.detailsExpanded
            }
            ScrollView {
                visible: dialog.detailsExpanded
                Layout.fillWidth: true; Layout.preferredHeight: Math.min(100,downloadError.implicitHeight)
                contentWidth: availableWidth; clip: true
                TextArea {
                    id: downloadError; objectName: "downloadErrorDetail"
                    text: studio.models.error; textFormat: TextEdit.PlainText
                    readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                    color: HomeTheme.secondary; font.pixelSize: 12; padding: 10
                    background: Rectangle { radius: 8; color: HomeTheme.preview }
                }
            }
        }
        RowLayout {
            visible: studio.models.busy || studio.models.paused
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
        Label {
            objectName: "modelDownloadSpeed"
            visible: studio.models.busy
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignRight
            color: HomeTheme.secondary; font.pixelSize: 12
            text: {
                const rate=studio.models.downloadBytesPerSecond
                const speed=rate>=1000000 ? (rate/1000000).toFixed(1)+" MB/s" : Math.round(rate/1000)+" KB/s"
                return (HomeTheme.english ? "Download speed: " : "下载速度：")+speed
            }
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
        ButtonGroup { id: downloadSources }
        ColumnLayout {
            objectName: "downloadSourceChoices"
            visible: studio.models.sourceValues.length>1 && dialog.sourceExpanded && !studio.models.busy
            Layout.fillWidth: true; spacing: 4
            Repeater {
                model: studio.models.sourceValues
                RadioButton {
                    id: sourceChoice
                    required property string modelData
                    required property int index
                    objectName: "downloadSource-"+index
                    Layout.fillWidth: true
                    text: HomeTheme.t(studio.models.sourceNames[index])
                    checked: studio.models.endpoint===modelData
                    ButtonGroup.group: downloadSources
                    onClicked: studio.models.endpoint=modelData
                    font.pixelSize: 13
                    padding: 4; leftPadding: 24
                    implicitHeight: 30
                    indicator: Rectangle {
                        x: 0; y: (sourceChoice.height-height)/2
                        width: 14; height: 14; radius: 7
                        color: HomeTheme.field
                        border.color: sourceChoice.checked || sourceChoice.activeFocus ? HomeTheme.accent : HomeTheme.outline
                        Rectangle { anchors.centerIn: parent; width: 8; height: 8; radius: 4; color: HomeTheme.accent; visible: sourceChoice.checked }
                    }
                    contentItem: Text { text: sourceChoice.text; font: sourceChoice.font; color: HomeTheme.text; verticalAlignment: Text.AlignVCenter; wrapMode: Text.Wrap }
                    background: Rectangle { radius: 4; color: sourceChoice.hovered ? HomeTheme.hover : "transparent"; border.width: sourceChoice.visualFocus ? 1 : 0; border.color: HomeTheme.accent }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            HomeButton { objectName: "changeDownloadSource"; visible: studio.models.sourceValues.length>1 && !!studio.models.error && !studio.models.busy && !studio.models.paused; text: HomeTheme.t("更换下载源"); quiet: true; compact: true; onClicked: dialog.sourceExpanded=!dialog.sourceExpanded }
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
