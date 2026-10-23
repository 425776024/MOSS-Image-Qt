import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "VoiceDesignPresets.js" as VoicePresets

Rectangle {
    id: root; objectName: "homeVoiceCreator"; color: HomeTheme.sidebar
    property int mode: 0
    property string selectedPresetId: ""
    readonly property var presets: VoicePresets.entries.filter(row=>{
        const name=HomeTheme.english ? row.nameEn : row.name
        const tag=HomeTheme.english ? row.tagEn : row.tag
        return (name+" "+tag).toLowerCase().includes(presetSearch.text.trim().toLowerCase())
    })
    function applyPreset(row) {
        if(studio.busy)return
        mode=0;selectedPresetId=row.id;generatedAudio="";notice=""
        voiceName.text=HomeTheme.english ? row.nameEn : row.name
        description.value=HomeTheme.english ? row.descriptionEn : row.description
        script.value=HomeTheme.english ? row.textEn : row.text
    }
    property string audioUrl: ""
    property string notice: ""
    property bool generating: false
    property string generatedAudio: ""
    property string generatedDescription: ""
    property string generatedText: ""
    readonly property bool hasPreview: !!generatedAudio && generatedAudio === studio.voiceDesignAudio
        && description.value === generatedDescription && script.value === generatedText
    signal openLibrary()
    signal voiceSaved()
    onModeChanged: notice=""
    function generate() {
        notice="";generatedAudio="";generating=true
        generatedDescription=description.value;generatedText=script.value
        studio.generateVoiceDesign(description.value,script.value)
        if(!studio.busy)generating=false
    }
    function save() {
        if(studio.busy || (mode===0 && !hasPreview))return
        notice=mode===1 ? studio.sound.createVoice(voiceName.text,audioUrl,transcript.value) : studio.saveDesignedVoice(voiceName.text)
        if(!notice){generatedAudio="";audioUrl="";root.voiceSaved()}
    }
    Connections {
        target: studio
        function onJobFinished(success) {
            if(!root.generating)return
            root.generating=false;root.generatedAudio=success ? studio.voiceDesignAudio : ""
        }
    }
    FileDialog {
        id: audioDialog; title: HomeTheme.t("选择录音"); nameFilters: ["WAV audio (*.wav)"]
        onAccepted: {root.audioUrl=selectedFile.toString();root.notice=""}
    }
    SplitView {
        anchors.fill: parent; orientation: Qt.Horizontal
        handle: Rectangle { implicitWidth: 1; color: SplitHandle.hovered || SplitHandle.pressed ? HomeTheme.accent : HomeTheme.line
            HoverHandler { cursorShape: Qt.SplitHCursor }
        }
        Rectangle {
            objectName: "voiceTemplatePane"; visible: root.mode===0
            SplitView.minimumWidth: 200; SplitView.preferredWidth: 224; SplitView.maximumWidth: 280
            color: HomeTheme.sidebar
            WindowDragArea { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 12 }
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 10; spacing: 10
                RowLayout { Layout.fillWidth: true; Layout.preferredHeight: 32
                    Text { Layout.fillWidth: true; text: HomeTheme.t("声音模板"); color: HomeTheme.text; font.pixelSize: 14; font.weight: Font.Medium }
                    Text { text: String(root.presets.length); color: HomeTheme.muted; font.pixelSize: 11 }
                }
                HomeField { id: presetSearch; objectName: "voicePresetSearch"; Layout.fillWidth: true; search: true; placeholderText: HomeTheme.t("搜索模板"); Accessible.name: placeholderText }
                ListView {
                    id: presetList; objectName: "voicePresetList"; Layout.fillWidth: true; Layout.fillHeight: true
                    model: root.presets; clip: true; spacing: 4; reuseItems: true
                    ScrollBar.vertical: ScrollBar { }
                    delegate: AbstractButton {
                        id: preset; required property var modelData; objectName: "voicePreset-"+modelData.id
                        width: presetList.width; height: 38; padding: 8; hoverEnabled: true; enabled: !studio.busy
                        readonly property bool chosen: root.selectedPresetId===modelData.id
                        readonly property string presetName: HomeTheme.english ? modelData.nameEn : modelData.name
                        Accessible.name: presetName; Accessible.selected: chosen
                        onClicked: root.applyPreset(modelData)
                        background: Rectangle { radius: 6; color: preset.chosen ? Qt.alpha(HomeTheme.text,HomeTheme.dark ? .10 : .07) : preset.hovered ? HomeTheme.hover : "transparent"; border.color: preset.visualFocus ? HomeTheme.accent : "transparent" }
                        contentItem: Text { id: presetLabel; text: preset.presetName; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter; color: HomeTheme.text; font.pixelSize: 13; font.weight: preset.chosen ? Font.Medium : Font.Normal }
                        ToolTip.visible: hovered && presetLabel.truncated; ToolTip.delay: 700; ToolTip.text: presetName
                    }
                }
                HomeEmpty { visible: root.presets.length===0; Layout.fillWidth: true; title: HomeTheme.t("没有匹配的模板"); iconName: "audio" }
            }
        }
        Item {
            objectName: "voiceDesignEditor"; SplitView.fillWidth: true; SplitView.minimumWidth: 540
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 22; spacing: 16
                HomeHeader { Layout.fillWidth: true; title: HomeTheme.t(root.mode===1 ? "导入音色" : "设计声音")
                    HomeButton { text: HomeTheme.t("返回音色库"); iconName: "audio"; quiet: true; onClicked: root.openLibrary() }
                }
                HomeTabs { labels: [HomeTheme.t("文字设计"),HomeTheme.t("导入录音")]; currentIndex: root.mode; enabled: !studio.busy; onSelected: index=>root.mode=index }
                ScrollView {
                    id: scroll; Layout.fillWidth: true; Layout.fillHeight: true; contentWidth: availableWidth; clip: true
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    HomeSection {
                        width: scroll.availableWidth; height: Math.max(implicitHeight,scroll.availableHeight); stretchContents: true
                        HomeField { id: voiceName; objectName: "createdVoiceName"; Layout.fillWidth: true; maximumLength: 120; placeholderText: HomeTheme.t("音色名称"); Accessible.name: placeholderText; enabled: !studio.busy }
                        Text { visible: root.mode===0; text: HomeTheme.t("声音描述"); color: HomeTheme.secondary; font.pixelSize: 12 }
                        InputArea {
                            id: description; objectName: "createdVoiceDescription"; visible: root.mode===0; Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 100; Layout.preferredHeight: 100
                            label: HomeTheme.t("声音描述"); placeholder: HomeTheme.t("例如：温暖沉稳的成年女声，吐字清晰，语速舒缓，适合讲述故事。")
                            enabled: !studio.busy; onEdited: text=>value=text
                        }
                        Text { visible: root.mode===0; text: HomeTheme.t("试听文案"); color: HomeTheme.secondary; font.pixelSize: 12 }
                        InputArea {
                            id: script; objectName: "createdVoiceText"; visible: root.mode===0; Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 90; Layout.preferredHeight: 90
                            label: HomeTheme.t("试听文案"); placeholder: HomeTheme.t("写一段希望这个声音读出的文字")
                            value: "你好，很高兴与你见面。让我们用声音记录生活，把每一个故事讲给你听。"
                            enabled: !studio.busy; onEdited: text=>value=text
                        }
                        HomeChoiceCard {
                            visible: root.mode===1; Layout.fillWidth: true; iconName: "audio"; enabled: !studio.busy
                            title: root.audioUrl ? decodeURIComponent(root.audioUrl.split("/").pop()) : HomeTheme.t("选择录音")
                            detail: HomeTheme.t("3–30 秒 WAV 录音"); selected: !!root.audioUrl; onClicked: audioDialog.open()
                        }
                        Loader {
                            Layout.fillWidth: true; Layout.preferredHeight: item ? item.implicitHeight : 0
                            visible: active; active: root.visible && (root.mode===1 ? !!root.audioUrl : root.hasPreview)
                            sourceComponent: ConversationAudio { source: root.mode===1 ? root.audioUrl : root.generatedAudio; active: root.visible && !studio.busy; compact: true }
                        }
                        Text { visible: root.mode===1; text: HomeTheme.t("参考录音中的完整原文"); color: HomeTheme.secondary; font.pixelSize: 12 }
                        InputArea {
                            id: transcript; objectName: "createdVoiceTranscript"; visible: root.mode===1; Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 120; Layout.preferredHeight: 120
                            label: HomeTheme.t("参考录音中的完整原文"); placeholder: HomeTheme.t("填写录音中实际说出的完整文案")
                            enabled: !studio.busy; onEdited: text=>value=text
                        }
                        Text {
                            visible: root.mode===0; Layout.fillWidth: true; text: HomeTheme.t("保存音色需要三至三十秒的试听；保存后可选择情绪或方言继续配音。")
                            wrapMode: Text.Wrap; color: HomeTheme.muted; font.pixelSize: 11
                        }
                        Text { visible: !!root.notice || (root.mode===0 && !!studio.error); Layout.fillWidth: true; text: root.notice || studio.error; wrapMode: Text.Wrap; color: HomeTheme.danger; font.pixelSize: 12 }
                        Text { visible: root.generating; Layout.fillWidth: true; text: HomeTheme.t(studio.status); color: HomeTheme.secondary; font.pixelSize: 12 }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            HomeButton { visible: root.generating; text: HomeTheme.t("取消生成"); quiet: true; onClicked: studio.cancel() }
                            Item { Layout.fillWidth: true }
                            HomeButton { objectName: "createdVoiceGenerate"; visible: root.mode===0; text: HomeTheme.t("生成试听"); enabled: !studio.busy && !!description.value.trim() && !!script.value.trim(); onClicked: root.generate() }
                            HomeButton {
                                objectName: "createdVoiceSave"; text: HomeTheme.t("保存音色"); primary: true
                                enabled: !studio.busy && !!voiceName.text.trim() && (root.mode===1 ? !!root.audioUrl && !!transcript.value.trim() : root.hasPreview)
                                onClicked: root.save()
                            }
                        }
                    }
                }
            }
        }
    }
}
