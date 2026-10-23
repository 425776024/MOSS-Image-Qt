import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    ModelDownloadDialog { onSetupFinished: home.showQuickStart() }
    id: home
    objectName: "homeWindow"
    width: Math.min(1152,Math.max(1080,Screen.desktopAvailableWidth-40))
    height: Math.min(688,Screen.desktopAvailableHeight-48,width*.62)
    minimumWidth: 1080; minimumHeight: 600
    visible: false; topPadding: 0
    title: workspace.appName; color: HomeTheme.surface
    property bool nativeTitleBar: Qt.platform.os !== "osx"
    property bool quickStartActive: false
    font.family: ".AppleSystemUIFont"; font.pixelSize: 14
    property string destination: "chat"
    property int page: 0
    property bool navCollapsed: true
    property string creationMode: "image"
    property var cachedConversations: ({})
    property int cachedViewCount: 0
    property var activeView: null
    property real inspectorWidth: width*.28
    readonly property real worksWidth: Math.max(160,Math.min(260,width*.15))
    readonly property real worksRatio: worksPane.width/width
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
    function openWork(id) { home.contentItem.forceActiveFocus();destination="chat";studio.openConversation(id);showConversation() }
    function openAvatar(url) { studio.newConversation();studio.conversation.task="avatar";studio.conversation.importMedia(url,"target");showEditor("") }
    function createAvatarVideo(source,audio,tts,text,voiceId) {
        if(studio.busy)return
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
            Text { Layout.fillWidth: true; text: button.shortText; color: button.active ? HomeTheme.text : HomeTheme.secondary; font.pixelSize: 13; font.weight: button.active ? Font.Medium : Font.Normal; elide: Text.ElideRight }
        }
    }
    component SidebarSection: Label {
        Layout.fillWidth: true; Layout.leftMargin: 10; Layout.topMargin: 12; Layout.bottomMargin: 4
        color: HomeTheme.faint; font.pixelSize: 11; font.weight: Font.Medium
    }
    HomeDialog {
        id: renameDialog; objectName: "homeProjectRenameDialog"; property int targetIndex: -1; title: HomeTheme.t("重命名作品"); modal: true; width: 380; anchors.centerIn: Overlay.overlay
        ColumnLayout { width: parent.width; spacing: 14
            HomeField { id: projectName; Layout.fillWidth: true; maximumLength: 120; Accessible.name: HomeTheme.t("作品名称") }
            RowLayout { Layout.alignment: Qt.AlignRight
                HomeButton { text: HomeTheme.t("取消"); quiet: true; onClicked: renameDialog.close() }
                HomeButton { text: HomeTheme.t("保存"); primary: true; enabled: !!projectName.text.trim(); onClicked: {studio.renamePage(renameDialog.targetIndex,projectName.text);renameDialog.close()} }
            }
        }
    }
    ColumnLayout {
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
                    SidebarRow { text: HomeTheme.t("作品"); glyph: "folder"; active: home.destination === "chat"; onClicked: home.showEditor("") }
                    SidebarRow { objectName: "homeAvatars"; text: HomeTheme.t("数字人"); glyph: "person"; active: ["avatars","createAvatar"].includes(home.destination); onClicked: home.destination="avatars" }
                    SidebarRow { objectName: "homeVoices"; text: HomeTheme.t("声音"); glyph: "audio"; active: ["voices","createVoice"].includes(home.destination); onClicked: home.destination="voices" }
                    SidebarRow { objectName: "homeTemplates"; text: HomeTheme.t("构图模板"); shortText: HomeTheme.t("构图"); glyph: "grid"; active: home.destination === "templates"; onClicked: home.destination="templates" }
                    Item { Layout.fillHeight: true }
                    SidebarRow { objectName: "homeRestore"; visible: studio.canRestorePage; text: HomeTheme.t("恢复上次移除"); shortText: HomeTheme.t("恢复"); glyph: "undo"; enabled: !studio.busy; onClicked: studio.restoreDeletedPage() }
                    SidebarRow { objectName: "homeModelsButton"; text: HomeTheme.t("模型资源"); shortText: HomeTheme.t("资源"); glyph: "model"; active: home.destination === "settings" && settings.section===1; onClicked: {settings.section=1;home.destination="settings"} }
                    SidebarRow { text: HomeTheme.t("设置"); glyph: "gear"; active: home.destination === "settings" && ![1,5,6].includes(settings.section); onClicked: {settings.section=0;home.destination="settings"} }
                    SidebarRow { objectName: "homeAbout"; text: HomeTheme.t("关于"); glyph: "info"; active: home.destination === "about" || (home.destination === "settings" && settings.section===5); onClicked: {settings.section=5;home.destination="settings"} }
                    SidebarRow { objectName: "homeContact"; text: HomeTheme.t("联系我们"); shortText: HomeTheme.t("联系"); glyph: "contact"; active: home.destination === "contact" || (home.destination === "settings" && settings.section===6); onClicked: home.destination="contact" }
                }
            }
            SplitView {
                id: workspaceSplit; Layout.fillWidth: true; Layout.fillHeight: true; orientation: Qt.Horizontal
                handle: Rectangle {
                    id: worksHandle; objectName: "worksResizeHandle"; implicitWidth: 1
                    color: SplitHandle.hovered || SplitHandle.pressed ? HomeTheme.accent : HomeTheme.line
                    containmentMask: Item { x: -4; width: 9; height: worksHandle.height }
                    HoverHandler { cursorShape: Qt.SplitHCursor }
                }
            Rectangle {
                id: worksPane; objectName: "worksPane"
                visible: home.destination === "chat"
                SplitView.minimumWidth: 160
                SplitView.maximumWidth: Math.max(160,Math.min(300,home.width*.22,home.width*.5-322))
                SplitView.preferredWidth: home.worksWidth; color: HomeTheme.sidebar
                WindowDragArea { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 12 }
                ColumnLayout {
                    anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; anchors.topMargin: 12; anchors.bottomMargin: 10; spacing: 10
                    RowLayout { Layout.fillWidth: true; spacing: 10
                        Text { Layout.fillWidth: true; text: HomeTheme.t("我的作品"); color: HomeTheme.text; font.pixelSize: 14; font.weight: Font.Medium }
                        IconButton { objectName: "homeCreateProject"; glyph: "plus"; hint: HomeTheme.t("新建作品"); implicitHeight: 30; implicitWidth: 30; onClicked: {home.contentItem.forceActiveFocus();studio.newConversation();home.showEditor("")} }
                    }
                    HomeField { id: search; objectName: "homeWorkSearch"; Layout.fillWidth: true; implicitHeight: 30; search: true; placeholderText: HomeTheme.t("搜索作品"); Accessible.name: HomeTheme.t("搜索作品"); font.pixelSize: 13 }
                    ListView {
                        id: works; objectName: "conversationWorkList"; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 4
                        model: studio.creationPages.map((p,i)=>({row:p,index:i})).filter(p=>HomeTheme.projectName(p.row).toLowerCase().includes(search.text.toLowerCase()))
                        reuseItems: true; cacheBuffer: 800; ScrollBar.vertical: ScrollBar { }
                        delegate: AbstractButton {
                            id: work; required property var modelData
                            width: works.width; height: 54; hoverEnabled: true
                            readonly property bool selected: !!studio.conversation && studio.conversation.id === modelData.row.id
                            readonly property string modifiedLabel: {
                                const date=new Date(modelData.row.modified || modelData.row.created || "")
                                return isNaN(date.getTime()) ? "" : Qt.formatDateTime(date,date.toDateString()===new Date().toDateString() ? "hh:mm" : "MM-dd")
                            }
                            readonly property var messages: modelData.row.conversation ? modelData.row.conversation.messages || [] : []
                            readonly property string lastText: {
                                if(!messages.length)return HomeTheme.t("开始新创作")
                                const last=messages[messages.length-1]
                                if(last.status === "running")return HomeTheme.t("正在创作…")
                                if(last.status === "failed" || last.status === "interrupted")return HomeTheme.t("创作未完成 · 可继续")
                                if(last.role === "user")return last.text || HomeTheme.t("新的要求")
                                return ({image:HomeTheme.t("图像已生成"),edit:HomeTheme.t("图像已编辑"),avatar:HomeTheme.t("说话视频已生成"),voice:HomeTheme.t("语音已合成"),sfx:HomeTheme.t("音效已生成"),pack:HomeTheme.t("视频已打包"),"import":HomeTheme.t("素材已导入")})[last.task] || HomeTheme.t("创作已保存")
                            }
                            ToolTip.visible: hovered && workTitle.truncated
                            ToolTip.text: HomeTheme.projectName(modelData.row)
                            ToolTip.delay: 700
                            Accessible.name: HomeTheme.t("打开作品：")+HomeTheme.projectName(modelData.row); Accessible.selected: selected
                            background: Rectangle { radius: 8; color: work.selected ? Qt.alpha(HomeTheme.text, HomeTheme.dark ? .08 : .055) : work.hovered ? HomeTheme.hover : "transparent"; border.color: work.visualFocus ? HomeTheme.accent : "transparent" }
                            contentItem: RowLayout {
                                spacing: 8
                                Rectangle { Layout.leftMargin: 7; Layout.preferredWidth: 30; Layout.preferredHeight: 30; color: HomeTheme.preview; radius: 5; clip: true
                                    Image { anchors.fill: parent; source: work.modelData.row.visual || work.modelData.row.image || ""; sourceSize: Qt.size(96,96); fillMode: Image.PreserveAspectCrop; asynchronous: true; retainWhileLoading: true; cache: true }
                                    StudioIcon { visible: !work.modelData.row.visual && !work.modelData.row.image; anchors.centerIn: parent; name: "document"; ink: HomeTheme.faint; width: 20; height: 20 }
                                }
                                ColumnLayout { Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.rightMargin: 7; spacing: 3
                                    Text { id: workTitle; Layout.fillWidth: true; Layout.rightMargin: workActions.opacity>0 ? 25 : 0; text: HomeTheme.projectName(work.modelData.row); elide: Text.ElideRight; font.pixelSize: 13; font.weight: work.selected ? Font.Medium : Font.Normal; color: HomeTheme.text }
                                    RowLayout { Layout.fillWidth: true; spacing: 4
                                        Text { Layout.fillWidth: true; Layout.minimumWidth: 0; text: work.lastText.replace(/\n/g," "); elide: Text.ElideRight; font.pixelSize: 11; color: HomeTheme.secondary }
                                        Text { visible: !!work.modifiedLabel; text: work.modifiedLabel; font.pixelSize: 9; color: HomeTheme.muted }
                                    }
                                }
                            }
                                HomeButton { id: workActions; anchors.right: parent.right; anchors.rightMargin: 3; y: 5; opacity: work.hovered || hovered || activeFocus || projectMenu.opened ? 1 : 0; iconName: "more"; hint: HomeTheme.t("作品操作"); width: 25; height: 22; quiet: true; onClicked: projectMenu.popup(width-projectMenu.width,height+4)
                                    HomeMenu { id: projectMenu
                                        AppMenuItem { text: HomeTheme.t("重命名"); enabled: !studio.busy; onTriggered: {renameDialog.targetIndex=work.modelData.index;projectName.text=HomeTheme.projectName(work.modelData.row);renameDialog.open()} }
                                        AppMenuItem { text: HomeTheme.t("复制作品"); enabled: !studio.busy; onTriggered: {studio.duplicatePage(work.modelData.index);home.showEditor("")} }
                                        AppMenuItem { text: HomeTheme.t("移除作品"); destructive: true; enabled: !studio.busy; onTriggered: studio.deletePage(work.modelData.index) }
                                    }
                                }
                            onClicked: home.openWork(modelData.row.id)
                        }
                    }

                }
            }
            Item {
                id: rightPane; objectName: "conversationPane"; SplitView.fillWidth: true
                SplitView.minimumWidth: home.destination === "chat" ? Math.min(home.width*.5+(home.activeView && home.activeView.inspectorOpen ? 261 : 0),workspaceSplit.width-161) : 0
                WindowDragArea { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: home.destination === "chat" ? 0 : 22 }
                Item { id: threadHost; anchors.fill: parent; visible: home.destination === "chat" }
                StackLayout {
                    anchors.fill: parent; visible: home.destination !== "chat"
                    currentIndex: ({avatars:0,voices:1,createAvatar:2,contact:3,settings:4,about:4,templates:5,createVoice:6})[home.destination] || 0
                    HomeAvatars { onUseAvatar: url=>home.openAvatar(url); onCreateAvatar: home.destination="createAvatar" }
                    HomeVoices { id: voicesLibrary; onCreateVoice: mode=>{voiceCreator.mode=mode;home.destination="createVoice"}; onUseVoice: id=>{studio.conversation.voiceId=id;studio.conversation.voiceOptions={emotion:studio.sound.settings.emotion,dialect:studio.sound.settings.dialect};home.showEditor("voice")} }
                    HomeAvatarCreator { onOpenLibrary: home.destination="avatars"; onCreateVideo: (source,audio,tts,text,voiceId)=>home.createAvatarVideo(source,audio,tts,text,voiceId) }
                    HomeContact { }
                    HomeSettings { id: settings; objectName: "homeSettings"; onOpenEditor: home.showEditor("") }
                    HomeTemplates { onTemplateUsed: home.showEditor("") }
                    HomeVoiceCreator { id: voiceCreator; onOpenLibrary: home.destination="voices"; onVoiceSaved: {voicesLibrary.builtin=false;home.destination="voices"} }
                }
            }
            }
        }
    }
}
