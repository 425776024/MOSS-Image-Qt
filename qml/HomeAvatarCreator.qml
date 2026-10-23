import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Rectangle {
    id: root; objectName: "homeAvatarCreator"; color: HomeTheme.sidebar
    property string sourceUrl: workspace.builtinAvatars.length ? workspace.builtinAvatars[0].url : ""
    property string audioUrl: ""
    property string voiceId: studio.sound.settings.voiceId
    property bool tts: true
    property string notice: ""
    property bool saved: false
    signal createVideo(string source, string audio, bool tts, string text, string voiceId)
    signal openLibrary()
    FileDialog { id: portraitDialog; title: HomeTheme.t("选择人像"); nameFilters: ["Images (*.png *.jpg *.jpeg *.webp)"]; onAccepted: {root.sourceUrl=selectedFile.toString();avatarName.text=decodeURIComponent(root.sourceUrl.split("/").pop()).replace(/\.[^.]+$/,"");root.saved=false;root.notice=""} }
    FileDialog { id: audioDialog; title: HomeTheme.t("选择配音"); nameFilters: ["Audio (*.wav *.mp3 *.m4a *.flac *.aac)"]; onAccepted: root.audioUrl=selectedFile.toString() }
    HomeDialog {
        id: saveDialog; objectName: "homeAvatarSaveDialog"; title: HomeTheme.t("保存到我的形象")
        dialogIcon: "person"
        ColumnLayout { width: parent.width; spacing: 12
            HomeImagePreview { Layout.fillWidth: true; source: root.sourceUrl }
            HomeField { id: avatarName; Layout.fillWidth: true; placeholderText: HomeTheme.t("形象名称"); Accessible.name: placeholderText; maximumLength: 120 }
            Text { visible: !!root.notice; Layout.fillWidth: true; text: root.notice; color: HomeTheme.danger; font.pixelSize: 12; wrapMode: Text.Wrap }
            RowLayout { Layout.alignment: Qt.AlignRight
                HomeButton { text: HomeTheme.t("取消"); quiet: true; onClicked: saveDialog.close() }
                HomeButton { text: HomeTheme.t("保存"); primary: true; enabled: !!avatarName.text.trim(); onClicked: {root.notice=workspace.importAvatar(root.sourceUrl,avatarName.text);if(!root.notice){root.saved=true;saveDialog.close()}} }
            }
        }
    }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 22; spacing: 20
        HomeHeader { Layout.fillWidth: true; title: HomeTheme.t("设计数字人")
            HomeButton { text: HomeTheme.t("返回形象库"); iconName: "person"; quiet: true; onClicked: root.openLibrary() }
        }
        ScrollView {
            id: scroll; Layout.fillWidth: true; Layout.fillHeight: true; contentWidth: availableWidth; clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            RowLayout {
                id: creatorPanels; objectName: "avatarCreatorPanels"
                width: scroll.availableWidth; spacing: 16
                height: Math.max(portraitPanel.implicitHeight,audioPanel.implicitHeight)
                HomeSection {
                    id: portraitPanel; objectName: "avatarPortraitPanel"
                    stretchContents: true; Layout.fillWidth: true; Layout.fillHeight: true; Layout.preferredWidth: 360; title: HomeTheme.t("人像"); iconName: "person"
                    AbstractButton {
                        Layout.fillWidth: true; Layout.fillHeight: true; implicitHeight: Math.min(360,root.height*.49); Accessible.name: HomeTheme.t(root.sourceUrl ? HomeTheme.t("更换图片") : HomeTheme.t("选择人像")); hoverEnabled: true
                        onClicked: portraitDialog.open()
                        background: Rectangle { radius: 8; color: HomeTheme.preview; border.color: parent.visualFocus ? HomeTheme.accent : HomeTheme.outline }
                        contentItem: Item {
                            Image { anchors.fill: parent; anchors.margins: 8; source: root.sourceUrl; fillMode: Image.PreserveAspectFit; asynchronous: true }
                            HomeEmpty { visible: !root.sourceUrl; anchors.centerIn: parent; width: Math.min(240,parent.width-24); title: HomeTheme.t("选择人像"); iconName: "plus" }
                        }
                    }
                    RowLayout { visible: !!root.sourceUrl; Layout.fillWidth: true; spacing: 8
                        HomeButton { text: HomeTheme.t("更换图片"); iconName: "image"; quiet: true; enabled: !studio.busy; onClicked: portraitDialog.open() }
                        Item { Layout.fillWidth: true }
                        HomeButton { text: root.saved ? HomeTheme.t("已保存") : HomeTheme.t("保存形象"); iconName: root.saved ? "check" : "plus"; quiet: true; enabled: !root.saved && !studio.busy; onClicked: saveDialog.open() }
                    }
                }
                HomeSection {
                    id: audioPanel; objectName: "avatarAudioPanel"
                    stretchContents: true; Layout.fillWidth: true; Layout.fillHeight: true; Layout.preferredWidth: 460; title: HomeTheme.t("配音"); iconName: "audio"
                    RowLayout { Layout.fillWidth: true; spacing: 8
                        HomeChoiceCard { Layout.fillWidth: true; title: HomeTheme.t("合成语音"); iconName: "text"; selected: root.tts; onClicked: root.tts=true }
                        HomeChoiceCard { Layout.fillWidth: true; title: HomeTheme.t("导入音频"); iconName: "audio"; selected: !root.tts; onClicked: root.tts=false }
                    }
                    GridView {
                        id: voices; visible: root.tts; Layout.fillWidth: true; Layout.preferredHeight: 144; clip: true
                        model: studio.sound.voices; cellWidth: width/2; cellHeight: 36; ScrollBar.vertical: ScrollBar { }
                        delegate: HomeChoiceCard { required property var modelData; width: voices.cellWidth-6; height: 32; padding: 6; title: modelData.name; iconName: "audio"; selected: root.voiceId===modelData.id; onClicked: root.voiceId=modelData.id }
                    }
                    InputArea { id: script; visible: root.tts; Layout.fillWidth: true; Layout.fillHeight: true; implicitHeight: 160; radius: 8; label: HomeTheme.t("配音文案"); placeholder: HomeTheme.t("输入数字人要说的话…"); onEdited: editedText=>value=editedText }
                    HomeChoiceCard { visible: !root.tts; Layout.fillWidth: true; title: root.audioUrl ? decodeURIComponent(root.audioUrl.split("/").pop()) : HomeTheme.t("选择配音"); detail: root.audioUrl ? HomeTheme.t("点击更换音频") : "WAV、MP3、M4A"; iconName: "audio"; selected: !!root.audioUrl; onClicked: audioDialog.open() }
                    ConversationAudio { visible: !root.tts && !!root.audioUrl; Layout.fillWidth: true; source: root.audioUrl; active: root.visible && !root.tts }
                    Text { visible: !!root.notice && !saveDialog.opened; Layout.fillWidth: true; text: root.notice; color: HomeTheme.danger; font.pixelSize: 12; wrapMode: Text.Wrap }
                    Item { visible: !root.tts; Layout.fillHeight: true }
                    HomeButton {
                        objectName: "homeCreateAvatarVideo"; Layout.alignment: Qt.AlignRight; text: HomeTheme.t("制作数字人视频"); primary: true; iconName: "play"
                        enabled: !studio.busy && !!root.sourceUrl && (root.tts ? !!script.value.trim() && !!root.voiceId : !!root.audioUrl)
                        onClicked: root.createVideo(root.sourceUrl,root.audioUrl,root.tts,script.value,root.voiceId)
                    }
                }
            }
        }
    }
}
