import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    ModelDownloadDialog { onSetupFinished: home.showQuickStart() }
    PurchaseDialog {}
    id: home
    objectName: "homeWindow"
    width: Math.min(1152,Math.max(1080,Screen.desktopAvailableWidth-40))
    height: Math.min(688,Screen.desktopAvailableHeight-48,width*.62)
    minimumWidth: 1080; minimumHeight: 600
    visible: false; topPadding: 0
    title: workspace.appName; color: HomeTheme.surface
    property bool nativeTitleBar: Qt.platform.os !== "osx"
    property bool quickStartActive: false
    font: VideoCutTheme.bodyFont
    property string destination: "chat"
    property int page: 0
    property bool navCollapsed: true
    property string creationMode: "image"
    property var cachedConversations: ({})
    property int cachedViewCount: 0
    property var activeView: null
    property real inspectorWidth: width*.28
    onClosing: studio.flushWorkspace()
    Shortcut { sequences: [StandardKey.Quit]; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    palette.window: HomeTheme.surface; palette.base: HomeTheme.field; palette.text: HomeTheme.text
    palette.windowText: HomeTheme.text; palette.button: HomeTheme.field; palette.buttonText: HomeTheme.text
    palette.highlight: HomeTheme.selection; palette.highlightedText: HomeTheme.accent; palette.mid: HomeTheme.outline
    palette.light: HomeTheme.hover; palette.dark: HomeTheme.sidebar; palette.placeholderText: HomeTheme.faint
    function titleFor(id) { const row=studio.creationPages.find(p=>p.id===id);return row ? HomeTheme.projectName(row) : HomeTheme.t("未命名作品") }
    function currentIndex() { return studio.creationPages.findIndex(p=>p.id===studio.conversation.id) }
    function showConversation() {
        const thread=studio.conversation;if(!thread)return
        const id=thread.id
        if(!cachedConversations[id]) {
            const view=threadComponent.createObject(threadHost,{thread:thread})
            view.projectTitle=Qt.binding(()=>home.titleFor(id))
            view.settingsRequested.connect(()=>{settings.section=1;home.destination="settings"})
            view.voicesRequested.connect(()=>home.destination="voices")
            view.renameWorkRequested.connect(id=>{renameDialog.targetId=id;projectName.text=home.titleFor(id);renameDialog.open()})
            view.cloneVoiceRequested.connect(source=>{voiceCreator.mode=1;voiceCreator.audioUrl=source;home.destination="createVoice"})
            cachedConversations[id]=view;cachedViewCount++
        }
        if(activeView === cachedConversations[id]){activeView.visible=destination === "chat";return}
        if(activeView)activeView.visible=false
        activeView=cachedConversations[id];activeView.visible=destination === "chat"
    }
    function showEditor(context) {
        home.contentItem.forceActiveFocus();destination="chat"
        if(studio.conversation && context)studio.conversation.task=({speech:"avatar",voice:"voice",sfx:"sfx",image:"image"})[context] || studio.conversation.task
        showConversation()
    }
    function showQuickStart() {
        if(!workspace.quickStartPending)return
        destination="chat";quickStartActive=true
    }
    function showHome(target) { if(target === "models"){settings.section=1;destination="settings"}else destination=target === "works" ? "chat" : target || "chat" }
    function openInspector(context) { showEditor(context) }
    function mcpControl(a) {
        function fail(message) { return {ok:false,error:{code:"UI_ACTION_FAILED",message:message}} }
        if(a.action === "settings" || a.action === "models") { settings.section=a.action==="models" ? 1 : 0;destination="settings";return {ok:true,data:{destination:destination}} }
        if(a.action === "voices") { destination="voices";return {ok:true,data:{destination:destination}} }
        if(!a.workId || !studio.creationPages.some(p=>p.id===a.workId))return fail("A valid workId is required")
        if(["edit","pack","parameters"].includes(a.action) && studio.busy)return fail("Creation is busy")
        openWork(a.workId)
        if(!activeView)return fail("Conversation view is unavailable")
        return activeView.mcpControl(a)
    }
    function openWork(id) { home.contentItem.forceActiveFocus();destination="chat";studio.openConversation(id);showConversation() }
    function openAvatar(url) { if(!studio.purchases.requireAccess())return;studio.newConversation();studio.conversation.task="avatar";studio.conversation.importMedia(url,"target");showEditor("") }
    function createAvatarVideo(source,audio,tts,text,voiceId) {
        if(studio.busy)return
        if(!studio.purchases.requireAccess())return
        openAvatar(source)
        if(tts){studio.conversation.voiceId=voiceId;studio.conversation.draft=text}
        else studio.conversation.importMedia(audio,"audio")
        studio.sendConversation()
    }
    Connections { target: studio; function onConversationChanged(){home.showConversation()} }
    onDestinationChanged: if(activeView)activeView.visible=destination === "chat"
    Component.onCompleted: showConversation()
    Component { id: threadComponent; ConversationView { anchors.fill: parent; visible: false; quickStartActive: home.quickStartActive; minimumChatWidth: Math.min(home.width*.5, Math.max(400, threadHost.width-261)); preferredInspectorWidth: home.inspectorWidth; maximumInspectorWidth: home.width*.34; onInspectorWidthAdjusted: width => home.inspectorWidth=width } }
    component SidebarRow: AbstractButton {
        id: button
        property string glyph: "document"
        property string shortText: text
        property bool active: false
        property bool expandable: false
        property bool expanded: false
        focusPolicy: Qt.StrongFocus
        activeFocusOnTab: true
        Layout.fillWidth: true
        implicitWidth: 140; implicitHeight: 34; hoverEnabled: true
        leftPadding: 10; rightPadding: 10; topPadding: 7; bottomPadding: 7
        opacity: enabled ? 1 : .4
        Accessible.name: text; Accessible.selected: active
        ToolTip.visible: hovered && button.text !== button.shortText; ToolTip.text: text; ToolTip.delay: 700
        background: Rectangle { radius: 6; color: button.active ? Qt.alpha(HomeTheme.text,HomeTheme.dark ? .10 : .07) : button.hovered ? HomeTheme.hover : "transparent"; border.color: button.visualFocus ? HomeTheme.accent : "transparent"; border.width: VideoCutTheme.hairline }
        contentItem: RowLayout {
            spacing: 10
            StudioIcon { Layout.preferredWidth: 16; Layout.preferredHeight: 16; name: button.glyph; ink: button.active ? HomeTheme.text : HomeTheme.secondary }
            Text { Layout.fillWidth: true; text: button.shortText; color: button.active ? HomeTheme.text : HomeTheme.secondary; font.pixelSize: 13; font.weight: Font.Medium; elide: Text.ElideRight }
            StudioIcon { visible: button.expandable; name: "chevron"; rotation: button.expanded ? 0 : -90; ink: HomeTheme.secondary; Layout.preferredWidth: 12; Layout.preferredHeight: 12 }
        }
    }
    component SidebarSection: Label {
        Layout.fillWidth: true; Layout.leftMargin: 10; Layout.topMargin: 12; Layout.bottomMargin: 4
        color: HomeTheme.faint; font.pixelSize: 11; font.weight: Font.Medium
    }
    HomeDialog {
        id: renameDialog; objectName: "homeProjectRenameDialog"; property string targetId: ""; title: HomeTheme.t("重命名作品"); modal: true; width: 380; anchors.centerIn: Overlay.overlay
        defaultAction: renameProjectSave
        onOpened: {projectName.forceActiveFocus();projectName.selectAll()}
        ColumnLayout { width: parent.width; spacing: 14
            HomeField { id: projectName; Layout.fillWidth: true; maximumLength: 120; Accessible.name: HomeTheme.t("作品名称") }
            RowLayout { Layout.alignment: Qt.AlignRight
                HomeButton { text: HomeTheme.t("取消"); quiet: true; onClicked: renameDialog.close() }
                HomeButton { id: renameProjectSave; text: HomeTheme.t("保存"); primary: true; enabled: !studio.busy && !!projectName.text.trim() && studio.creationPages.some(p=>p.id===renameDialog.targetId); onClicked: {const index=studio.creationPages.findIndex(p=>p.id===renameDialog.targetId);if(index>=0)studio.renamePage(index,projectName.text);renameDialog.close()} }
            }
        }
    }
    ColumnLayout {
        enabled: studio.purchases.accessAllowed
        anchors.fill: parent; spacing: 0
        Rectangle {
            objectName: "homeTitleStrip"
            visible: !home.nativeTitleBar
            Layout.fillWidth: true; Layout.preferredHeight: 32; color: HomeTheme.sidebar
            WindowDragArea { anchors.fill: parent }
            Text { anchors.centerIn: parent; text: workspace.appName; color: HomeTheme.secondary; font.pixelSize: 12; font.weight: Font.Medium }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: VideoCutTheme.hairline; color: HomeTheme.line }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
            Rectangle {
                objectName: "navigationRail"; Layout.preferredWidth: HomeTheme.english ? 168 : 148; Layout.fillHeight: true; color: HomeTheme.sidebar
                Rectangle { anchors.right: parent.right; width: VideoCutTheme.hairline; height: parent.height; color: HomeTheme.line }
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 8; spacing: 2
                    SidebarSection { text: HomeTheme.t("工作区"); Layout.topMargin: 4 }
                    SidebarRow { objectName: "homeWorksToggle"; text: HomeTheme.t("作品"); glyph: "folder"; active: home.destination === "chat"; onClicked: home.showEditor("") }
                    SidebarRow { objectName: "homeAvatars"; text: HomeTheme.t("数字人"); glyph: "person"; active: ["avatars","createAvatar"].includes(home.destination); onClicked: {if(studio.purchases.requireAccess())home.destination="avatars"} }
                    SidebarRow { objectName: "homeVoices"; text: HomeTheme.t("音色"); glyph: "audio"; active: ["voices","createVoice"].includes(home.destination); onClicked: home.destination="voices" }
                    SidebarRow { objectName: "homeTemplates"; text: HomeTheme.t("构图模板"); shortText: HomeTheme.t("构图"); glyph: "grid"; active: home.destination === "templates"; onClicked: home.destination="templates" }
                    Item { Layout.fillHeight: true }
                    SidebarRow { objectName: "homeMcpButton"; text: "MCP"; glyph: "server"; active: home.destination === "mcp"; onClicked: home.destination="mcp" }
                    SidebarRow { objectName: "homeModelsButton"; text: HomeTheme.t("模型资源"); shortText: HomeTheme.t("资源"); glyph: "model"; active: home.destination === "settings" && settings.section===1; onClicked: {settings.section=1;home.destination="settings"} }
                    SidebarRow { text: HomeTheme.t("设置"); glyph: "gear"; active: home.destination === "settings" && ![1,5,6].includes(settings.section); onClicked: {settings.section=0;home.destination="settings"} }
                    SidebarRow { objectName: "homeUpdatesButton"; text: HomeTheme.t("更新")+(appUpdate.state==="available" ? " •" : ""); glyph: "refresh"; active: home.destination === "updates"; onClicked: home.destination="updates" }
                    SidebarRow { objectName: "homeAbout"; text: HomeTheme.t("关于"); glyph: "info"; active: home.destination === "about" || (home.destination === "settings" && settings.section===5); onClicked: {settings.section=5;home.destination="settings"} }
                    SidebarRow { objectName: "homeContact"; text: HomeTheme.t("联系我们"); shortText: HomeTheme.t("联系"); glyph: "contact"; active: home.destination === "contact" || (home.destination === "settings" && settings.section===6); onClicked: home.destination="contact" }
                }
            }
            Item {
                id: workspaceSplit; Layout.fillWidth: true; Layout.fillHeight: true
            Item {
                id: rightPane; objectName: "conversationPane"; anchors.fill: parent
                WindowDragArea { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: home.destination === "chat" ? 0 : 22 }
                Item { id: threadHost; anchors.fill: parent; visible: home.destination === "chat" }
                StackLayout {
                    anchors.fill: parent; visible: home.destination !== "chat"
                    currentIndex: ({avatars:0,voices:1,createAvatar:2,contact:3,settings:4,about:4,templates:5,createVoice:6,mcp:7,updates:8})[home.destination] || 0
                    HomeAvatars { onUseAvatar: url=>home.openAvatar(url); onCreateAvatar: home.destination="createAvatar" }
                    HomeVoices { id: voicesLibrary; onCreateVoice: mode=>{voiceCreator.mode=mode;home.destination="createVoice"}; onUseVoice: id=>{studio.conversation.voiceId=id;studio.conversation.voiceOptions={emotion:studio.sound.settings.emotion,dialect:studio.sound.settings.dialect};home.showEditor("voice")} }
                    HomeAvatarCreator { onOpenLibrary: home.destination="avatars"; onCreateVideo: (source,audio,tts,text,voiceId)=>home.createAvatarVideo(source,audio,tts,text,voiceId) }
                    HomeContact { }
                    HomeSettings { id: settings; objectName: "homeSettings"; onOpenEditor: home.showEditor("") }
                    HomeTemplates { onTemplateUsed: home.showEditor("") }
                    HomeVoiceCreator { id: voiceCreator; onOpenLibrary: home.destination="voices"; onVoiceSaved: {voicesLibrary.builtin=false;home.destination="voices"} }
                    HomeMcp { }
                    HomeUpdates { }
                }
            }
            }
        }
    }
}
