import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtMultimedia

Rectangle {
    id: root
    color: StudioTheme.panel
    property string mode: "speech"
    property var sound: studio.sound
    property var settings: sound.settings
    property var selectedVoice: sound.voices.find(v => v.id === settings.voiceId) || ({})
    property string cloneError: ""
    onVisibleChanged: if (!visible) { voice.stop(); voiceReference.stop(); effect.stop() }
    Connections { target: studio; function onChanged() { if (studio.busy) { voice.stop(); voiceReference.stop(); effect.stop() } } }
    FileDialog { id: audioDialog; title: HomeTheme.t("为当前页选择配音"); nameFilters: [HomeTheme.t("配音音频 (*.wav *.mp3 *.m4a *.flac *.aac)")]; onAccepted: studio.pageAudio = selectedFile.toString() }
    FileDialog { id: imageDialog; title: HomeTheme.t("为当前页导入画面"); nameFilters: [HomeTheme.t("图片 (*.png *.jpg *.jpeg *.webp)")]; onAccepted: { studio.importEditImage(selectedFile); studio.narrating = true } }
    FileDialog { id: cloneFile; title: HomeTheme.t("选择 3–30 秒的参考录音"); nameFilters: [HomeTheme.t("WAV 音频 (*.wav)")]; onAccepted: cloneAudio.text = selectedFile.toString() }
    MediaPlayer { id: voice; source: (root.settings.voiceSource === "tts" ? root.settings.speechResult : studio.pageAudio) ? "file://" + (root.settings.voiceSource === "tts" ? root.settings.speechResult : studio.pageAudio) : ""; audioOutput: AudioOutput {} }
    MediaPlayer { id: voiceReference; source: root.selectedVoice.reference ? "file://" + root.selectedVoice.reference : ""; audioOutput: AudioOutput {} }
    MediaPlayer { id: effect; source: root.settings.sfxResult ? "file://" + root.settings.sfxResult : ""; audioOutput: AudioOutput {} }
    HomeDialog {
        id: cloneDialog; title: HomeTheme.t("创建参考音色"); anchors.centerIn: Overlay.overlay; modal: true; width: 520; height: 430
        standardButtons: Dialog.Cancel
        onOpened: root.cloneError = ""
        ColumnLayout {
            anchors.fill: parent; spacing: 12
            Text { Layout.fillWidth: true; text: HomeTheme.t("导入你的清晰单人录音，并填写录音里实际说出的文字。"); wrapMode: Text.Wrap; color: StudioTheme.secondary }
            StudioField { id: cloneName; Layout.fillWidth: true; placeholderText: HomeTheme.t("音色名称"); Accessible.name: HomeTheme.t("自定义音色名称") }
            RowLayout {
                Layout.fillWidth: true
                StudioField { id: cloneAudio; Layout.fillWidth: true; readOnly: true; placeholderText: HomeTheme.t("3–30 秒 WAV 录音") }
                StudioButton { text: HomeTheme.t("选择录音"); onClicked: cloneFile.open() }
            }
            InputArea { id: cloneTranscript; label: HomeTheme.t("参考录音文字"); Layout.fillWidth: true; Layout.fillHeight: true; value: ""; onEdited: text => value = text; placeholder: HomeTheme.t("录音中的完整原文，用于音色克隆…") }
            Text { Layout.fillWidth: true; visible: root.cloneError !== ""; text: root.cloneError; wrapMode: Text.Wrap; color: StudioTheme.danger }
            StudioButton { Layout.alignment: Qt.AlignRight; text: HomeTheme.t("保存音色"); primary: true; onClicked: { root.cloneError = sound.createVoice(cloneName.text, cloneAudio.text, cloneTranscript.value); if (root.cloneError === "") cloneDialog.close() } }
        }
    }
    ScrollView {
        anchors.fill: parent; anchors.margins: 12; anchors.topMargin: 10; contentWidth: availableWidth; clip: true
        ColumnLayout {
            width: parent.width; spacing: 10; enabled: !studio.busy
            ColumnLayout {
                visible: root.mode === "speech" || root.mode === "voice"; Layout.fillWidth: true; spacing: 10
            RowLayout {
                Layout.fillWidth: true
                Text { text: HomeTheme.t("配音"); color: StudioTheme.text; font.pixelSize: 12; font.weight: Font.DemiBold }
                Item { Layout.fillWidth: true }
                StudioButton { visible: root.settings.voiceSource === "tts" && root.settings.speechResult !== ""; implicitHeight: 24; text: voice.playbackState === MediaPlayer.PlayingState ? HomeTheme.t("暂停") : HomeTheme.t("试听"); iconName: "play"; quiet: true; onClicked: voice.playbackState === MediaPlayer.PlayingState ? voice.pause() : voice.play() }
            }
            RowLayout {
                visible: root.mode !== "voice"; Layout.fillWidth: true; spacing: 4
                Repeater {
                    model: [{key:"none",name:HomeTheme.t("无")},{key:"import",name:HomeTheme.t("导入音频")},{key:"tts",name:HomeTheme.t("合成语音")}]
                    delegate: StudioButton { required property var modelData; Layout.fillWidth: true; text: modelData.name; active: root.settings.voiceSource === modelData.key; onClicked: { voice.stop(); sound.set("voiceSource", modelData.key) } }
                }
            }
            RowLayout {
                visible: root.mode !== "voice" && root.settings.voiceSource === "import"; Layout.fillWidth: true; spacing: 6
                StudioButton { Layout.fillWidth: true; text: studio.pageAudio ? studio.pageAudio.split("/").pop() : HomeTheme.t("选择配音文件…"); iconName: "audio"; hint: HomeTheme.t("选择或更换配音 · WAV / MP3 / M4A / FLAC / AAC"); onClicked: audioDialog.open() }
                StudioButton { iconName: "play"; hint: voice.playbackState === MediaPlayer.PlayingState ? HomeTheme.t("暂停试听") : HomeTheme.t("试听配音"); enabled: studio.pageAudio !== ""; onClicked: voice.playbackState === MediaPlayer.PlayingState ? voice.pause() : voice.play() }
            }
            ColumnLayout {
                visible: root.mode === "voice" || root.settings.voiceSource === "tts"; Layout.fillWidth: true; spacing: 8
                RowLayout {
                    Layout.fillWidth: true; spacing: 4
                    StudioCombo {
                        Layout.fillWidth: true; model: sound.voices; textRole: "name"
                        currentIndex: sound.voices.findIndex(v => v.id === root.settings.voiceId)
                        onActivated: sound.set("voiceId", sound.voices[currentIndex].id)
                        Accessible.name: HomeTheme.t("配音音色")
                    }
                    StudioButton { iconName: "play"; hint: HomeTheme.t("试听所选音色"); quiet: true; enabled: !!root.selectedVoice.reference; onClicked: voiceReference.playbackState === MediaPlayer.PlayingState ? voiceReference.stop() : voiceReference.play() }
                    StudioButton { text: HomeTheme.t("克隆"); hint: HomeTheme.t("用参考录音创建自定义音色"); quiet: true; onClicked: cloneDialog.open() }
                }
                VoiceStyleControls { Layout.fillWidth: true; options: root.settings; onOptionEdited: (key,value)=>sound.set(key,value) }
                InputArea { objectName: "speechText"; label: HomeTheme.t("配音文案"); Layout.fillWidth: true; Layout.preferredHeight: 104; value: root.settings.speechText; placeholder: HomeTheme.t("输入配音文案…"); onEdited: text => sound.set("speechText", text) }
            }
            StudioCheckBox {
                objectName: "automaticMouthAdaptation"; visible: root.mode === "speech"
                text: HomeTheme.t("自动适配人像口型"); checked: root.settings.mouthReference !== "source"
                onClicked: sound.set("mouthReference", checked ? "auto" : "source")
            }
            Text {
                visible: root.mode === "speech"; Layout.fillWidth: true; wrapMode: Text.Wrap
                text: HomeTheme.t("自动校准人物的静音嘴形，不适合时保留原始效果。关闭可对照；修改后重新生成。")
                color: StudioTheme.muted; font.pixelSize: 11
            }
            StudioButton { Layout.fillWidth: true; visible: studio.timeline.tracks.some(t => t.key === "speech" && t.kind === "video"); text: HomeTheme.t("重置画面位置"); onClicked: studio.timeline.resetTransform("speech") }
            Text { visible: root.settings.voiceSource !== "none" || root.mode === "voice"; Layout.fillWidth: true; text: root.mode === "voice" ? HomeTheme.t("仅合成语音，生成后可试听或导出音频。") : HomeTheme.t("为共享的合成画面配音；有人像生成说话视频。"); wrapMode: Text.Wrap; color: StudioTheme.muted; font.pixelSize: 11 }
            }
            ColumnLayout {
                visible: root.mode === "sfx"; Layout.fillWidth: true; spacing: 10
            RowLayout {
                Layout.fillWidth: true
                StudioCheckBox { text: HomeTheme.t("生成音效"); checked: root.settings.sfxEnabled; onClicked: sound.set("sfxEnabled", checked) }
                Item { Layout.fillWidth: true }
                StudioButton { visible: root.settings.sfxEnabled && root.settings.sfxResult !== ""; implicitHeight: 24; text: effect.playbackState === MediaPlayer.PlayingState ? HomeTheme.t("暂停") : HomeTheme.t("试听"); iconName: "play"; quiet: true; onClicked: effect.playbackState === MediaPlayer.PlayingState ? effect.pause() : effect.play() }
            }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 8
                InputArea { objectName: "sfxPrompt"; label: HomeTheme.t("音效描述"); Layout.fillWidth: true; Layout.preferredHeight: 84; value: root.settings.sfxPrompt; placeholder: HomeTheme.t("描述音效，例如轻柔雨声、远处鸟鸣…"); onEdited: text => sound.set("sfxPrompt", text) }
                RowLayout {
                    Layout.fillWidth: true; spacing: 6
                    StudioSpinBox { from: 1; to: 30; value: root.settings.sfxDuration; onValueModified: sound.set("sfxDuration", value); Accessible.name: HomeTheme.t("音效时长（秒）") }
                    Text { text: HomeTheme.t("秒"); color: StudioTheme.muted; font.pixelSize: 11 }
                    Item { Layout.preferredWidth: 6 }
                    StudioSlider { Layout.fillWidth: true; from: 0; to: 1; value: root.settings.sfxGain; onMoved: sound.set("sfxGain", value); Accessible.name: HomeTheme.t("音效混入音量") }
                    Text { text: Math.round(root.settings.sfxGain * 100) + "%"; color: StudioTheme.muted; font.pixelSize: 11 }
                }
            }
            }
        }
    }
    signal modelSettings()
}
