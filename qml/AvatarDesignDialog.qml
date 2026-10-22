import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

HomeDialog {
    id: root; objectName: "avatarDesignDialog"; title: HomeTheme.t("文字生成形象")
    width: Math.min(760,Overlay.overlay ? Overlay.overlay.width-40 : 760)
    height: Math.min(560,Overlay.overlay ? Overlay.overlay.height-40 : 560)
    property string notice: ""
    property bool generatedHere: false
    property string generatedPrompt: ""
    readonly property bool hasPreview: generatedHere && !!studio.avatarDesignImage && description.value === generatedPrompt
    signal avatarSaved()
    onOpened: {generatedHere=false;notice=""}
    function generate() {notice="";generatedHere=true;generatedPrompt=description.value;studio.generateAvatarImage(description.value)}
    function save() {
        notice=workspace.importAvatar(studio.avatarDesignImage,avatarName.text,studio.avatarDesignRecipe)
        if(!notice){root.avatarSaved();root.close()}
    }
    ColumnLayout {
        anchors.fill: parent; spacing: 12
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 18
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 10
                HomeField { id: avatarName; objectName: "avatarDesignName"; Layout.fillWidth: true; placeholderText: HomeTheme.t("形象名称"); maximumLength: 120; Accessible.name: placeholderText; enabled: !studio.busy }
                Text { text: HomeTheme.t("人物描述"); color: HomeTheme.secondary; font.pixelSize: 12 }
                InputArea {
                    id: description; objectName: "avatarDesignDescription"; Layout.fillWidth: true; Layout.fillHeight: true
                    label: HomeTheme.t("人物描述"); placeholder: HomeTheme.t("描述人物的外貌、服装、表情、背景和画面风格…")
                    value: ""; enabled: !studio.busy; onEdited: text=>value=text
                }
                Text { Layout.fillWidth: true; text: HomeTheme.t("单人、清晰五官、正面或微侧脸的头肩人像，更适合制作数字人。"); color: HomeTheme.muted; wrapMode: Text.Wrap; font.pixelSize: 11 }
            }
            Rectangle {
                Layout.preferredWidth: 250; Layout.fillHeight: true; radius: 8; color: HomeTheme.preview; border.color: HomeTheme.line; clip: true
                Image { objectName: "avatarDesignPreview"; anchors.fill: parent; anchors.margins: 6; source: root.hasPreview ? studio.avatarDesignImage : ""; fillMode: Image.PreserveAspectFit; asynchronous: true; sourceSize: Qt.size(600,800) }
                HomeEmpty { visible: !root.hasPreview; anchors.centerIn: parent; width: parent.width-28; iconName: "person"; title: HomeTheme.t("生成后在这里预览") }
            }
        }
        Text { visible: !!root.notice || !!studio.error; Layout.fillWidth: true; text: root.notice || studio.error; wrapMode: Text.Wrap; color: HomeTheme.danger; font.pixelSize: 12 }
        Text { visible: studio.busy && root.generatedHere; Layout.fillWidth: true; text: HomeTheme.t(studio.status); color: HomeTheme.secondary; font.pixelSize: 12 }
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            HomeButton { text: studio.busy && root.generatedHere ? HomeTheme.t("取消生成") : HomeTheme.t("取消"); quiet: true; onClicked: {if(studio.busy && root.generatedHere)studio.cancel();else root.close()} }
            Item { Layout.fillWidth: true }
            HomeButton { objectName: "avatarDesignGenerate"; text: HomeTheme.t("生成形象"); enabled: !studio.busy && description.value.trim()!==""; onClicked: root.generate() }
            HomeButton { objectName: "avatarDesignSave"; text: HomeTheme.t("保存到我的形象"); primary: true; enabled: !studio.busy && root.hasPreview && avatarName.text.trim()!==""; onClicked: root.save() }
        }
    }
}
