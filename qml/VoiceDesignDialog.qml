import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia

HomeDialog {
    id: root; objectName: "voiceDesignDialog"; title: HomeTheme.t("语音设计")
    width: Math.min(540,Overlay.overlay ? Overlay.overlay.width-40 : 540)
    height: Math.min(fields.implicitHeight+topPadding+bottomPadding+header.implicitHeight,
                     Overlay.overlay ? Overlay.overlay.height-40 : 650)
    property string notice: ""
    property bool generatedHere: false
    property string generatedDescription: ""
    property string generatedText: ""
    readonly property bool hasPreview: generatedHere && !!studio.voiceDesignAudio && description.value === generatedDescription && script.value === generatedText
    signal voiceSaved()
    function generate() {
        player.stop();notice="";generatedHere=true;generatedDescription=description.value;generatedText=script.value
        studio.generateVoiceDesign(description.value,script.value)
    }
    function save() {
        notice=studio.saveDesignedVoice(voiceName.text)
        if(!notice){root.voiceSaved();root.close()}
    }
    onOpened: { generatedHere=false;notice="" }
    onClosed: player.stop()
    MediaPlayer { id: player; audioOutput: AudioOutput {} }
    ScrollView {
        anchors.fill: parent; contentWidth: availableWidth; clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            id: fields
            width: parent.width; spacing: 12
            HomeField { id: voiceName; objectName: "voiceDesignName"; Layout.fillWidth: true; maximumLength: 120; placeholderText: HomeTheme.t("音色名称"); Accessible.name: placeholderText; enabled: !studio.busy }
            Text { text: HomeTheme.t("声音描述"); color: HomeTheme.secondary; font.pixelSize: 12 }
            InputArea {
                id: description; objectName: "voiceDesignDescription"; Layout.fillWidth: true; Layout.preferredHeight: 100
                label: HomeTheme.t("声音描述"); placeholder: HomeTheme.t("例如：温暖沉稳的成年女声，吐字清晰，语速舒缓，适合讲述故事。")
                value: ""; enabled: !studio.busy; onEdited: text=>value=text
            }
            Text { text: HomeTheme.t("试听文案"); color: HomeTheme.secondary; font.pixelSize: 12 }
            InputArea {
                id: script; objectName: "voiceDesignText"; Layout.fillWidth: true; Layout.preferredHeight: 90
                label: HomeTheme.t("试听文案"); placeholder: HomeTheme.t("写一段希望这个声音读出的文字")
                value: "你好，很高兴与你见面。让我们用声音记录生活，把每一个故事讲给你听。"; enabled: !studio.busy; onEdited: text=>value=text
            }
            Text { Layout.fillWidth: true; text: HomeTheme.t("保存音色需要三至三十秒的试听；保存后可选择情绪或方言继续配音。"); wrapMode: Text.Wrap; color: HomeTheme.muted; font.pixelSize: 11 }
            ConversationAudio { visible: root.hasPreview; Layout.fillWidth: true; source: root.hasPreview ? studio.voiceDesignAudio : ""; active: root.opened && !studio.busy; compact: true }
            Text { visible: !!root.notice || !!studio.error; Layout.fillWidth: true; text: root.notice || studio.error; color: HomeTheme.danger; wrapMode: Text.Wrap; font.pixelSize: 12 }
            Text { visible: studio.busy && root.generatedHere; Layout.fillWidth: true; text: HomeTheme.t(studio.status); color: HomeTheme.secondary; font.pixelSize: 12 }
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                HomeButton { text: studio.busy && root.generatedHere ? HomeTheme.t("取消生成") : HomeTheme.t("取消"); quiet: true; onClicked: {if(studio.busy && root.generatedHere)studio.cancel();else root.close()} }
                Item { Layout.fillWidth: true }
                HomeButton { objectName: "voiceDesignGenerate"; text: HomeTheme.t("生成试听"); enabled: !studio.busy && description.value.trim()!=="" && script.value.trim()!==""; onClicked: root.generate() }
                HomeButton { objectName: "voiceDesignSave"; text: HomeTheme.t("保存音色"); primary: true; enabled: !studio.busy && root.hasPreview && voiceName.text.trim()!==""; onClicked: root.save() }
            }
        }
    }
}
