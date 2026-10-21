import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtMultimedia
import MossStudio

Rectangle {
    id: root; objectName: "homeVoiceLibrary"
    color: HomeTheme.sidebar
    property string audioUrl: ""
    property bool builtin: false
    property string notice: ""
    property string playingId: ""
    readonly property var entries: studio.sound.voices.filter(row => !!row.builtin === builtin && row.name.toLowerCase().includes(search.text.trim().toLowerCase()))
    signal useVoice(string id)
    function stopPreview() { player.stop(); playingId = "" }
    function preview(row) { if(playingId === row.id)stopPreview(); else { player.stop(); playingId = row.id; player.source = "file://" + row.reference; player.play() } }
    function importVoice() { editDialog.voiceId = ""; voiceName.text = ""; root.audioUrl = ""; transcript.text = ""; notice = ""; editDialog.open() }
    function editVoice(row) { editDialog.voiceId = row.id; voiceName.text = row.name; root.audioUrl = "file://" + row.reference; transcript.text = row.transcript; notice = ""; editDialog.open() }
    function saveVoice() {
        notice = editDialog.voiceId ? studio.sound.updateVoice(editDialog.voiceId, voiceName.text, transcript.text) : studio.sound.createVoice(voiceName.text, root.audioUrl, transcript.text)
        if(!notice) { builtin = false; editDialog.close() }
    }
    onVisibleChanged: if(!visible)stopPreview()
    Connections { target: studio; function onChanged() { if(studio.busy)root.stopPreview() } }
    MediaPlayer { id: player; audioOutput: AudioOutput {}
         onMediaStatusChanged: if(mediaStatus === MediaPlayer.EndOfMedia)root.playingId = ""; onErrorOccurred: { root.notice = errorString; root.playingId = "" } }
    FileDialog { id: audioDialog; title: HomeTheme.t("选择录音"); nameFilters: ["WAV audio (*.wav)"]; onAccepted: root.audioUrl = selectedFile.toString() }
    HomeDialog {
        id: editDialog; objectName: "homeVoiceImportDialog"; property string voiceId: ""; title: HomeTheme.t(voiceId ? "音色名称与参考文字" : "导入音色")
        width: Math.min(500,Overlay.overlay.width-40); height: Math.min(460,Overlay.overlay.height-48)
        onClosed: root.stopPreview()
        ColumnLayout { anchors.fill: parent; spacing: 12
            HomeField { id: voiceName; objectName: "homeVoiceName"; Layout.fillWidth: true; maximumLength: 120; placeholderText: HomeTheme.t("音色名称"); Accessible.name: placeholderText }
            HomeSection {
                objectName: "homeVoiceAudio"; Layout.fillWidth: true; padding: 10
                RowLayout { Layout.fillWidth: true; spacing: 8
                    StudioIcon { name: "audio"; Layout.preferredWidth: 20; Layout.preferredHeight: 20; ink: HomeTheme.accent }
                    Text { Layout.fillWidth: true; Layout.minimumWidth: 0; text: root.audioUrl ? decodeURIComponent(root.audioUrl.split("/").pop()) : HomeTheme.t("3–30 秒 WAV 录音"); color: HomeTheme.secondary; font.pixelSize: 12; elide: Text.ElideMiddle }
                    HomeButton { visible: !editDialog.voiceId; text: root.audioUrl ? "更换" : HomeTheme.t("选择录音"); compact: true; onClicked: audioDialog.open() }
                }
                ConversationAudio { visible: !!root.audioUrl; Layout.fillWidth: true; source: root.audioUrl; active: editDialog.opened; compact: true; onInteracted: root.stopPreview() }
            }
            Text { text: HomeTheme.t("参考录音中的完整原文"); color: HomeTheme.secondary; font.pixelSize: 12 }
            InputArea { id: transcript; objectName: "homeVoiceTranscript"; Layout.fillWidth: true; Layout.fillHeight: true; label: HomeTheme.t("参考录音中的完整原文"); placeholder: "填写录音中实际说出的完整文案"; property alias text: transcript.value; onEdited: editedText=>value=editedText; radius: 8 }
            Text { visible: !!root.notice; Layout.fillWidth: true; text: root.notice; color: HomeTheme.danger; wrapMode: Text.Wrap; font.pixelSize: 12 }
            RowLayout { Layout.alignment: Qt.AlignRight; spacing: 8
                HomeButton { text: HomeTheme.t("取消"); quiet: true; onClicked: editDialog.close() }
                HomeButton { objectName: "homeSaveVoice"; text: HomeTheme.t("保存音色"); primary: true; enabled: voiceName.text.trim()!=="" && transcript.text.trim()!=="" && root.audioUrl!==""; onClicked: root.saveVoice() }
            }
        }
    }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 22; spacing: 16
        HomeHeader { Layout.fillWidth: true; title: "我的声音"; detail: String(root.entries.length)
            HomeButton { text: HomeTheme.t("导入音色"); iconName: "plus"; primary: true; enabled: !studio.busy; onClicked: root.importVoice() }
        }
        RowLayout { Layout.fillWidth: true; spacing: 12
            HomeTabs { labels: ["我的音色","内置音色"]; currentIndex: root.builtin ? 1 : 0; onSelected: index=>{root.stopPreview();root.builtin=index===1} }
            Item { Layout.fillWidth: true }
            HomeField { id: search; Layout.preferredWidth: 240; search: true; placeholderText: HomeTheme.t("搜索音色"); Accessible.name: placeholderText }
        }
        Item {
            Layout.fillWidth: true; Layout.fillHeight: true
            ColumnLayout { visible: root.entries.length===0; anchors.centerIn: parent; spacing: 16
                HomeEmpty { title: search.text ? "没有匹配的音色" : "还没有音色"; description: search.text ? "" : "导入一段清晰人声，制作自己的参考音色。"; iconName: "audio" }
                RowLayout { visible: !search.text; Layout.alignment: Qt.AlignHCenter
                    HomeButton { text: HomeTheme.t("导入音色"); iconName: "plus"; onClicked: root.importVoice() }
                    HomeButton { visible: !root.builtin; text: HomeTheme.t("内置音色"); quiet: true; onClicked: root.builtin=true }
                }
            }
            GridView {
                id: list; anchors.fill: parent; model: root.entries; clip: true
                cellWidth: width/Math.max(1,Math.floor(width/270)); cellHeight: 120
                ScrollBar.vertical: ScrollBar { }
                delegate: AbstractButton {
                    id: row; required property var modelData; readonly property bool chosen: studio.sound.settings.voiceId===modelData.id
                    width: list.cellWidth-12; height: list.cellHeight-12; padding: 12; hoverEnabled: true
                    Accessible.name: modelData.name; Accessible.selected: chosen
                    onClicked: if(!studio.busy){studio.sound.set("voiceId",modelData.id);studio.sound.set("voiceSource","tts")}
                    onDoubleClicked: if(!studio.busy)root.useVoice(modelData.id)
                    background: Rectangle { radius: 10; color: HomeTheme.field; border.color: row.chosen || row.visualFocus ? HomeTheme.accent : row.hovered ? HomeTheme.outline : HomeTheme.line }
                    contentItem: ColumnLayout { spacing: 6
                        RowLayout { Layout.fillWidth: true; spacing: 7
                            IconButton { glyph: root.playingId===row.modelData.id ? "pause" : "play"; hint: HomeTheme.t("试听")+" "+row.modelData.name; iconColor: HomeTheme.accent; onClicked: root.preview(row.modelData) }
                            Text { Layout.fillWidth: true; Layout.minimumWidth: 0; text: row.modelData.name; elide: Text.ElideRight; font.pixelSize: 13; font.weight: Font.Medium; color: HomeTheme.text }
                            StudioIcon { visible: row.chosen; name: "check"; ink: HomeTheme.accent; Layout.preferredWidth: 14; Layout.preferredHeight: 14 }
                            IconButton { visible: !root.builtin; glyph: "more"; hint: HomeTheme.t("操作"); enabled: !studio.busy; onClicked: {actions.target=row.modelData;const at=row.mapToItem(root,row.width-actions.width,row.height+4);actions.popup(at.x,at.y)} }
                        }
                        Text { Layout.fillWidth: true; text: row.modelData.transcript; elide: Text.ElideRight; font.pixelSize: 11; color: HomeTheme.muted }
                        AudioWaveform { Layout.fillWidth: true; Layout.fillHeight: true; source: "file://"+row.modelData.reference; active: root.visible && row.visible; progress: root.playingId===row.modelData.id ? player.position/Math.max(1,player.duration) : 0; waveColor: Qt.alpha(HomeTheme.secondary,.4); playedColor: HomeTheme.accent }
                    }
                    ToolTip.visible: hovered; ToolTip.delay: 750; ToolTip.text: modelData.name
                }
            }
        }
        HomeSection { visible: root.entries.some(v=>v.id===studio.sound.settings.voiceId); Layout.fillWidth: true; padding: 10
            RowLayout { Layout.fillWidth: true; spacing: 10
                StudioIcon { name: "audio"; ink: HomeTheme.secondary; Layout.preferredWidth: 18; Layout.preferredHeight: 18 }
                Text { Layout.fillWidth: true; text: (root.entries.find(v=>v.id===studio.sound.settings.voiceId)||{}).name || ""; elide: Text.ElideRight; color: HomeTheme.text; font.pixelSize: 12 }
                HomeButton { text: HomeTheme.t("用于创作"); iconName: "arrow"; primary: true; enabled: !studio.busy; onClicked: root.useVoice(studio.sound.settings.voiceId) }
            }
        }
        RowLayout { visible: !!root.notice || studio.sound.canRestoreVoice; Layout.fillWidth: true
            Text { Layout.fillWidth: true; text: root.notice; color: HomeTheme.danger; wrapMode: Text.Wrap; font.pixelSize: 12 }
            HomeButton { visible: studio.sound.canRestoreVoice; quiet: true; text: HomeTheme.t("恢复上次移除"); onClicked: root.notice=studio.sound.restoreVoice() }
        }
    }
    HomeMenu { id: actions; property var target: ({})
        AppMenuItem { text: HomeTheme.t("用于创作"); onTriggered: root.useVoice(actions.target.id) }
        AppMenuItem { text: HomeTheme.t("编辑"); onTriggered: root.editVoice(actions.target) }
        AppMenuItem { text: HomeTheme.t("移除"); destructive: true; onTriggered: { root.stopPreview(); root.notice = studio.sound.removeVoice(actions.target.id) } }
    }
}
