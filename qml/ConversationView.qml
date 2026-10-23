import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtMultimedia
import MossStudio

Item {
    id: root
    required property var thread
    property string projectTitle: HomeTheme.t("未命名作品")
    signal settingsRequested()
    signal voicesRequested()
    readonly property var taskKeys: ["image","edit","avatar","voice","sfx","pack"]
    readonly property var taskLabels: [HomeTheme.t("生成图像"),HomeTheme.t("编辑图像"),HomeTheme.t("让画面说话"),HomeTheme.t("语音合成"),HomeTheme.t("音效生成"),HomeTheme.t("打包视频")]
    property string pickerMode: "target"
    property bool imageToolsOpen: false
    property string toolsTask: ""
    property bool inspectorOpen: thread.messages.some(entry=>(entry.assetIds || []).length>0)
    property bool quickStartActive: false
    readonly property bool showingQuickStart: quickStartActive && workspace.quickStartPending
    property real minimumChatWidth: 540
    property real preferredInspectorWidth: 400
    property real maximumInspectorWidth: 490
    signal inspectorWidthAdjusted(real width)
    property string inspectedMessageId: ""
    property string inspectedAssetId: ""
    property bool packExpanded: false
    readonly property var inspectedEntry: thread.messages.find(m=>m.id===inspectedMessageId) || ({})
    readonly property var inspectedAsset: {
        const all=thread.assets
        return thread.asset(inspectedAssetId || (inspectedEntry.assetIds || [])[0] || "")
    }
    property string editTargetKey: thread.target.id || ""
    onEditTargetKeyChanged: if(imageToolsOpen && toolsTask === "edit")studio.prepareConversationEdit()
    onVisibleChanged: if(!visible)imageToolsOpen=false
    function showDetails(messageId,assetId) {
        root.forceActiveFocus()
        if(imageToolsOpen)studio.flushWorkspace()
        imageToolsOpen=false;packExpanded=false
        inspectedMessageId=messageId;inspectedAssetId=assetId || "";inspectorOpen=true
    }
    function showLatest() {
        const messages=thread.messages
        for(let i=messages.length-1;i>=0;i--)if((messages[i].assetIds || []).length){inspectedMessageId=messages[i].id;inspectedAssetId="";return}
        inspectedMessageId=messages.length ? messages[messages.length-1].id : "";inspectedAssetId=""
    }
    function openImageTools() {
        root.forceActiveFocus()
        if(imageToolsOpen){studio.flushWorkspace();imageToolsOpen=false;return}
        if(thread.task === "edit") {if(!studio.prepareConversationEdit())return}
        else {if(!studio.prepareConversationSettings())return;studio.document.prompt=thread.draft}
        toolsTask=thread.task;packExpanded=false;inspectorOpen=true;imageToolsOpen=true
    }
    function editAsset(id) {root.forceActiveFocus();thread.task="edit";thread.editScope="local";thread.chooseTarget(id);imageToolsOpen=false;openImageTools()}
    function showPackTools() {root.forceActiveFocus();if(imageToolsOpen)studio.flushWorkspace();imageToolsOpen=false;inspectorOpen=true;packExpanded=true}
    function usePackAsset(id) {thread.task="pack";if(id && !thread.selected.includes(id))thread.toggleSelected(id);showPackTools()}
    Connections {
        target: root.thread
        function onChanged() {
            if(root.imageToolsOpen && root.toolsTask !== root.thread.task)root.imageToolsOpen=false
            if(root.thread.task !== "pack")root.packExpanded=false
            if(root.imageToolsOpen && root.toolsTask === "image")studio.document.prompt=root.thread.draft
        }
        function onMessagesChanged() {if(!root.thread.messages.some(m=>m.id===root.inspectedMessageId))root.showLatest()}
    }
    readonly property real railMargin: 16
    property string exportId: ""
    property bool presentationReady: false
    Component.onCompleted: {showLatest();presentationReady=true}
    property string selectionKey: thread.selected.join(",")
    onSelectionKeyChanged: if(presentationReady && thread.task === "pack" && thread.selected.length)showPackTools()
    function send() {
        root.forceActiveFocus(); history.followTail=true; studio.sendConversation()
        if(thread.pending){imageToolsOpen=false;packExpanded=false;inspectedMessageId=thread.messages[thread.messages.length-1].id;inspectedAssetId=""}
    }
    readonly property bool targeted: thread.task === "edit" || thread.task === "avatar"
    readonly property bool audioInput: thread.task === "avatar" && !!thread.audio.id
    objectName: "conversation-"+thread.id
    function label(task) { return task === "import" ? HomeTheme.t("导入素材") : taskLabels[taskKeys.indexOf(task)] || task }
    function voiceName() { const voice=studio.sound.voices.find(v=>v.id===thread.voiceId); return voice ? voice.name : HomeTheme.t("选择音色") }
    function openPicker(mode) { pickerMode=mode;assetPicker.open() }
    function importFiles(purpose) { importDialog.purpose=purpose;importDialog.fileMode=purpose === "pack" ? FileDialog.OpenFiles : FileDialog.OpenFile;importDialog.open() }
    function exportAsset(asset) { exportId=asset.id;exportDialog.nameFilters=asset.kind === "image" ? [HomeTheme.t("图像 (*.png *.jpg *.webp)")] : asset.kind === "audio" ? [HomeTheme.t("音频 (*.wav *.mp3 *.m4a)")] : [HomeTheme.t("视频 (*.mp4)")];exportDialog.selectedFile=asset.url;exportDialog.open() }
    FileDialog {
        id: importDialog; property string purpose: "target"; title: purpose === "audio" ? HomeTheme.t("导入配音") : purpose === "pack" ? HomeTheme.t("导入视频素材") : HomeTheme.t("导入操作图片")
        nameFilters: purpose === "audio" ? [HomeTheme.t("音频 (*.wav *.mp3 *.m4a *.aac *.flac *.ogg)")] : purpose === "target" ? [HomeTheme.t("图像 (*.png *.jpg *.jpeg *.webp *.bmp)")] : [HomeTheme.t("媒体素材 (*.png *.jpg *.jpeg *.webp *.mp4 *.mov *.mkv *.wav *.mp3 *.m4a *.flac)")]
        onAccepted: { for(const file of selectedFiles)root.thread.importMedia(file,purpose);if(assetPicker.opened)assetPicker.close();if(audioPicker.opened)audioPicker.close() }
    }
    FileDialog { id: exportDialog; title: HomeTheme.t("导出素材"); fileMode: FileDialog.SaveFile; onAccepted: studio.exportConversationAsset(root.exportId,selectedFile) }
    HomeNoticeDialog { id: exportNotice; objectName: "conversationExportNotice" }
    Connections {
        target: studio
        enabled: root.visible
        function onFileExportFinished(success, detail) {
            exportNotice.message=HomeTheme.t(success ? "文件已保存" : "导出失败")
            exportNotice.severity=success ? "success" : "error"
            exportNotice.filePath=success ? detail : ""
            exportNotice.detail=success ? "" : detail
            exportNotice.open()
        }
    }
    HomeNoticeDialog { id: saveNotice; objectName: "conversationSaveNotice" }
    Connections {
        target: studio
        enabled: root.visible
        function onConversationSaveFinished(success, detail) {
            saveNotice.message=HomeTheme.t(success ? "文件已保存" : "导出失败")
            saveNotice.severity=success ? "success" : "error"
            saveNotice.filePath=success ? detail : ""
            saveNotice.detail=success ? "" : detail
            saveNotice.open()
        }
    }
    FileDialog { id: saveDialog; title: HomeTheme.t("保存作品工程"); fileMode: FileDialog.SaveFile; nameFilters: [HomeTheme.t("Moss 工程 (*.moss.json)")]; onAccepted: studio.saveConversation(selectedFile) }
    Shortcut { sequences: [StandardKey.Save]; enabled: root.visible; onActivated: saveDialog.open() }
    SplitView {
        id: paneSplit; anchors.fill: parent; orientation: Qt.Horizontal
        onResizingChanged: if(!resizing && root.visible && inspector.visible && inspector.width>0){
            root.inspectorWidthAdjusted(inspector.width)
            inspector.SplitView.preferredWidth=Qt.binding(()=>root.preferredInspectorWidth)
        }
        handle: Rectangle {
            id: splitHandle; objectName: "inspectorResizeHandle"
            implicitWidth: 1; color: SplitHandle.hovered || SplitHandle.pressed ? HomeTheme.accent : HomeTheme.line
            containmentMask: Item { x: -4; width: 9; height: splitHandle.height }
            HoverHandler { cursorShape: Qt.SplitHCursor }
        }
    ColumnLayout {
        id: chatPanel; objectName: "conversationChatPanel"
        SplitView.minimumWidth: root.minimumChatWidth; SplitView.fillWidth: true; spacing: 0
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 44; color: HomeTheme.surface
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: root.railMargin; anchors.rightMargin: root.railMargin; spacing: 12
                Text { Layout.fillWidth: true; text: root.projectTitle; elide: Text.ElideRight; color: HomeTheme.text; font.pixelSize: 13; font.weight: Font.Medium }
                IconButton { glyph: "sidebar"; hint: root.inspectorOpen ? HomeTheme.t("收起详情面板") : HomeTheme.t("显示详情面板"); active: root.inspectorOpen; onClicked: {root.forceActiveFocus();if(root.imageToolsOpen)studio.flushWorkspace();root.imageToolsOpen=false;root.inspectorOpen=!root.inspectorOpen} }
                IconButton { objectName: "conversationMoreTrigger"; glyph: "more"; hint: HomeTheme.t("作品设置与操作"); onClicked: moreMenu.popup(width-moreMenu.width,height+4)
                    HomeMenu { id: moreMenu; objectName: "conversationMoreMenu"
                        AppMenuItem { text: HomeTheme.t("保存作品工程"); onTriggered: saveDialog.open() }
                        AppMenuItem { text: HomeTheme.t("生成参数"); enabled: !studio.busy; onTriggered: if(studio.prepareConversationSettings())parameters.open() }
                        AppMenuItem { text: HomeTheme.dark ? HomeTheme.t("切换浅色") : HomeTheme.t("切换深色"); onTriggered: workspace.appearanceMode=HomeTheme.dark ? "light" : "dark" }
                        AppMenuItem { text: HomeTheme.t("模型与设置"); onTriggered: root.settingsRequested() }
                        AppMenuItem { text: HomeTheme.t("运行日志"); onTriggered: logs.open() }
                    }
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: VideoCutTheme.hairline; color: HomeTheme.line }
        }
        Item {
            Layout.fillWidth: true; Layout.fillHeight: true
            Column {
                visible: root.thread.messages.length === 0; anchors.centerIn: parent; spacing: 10
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: HomeTheme.t("从一句话开始创作"); font.pixelSize: 19; font.weight: Font.Medium; color: HomeTheme.text }
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: HomeTheme.t("图像、声音和视频，都在这里。"); font.pixelSize: 12; color: HomeTheme.secondary }
            }
            ListView {
                id: history; objectName: "conversationHistory"; anchors.fill: parent; anchors.leftMargin: root.railMargin; anchors.rightMargin: root.railMargin; clip: true; spacing: 4; boundsBehavior: Flickable.StopAtBounds
                leftMargin: 0; rightMargin: 0; topMargin: 12; bottomMargin: 0
                footer: Item { height: 12 }
                model: root.thread.messages
                reuseItems: false
                ScrollBar.vertical: ScrollBar { }
                property bool followTail: true
                onMovementEnded: followTail=atYEnd
                onCountChanged: if(followTail)Qt.callLater(()=>history.positionViewAtEnd())
                onContentHeightChanged: if(followTail)Qt.callLater(()=>history.positionViewAtEnd())
                delegate: ConversationMessage {
                    required property var modelData
                    width: history.width-history.leftMargin-history.rightMargin
                    entry: modelData; thread: root.thread; active: root.visible
                    selected: root.inspectorOpen && root.inspectedMessageId === modelData.id
                    onLayoutInteraction: history.followTail=false
                    onInspectRequested: (messageId,assetId) => root.showDetails(messageId,assetId)
                    onAssetExport: asset => root.exportAsset(asset)
                    onDeleteRequested: id => {deleteMessage.messageId=id;cleanFiles.checked=true;deleteMessage.open()}
                }
            }
        }

        CardSurface {
            objectName: "creationQuickStart"
            visible: root.showingQuickStart
            Layout.fillWidth: true; Layout.leftMargin: root.railMargin; Layout.rightMargin: root.railMargin
            implicitHeight: quickStartBody.implicitHeight+20; baseColor: HomeTheme.preview; radius: 10
            RowLayout {
                id: quickStartBody; anchors.fill: parent; anchors.margins: 10; spacing: 10
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 4
                    Label { Layout.fillWidth: true; text: HomeTheme.t("选择创作功能"); color: HomeTheme.text; font.pixelSize: 13; font.weight: Font.DemiBold; wrapMode: Text.Wrap }
                    Label { Layout.fillWidth: true; text: HomeTheme.t("从下方菜单选择功能，再输入内容并发送。"); color: HomeTheme.secondary; font.pixelSize: 12; wrapMode: Text.Wrap }
                }
                HomeButton { objectName: "quickStartDismiss"; text: HomeTheme.t("知道了"); quiet: true; compact: true; onClicked: workspace.completeQuickStart() }
                HomeButton { objectName: "quickStartChoose"; text: HomeTheme.t("选择功能"); compact: true; onClicked: {taskPicker.forceActiveFocus();taskPicker.popup.open()} }
            }
        }
        CardSurface {
            id: composer; objectName: "conversationComposer"
            Layout.fillWidth: true; Layout.leftMargin: root.railMargin; Layout.rightMargin: root.railMargin; Layout.bottomMargin: 16
            implicitHeight: composerBody.implicitHeight+20; baseColor: HomeTheme.field; radius: 12; outlined: true
            border.color: draft.activeFocus ? Qt.alpha(HomeTheme.accent,.35) : HomeTheme.outline
            ColumnLayout {
                id: composerBody; anchors.fill: parent; anchors.margins: 10; spacing: 8
                ScrollView {
                    visible: root.thread.task !== "pack" && !root.audioInput; Layout.fillWidth: true
                    Layout.minimumHeight: 32; Layout.maximumHeight: 112
                    Layout.preferredHeight: Math.min(112,Math.max(32,draft.contentHeight+draft.topPadding+draft.bottomPadding)); clip: true
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    TextArea {
                        id: draft; objectName: "conversationDraft"
                        text: root.thread.draft; onTextChanged: if(root.thread.draft !== text)root.thread.draft=text
                        placeholderText: root.thread.task === "avatar" ? HomeTheme.t("输入想让画面说的话，或导入配音…") : root.thread.task === "voice" ? HomeTheme.t("输入需要读出的文字…") : root.thread.task === "sfx" ? HomeTheme.t("描述你想听到的声音…") : root.thread.task === "edit" ? HomeTheme.t("描述修改后的画面…") : HomeTheme.t("描述你想生成的画面…")
                        wrapMode: TextEdit.Wrap; selectByMouse: true; color: HomeTheme.text; placeholderTextColor: HomeTheme.muted
                        font.pixelSize: 13; leftPadding: 5; rightPadding: 5; topPadding: 5; bottomPadding: 5; background: null
                        Accessible.name: root.thread.task === "avatar" || root.thread.task === "voice" ? HomeTheme.t("配音文案") : root.thread.task === "sfx" ? HomeTheme.t("音效描述") : HomeTheme.t("画面描述")
                        Keys.onPressed: event => {if(event.key === Qt.Key_Return && (event.modifiers & (Qt.ControlModifier|Qt.MetaModifier))){root.thread.draft=text;root.send();event.accepted=true}}
                    }
                }
                RowLayout {
                    visible: root.audioInput; Layout.fillWidth: true; spacing: 10
                    Text { Layout.maximumWidth: 150; text: root.thread.audio.name || ""; elide: Text.ElideRight; font.pixelSize: 12; color: HomeTheme.secondary }
                    ConversationAudio { Layout.fillWidth: true; Layout.minimumWidth: 0; source: root.thread.audio.url || ""; seconds: root.thread.audio.duration || 0; active: root.visible && root.audioInput; compact: true }
                    IconButton { glyph: "close"; hint: HomeTheme.t("移除配音，改用文字"); onClicked: root.thread.chooseAudio("") }
                }
                Text {
                    visible: root.thread.task === "pack" && root.thread.selected.length === 0
                    Layout.fillWidth: true; Layout.topMargin: 5; Layout.bottomMargin: 5
                    text: HomeTheme.t("选择对话里的图片、视频和声音，组合成一个视频。"); color: HomeTheme.muted; font.pixelSize: 13
                }
                Text { visible: !!root.thread.error; Layout.fillWidth: true; text: root.thread.error; wrapMode: Text.Wrap; color: HomeTheme.danger; font.pixelSize: 12 }
                Item {
                    Layout.fillWidth: true; implicitHeight: Math.max(30,toolsFlow.implicitHeight)
                    Flow {
                        id: toolsFlow; width: parent.width-sendButton.width-6; spacing: 4
                        AppSelect {
                            id: taskPicker; objectName: "conversationTask"; width: HomeTheme.english ? 156 : 112; height: 30
                            surfaceColor: HomeTheme.preview; surfaceRadius: 7; model: root.taskLabels; currentIndex: root.taskKeys.indexOf(root.thread.task)
                            surfaceBorderColor: root.showingQuickStart ? HomeTheme.accent : HomeTheme.line
                            outlined: root.showingQuickStart
                            onActivated: index => {
                                root.forceActiveFocus();root.thread.task=root.taskKeys[index]
                                if(root.showingQuickStart){workspace.completeQuickStart();draft.forceActiveFocus()}
                            }
                            Accessible.name: HomeTheme.t("创作功能"); font.pixelSize: 12
                        }
                        HomeButton {
                            visible: root.targeted; width: 106; height: 30; quiet: true
                            text: root.thread.target.name || HomeTheme.t("选择图片"); hint: HomeTheme.t("操作对象：")+(root.thread.target.name || HomeTheme.t("选择图片"))
                            leftPadding: 4; rightPadding: 4
                            contentItem: RowLayout {
                                spacing: 5
                                Image { Layout.preferredWidth: 22; Layout.preferredHeight: 22; source: root.thread.target.url || ""; fillMode: Image.PreserveAspectCrop; sourceSize: Qt.size(64,64) }
                                Text { Layout.fillWidth: true; text: root.thread.target.name || HomeTheme.t("选择图片"); elide: Text.ElideRight; font.pixelSize: 11; color: HomeTheme.text }
                            }
                            onClicked: root.openPicker("target")
                        }
                        AppSelect {
                            objectName: "conversationEditScope"; visible: root.thread.task === "edit"
                            width: HomeTheme.english ? 128 : 76; height: 30; enabled: !studio.busy
                            model: [HomeTheme.t("整图"),HomeTheme.t("局部")]
                            currentIndex: root.thread.editScope === "local" ? 1 : 0
                            Accessible.name: HomeTheme.t("编辑范围")
                            onActivated: index => {root.thread.editScope=index === 0 ? "whole" : "local";if(root.imageToolsOpen)studio.prepareConversationEdit()}
                        }
                        HomeButton { visible: root.thread.task === "avatar"; text: HomeTheme.t("配音"); iconName: "audio"; quiet: true; height: 30; hint: root.audioInput ? HomeTheme.t("更换配音") : HomeTheme.t("导入配音"); onClicked: audioPicker.open() }
                        IconButton { visible: (root.thread.task === "avatar" && !root.audioInput) || root.thread.task === "voice"; width: 28; height: 30; glyph: "volume"; hint: root.voiceName(); onClicked: voicePicker.open() }
                        HomeButton { visible: root.thread.task === "image" || root.thread.task === "edit"; text: root.thread.task === "image" ? HomeTheme.t("构图") : root.thread.editScope === "whole" ? HomeTheme.t("图生图") : HomeTheme.t("涂抹"); iconName: root.thread.task === "image" ? "rectangle" : root.thread.editScope === "whole" ? "image" : "brush"; quiet: true; active: root.imageToolsOpen; height: 30; enabled: !studio.busy; onClicked: root.openImageTools() }
                        HomeButton { visible: root.thread.task === "sfx"; text: root.thread.sfxDuration+HomeTheme.t(" 秒"); quiet: true; height: 30; onClicked: durationMenu.popup(0,-durationMenu.height-4)
                            HomeMenu { id: durationMenu; Repeater { model: [5,10,17,30]; AppMenuItem { required property int modelData; text: modelData+HomeTheme.t(" 秒"); onTriggered: root.thread.sfxDuration=modelData } } }
                        }
                        HomeButton { objectName: "packToggle"; visible: root.thread.task === "pack" && root.thread.selected.length > 0; text: root.thread.selected.length+HomeTheme.t(" 项"); iconName: "layers"; quiet: true; height: 30; hint: HomeTheme.t("在右侧编辑轨道"); onClicked: root.showPackTools() }
                        IconButton { visible: root.thread.task === "pack"; width: 28; height: 30; glyph: "plus"; hint: HomeTheme.t("选择打包素材"); onClicked: root.openPicker("pack") }
                    }
                    HomeButton {
                        id: sendButton; objectName: "conversationSend"; anchors.right: parent.right; anchors.bottom: parent.bottom; width: Math.max(86,implicitWidth); height: 30
                        text: root.thread.pending ? HomeTheme.t("停止") : root.thread.task === "pack" ? HomeTheme.t("打包") : HomeTheme.t("发送")
                        iconName: root.thread.pending ? "close" : "arrow"; primary: true
                        enabled: root.thread.pending || (!studio.busy && (root.thread.task !== "pack" || root.thread.clips.some(c=>c.lane === "visual")))
                        onClicked: if(root.thread.pending)studio.cancel();else root.send()
                    }
                }
            }
        }
    }
    ConversationInspector {
        id: inspector; visible: root.inspectorOpen
        SplitView.minimumWidth: 260
        SplitView.maximumWidth: Math.max(260,Math.min(root.maximumInspectorWidth,root.width-root.minimumChatWidth-1))
        SplitView.preferredWidth: root.preferredInspectorWidth
        thread: root.thread; entry: root.inspectedEntry; asset: root.inspectedAsset
        active: root.visible && visible; editing: root.imageToolsOpen; maskMode: root.toolsTask === "edit"; packing: root.packExpanded
        onCloseRequested: {root.forceActiveFocus();if(root.imageToolsOpen)studio.flushWorkspace();root.imageToolsOpen=false;root.inspectorOpen=false}
        onPreviewRequested: {root.forceActiveFocus();if(root.imageToolsOpen)studio.flushWorkspace();root.imageToolsOpen=false;root.packExpanded=false}
        onAssetSelected: id => {root.inspectedAssetId=id}
        onExportRequested: asset => root.exportAsset(asset)
        onEditRequested: id => root.editAsset(id)
        onAvatarRequested: id => {root.thread.task="avatar";root.thread.chooseTarget(id);draft.forceActiveFocus()}
        onAudioRequested: id => {root.thread.task="avatar";root.thread.chooseAudio(id);draft.forceActiveFocus()}
        onPackRequested: id => root.usePackAsset(id)
        onPickerRequested: root.openPicker("pack")
        onReuseRequested: entry => {root.thread.task=entry.task;root.thread.draft=entry.text;if(entry.editScope)root.thread.editScope=entry.editScope;if(entry.voiceId)root.thread.voiceId=entry.voiceId;if(entry.voiceOptions)root.thread.voiceOptions=entry.voiceOptions;for(const id of entry.refs || []){const a=root.thread.asset(id);if(a.kind === "image")root.thread.chooseTarget(id);else if(a.kind === "audio")root.thread.chooseAudio(id)}draft.forceActiveFocus()}
    }
    }
    HomeDialog {
        id: deleteMessage; objectName: "conversationDeleteDialog"; property string messageId: ""
        dialogIcon: "trash"; tone: HomeTheme.danger
        title: HomeTheme.t("删除这条消息？"); modal: true; width: Math.min(390,Overlay.overlay.width-48); anchors.centerIn: Overlay.overlay
        ColumnLayout {
            anchors.fill: parent; spacing: 12
            Text { Layout.fillWidth: true; text: HomeTheme.t("删除后无法恢复。"); wrapMode: Text.Wrap; color: HomeTheme.secondary; font.pixelSize: 13 }
            AppCheckBox { id: cleanFiles; text: HomeTheme.t("同时清理未被引用的临时素材"); checked: true }
            RowLayout {
                Layout.fillWidth: true; Layout.topMargin: 4
                Item { Layout.fillWidth: true }
                HomeButton { text: HomeTheme.t("取消"); implicitHeight: 32; onClicked: deleteMessage.close() }
                HomeButton { text: HomeTheme.t("删除消息"); implicitHeight: 32; primary: true; enabled: !studio.busy
                    onClicked: {
                        history.followTail=false
                        const scroll=history.contentY
                        if(studio.deleteConversationMessage(root.thread.id,deleteMessage.messageId,cleanFiles.checked)) {
                            deleteMessage.close()
                            Qt.callLater(()=>{history.contentY=Math.max(history.originY,Math.min(scroll,history.originY+history.contentHeight-history.height))})
                        }
                    }
                }
            }
        }
    }
    HomeDialog {
        id: assetPicker; objectName: "conversationAssetPicker"
        title: root.pickerMode === "pack" ? HomeTheme.t("选择要打包的素材") : HomeTheme.t("选择操作图片")
        readonly property var entries: root.thread.assets.filter(a=>root.pickerMode === "pack" || a.kind === "image")
        readonly property bool compactList: entries.length < 3
        readonly property int columns: compactList ? 1 : 2
        width: Math.min(compactList ? 400 : 640,Overlay.overlay ? Overlay.overlay.width-64 : 640)
        ColumnLayout {
            width: parent.width; spacing: 12
            RowLayout { Layout.fillWidth: true; spacing: 8
                Text { Layout.fillWidth: true; text: root.pickerMode === "pack" ? HomeTheme.t("已选 ")+root.thread.selected.length+HomeTheme.t(" 项") : HomeTheme.t("从对话中选择"); color: HomeTheme.secondary; font.pixelSize: 12 }
                HomeButton { text: HomeTheme.t("导入素材"); iconName: "plus"; onClicked: root.importFiles(root.pickerMode) }
            }
            GridView {
                id: assetGrid; Layout.fillWidth: true; clip: true
                Layout.preferredHeight: count ? Math.min(Math.ceil(count/assetPicker.columns)*cellHeight-8,Math.max(cellHeight-8,Math.floor(((Overlay.overlay ? Overlay.overlay.height : 720)-200)/cellHeight)*cellHeight-8)) : 144
                cellWidth: width/assetPicker.columns; cellHeight: 88
                model: assetPicker.entries; boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: assetGrid.contentHeight>assetGrid.height+8 ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded }
                HomeEmpty { visible: assetGrid.count===0; anchors.centerIn: parent; width: parent.width; title: HomeTheme.t("暂无可用素材"); description: root.pickerMode === "pack" ? HomeTheme.t("导入图片、视频或音频。") : HomeTheme.t("导入一张图片开始创作。"); iconName: "image" }
                delegate: AbstractButton {
                    id: pick; required property var modelData; width: assetGrid.cellWidth-(assetPicker.compactList ? 0 : 8); height: assetGrid.cellHeight-8
                    padding: 8; rightPadding: 34; hoverEnabled: true
                    readonly property bool chosen: root.pickerMode === "pack" ? root.thread.selected.includes(modelData.id) : root.thread.target.id === modelData.id
                    readonly property string detail: modelData.kind === "image" ? modelData.width+" × "+modelData.height : (modelData.kind === "audio" ? HomeTheme.t("音频") : HomeTheme.t("视频"))+" · "+Number(modelData.duration || 0).toFixed(1)+HomeTheme.t(" 秒")
                    Accessible.name: modelData.name; Accessible.description: detail; Accessible.checkable: true; Accessible.checked: chosen
                    background: Rectangle { radius: 8; color: pick.chosen ? HomeTheme.selection : pick.hovered ? HomeTheme.hover : HomeTheme.field; border.color: pick.chosen || pick.visualFocus ? HomeTheme.accent : HomeTheme.outline }
                    contentItem: GridLayout {
                        columns: 2; rowSpacing: 4; columnSpacing: 10
                        Rectangle {
                            Layout.preferredWidth: 64; Layout.preferredHeight: 64; Layout.rowSpan: 2
                            color: pick.modelData.kind === "audio" ? Qt.alpha(HomeTheme.accent,.08) : HomeTheme.preview; radius: 5; clip: true
                            Image { id: pickThumb; anchors.fill: parent; source: pick.modelData.kind === "image" ? pick.modelData.url : pick.modelData.poster || ""; fillMode: Image.PreserveAspectCrop; asynchronous: true; retainWhileLoading: true; sourceSize: Qt.size(320,320) }
                            StudioIcon { visible: pick.modelData.kind !== "audio" && (pick.modelData.kind !== "image" || pickThumb.status !== Image.Ready); anchors.centerIn: parent; name: pick.modelData.kind === "audio" ? "audio" : pick.modelData.kind === "video" ? "play" : "image"; width: 24; height: 24; ink: HomeTheme.accent }
                            AudioWaveform { visible: pick.modelData.kind === "audio"; anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.right: parent.right; anchors.margins: 7; height: 30; source: pick.modelData.kind === "audio" ? pick.modelData.url : ""; active: visible && assetPicker.opened; waveColor: HomeTheme.accent }
                            StudioIcon { visible: pick.modelData.kind === "audio"; anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 5; width: 12; height: 12; name: "audio"; ink: HomeTheme.secondary }
                        }
                        Text { Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.alignment: Qt.AlignBottom; text: pick.modelData.name; elide: Text.ElideRight; color: HomeTheme.text; font.pixelSize: 12; font.weight: Font.Medium }
                        Text { Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.alignment: Qt.AlignTop; text: pick.detail; elide: Text.ElideRight; color: HomeTheme.secondary; font.pixelSize: 10 }
                    }
                    Rectangle {
                        anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 9
                        width: 18; height: 18; radius: 5; color: pick.chosen ? HomeTheme.accent : HomeTheme.field; border.color: pick.chosen ? HomeTheme.accent : HomeTheme.outline
                        StudioIcon { visible: pick.chosen; anchors.centerIn: parent; name: "check"; width: 12; height: 12; ink: HomeTheme.accentInk }
                    }
                    ToolTip.visible: hovered; ToolTip.delay: 650; ToolTip.text: modelData.name
                    onClicked: if(root.pickerMode === "pack")root.thread.toggleSelected(modelData.id);else { root.thread.chooseTarget(modelData.id);assetPicker.close() }
                }
            }
            RowLayout { Layout.fillWidth: true
                HomeButton { visible: root.pickerMode === "target" && assetGrid.count>0; text: HomeTheme.t("使用最新图像"); quiet: true; onClicked: {root.thread.chooseTarget("");assetPicker.close()} }
                Item { Layout.fillWidth: true }
                HomeButton { text: root.pickerMode === "pack" ? HomeTheme.t("完成选择") : HomeTheme.t("取消"); primary: root.pickerMode === "pack"; quiet: root.pickerMode !== "pack"; onClicked: assetPicker.close() }
            }
        }
    }
    HomeDialog {
        id: audioPicker; objectName: "conversationAudioPicker"; title: HomeTheme.t("选择配音")
        width: Math.min(440,Overlay.overlay ? Overlay.overlay.width-64 : 440)
        ColumnLayout { width: parent.width; spacing: 12
            HomeButton { text: HomeTheme.t("导入音频"); iconName: "plus"; onClicked: root.importFiles("audio") }
            ListView {
                id: audioList; Layout.fillWidth: true; Layout.preferredHeight: count ? Math.min(count*82-8,Math.max(74,Math.floor(((Overlay.overlay ? Overlay.overlay.height : 720)-200)/82)*82-8)) : 144
                clip: true; spacing: 8; model: root.thread.assets.filter(a=>a.kind === "audio"); boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: audioList.contentHeight>audioList.height+8 ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded }
                HomeEmpty { visible: audioList.count===0; anchors.centerIn: parent; width: parent.width; title: HomeTheme.t("暂无可用配音"); description: HomeTheme.t("导入录音，或先生成一段语音。"); iconName: "audio" }
                delegate: HomeSection {
                    id: audioOption; required property var modelData; width: ListView.view.width; padding: 8; contentSpacing: 4
                    RowLayout { Layout.fillWidth: true; spacing: 8
                        StudioIcon { Layout.preferredWidth: 16; Layout.preferredHeight: 16; name: "audio"; ink: HomeTheme.accent }
                        Text { Layout.fillWidth: true; text: audioOption.modelData.name; elide: Text.ElideRight; color: HomeTheme.text; font.pixelSize: 12 }
                        HomeButton { text: HomeTheme.t("选用"); compact: true; implicitHeight: 26; active: root.thread.audio.id===audioOption.modelData.id; onClicked: {root.thread.chooseAudio(audioOption.modelData.id);audioPicker.close()} }
                    }
                    ConversationAudio { Layout.fillWidth: true; source: audioOption.modelData.url; seconds: audioOption.modelData.duration; compact: true; active: root.visible && audioPicker.opened }
                }
            }
            HomeButton { Layout.alignment: Qt.AlignRight; text: HomeTheme.t("取消"); quiet: true; onClicked: audioPicker.close() }
        }
    }
    HomeDialog {
        id: voicePicker; objectName: "conversationVoicePicker"; title: HomeTheme.t("选择音色")
        width: Math.min(440,Overlay.overlay ? Overlay.overlay.width-64 : 440)
        ColumnLayout { width: parent.width; spacing: 12
            GridView {
                id: voiceGrid; Layout.fillWidth: true
                readonly property int visibleRows: Math.max(1,Math.min(Math.ceil(count/2),6,Math.floor(((Overlay.overlay ? Overlay.overlay.height : 720)-170)/44)))
                Layout.preferredHeight: visibleRows*44-6
                clip: true; model: studio.sound.voices; boundsBehavior: Flickable.StopAtBounds
                cellWidth: width/2; cellHeight: 44
                ScrollBar.vertical: ScrollBar { policy: voiceGrid.count>voiceGrid.visibleRows*2 ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded }
                delegate: HomeChoiceCard { required property var modelData; width: voiceGrid.cellWidth-6; height: 38; title: modelData.name; iconName: "audio"; selected: root.thread.voiceId === modelData.id; onClicked: root.thread.voiceId=modelData.id }
            }
            VoiceStyleControls { Layout.fillWidth: true; options: root.thread.voiceOptions; onOptionEdited: (key,value)=>{const next=Object.assign({},root.thread.voiceOptions);next[key]=value;root.thread.voiceOptions=next} }
            RowLayout { Layout.fillWidth: true
                HomeButton { text: HomeTheme.t("管理、克隆与设计"); quiet: true; onClicked: {voicePicker.close();root.voicesRequested()} }
                Item { Layout.fillWidth: true }
                HomeButton { text: HomeTheme.t("完成"); primary: true; onClicked: voicePicker.close() }
            }
        }
    }
    HomeDialog {
        id: parameters; objectName: "generationParametersDialog"; title: HomeTheme.t("生成参数"); modal: true; width: 420; anchors.centerIn: Overlay.overlay
        ColumnLayout { width: parent.width; spacing: 15
            CanvasSizePicker { Layout.fillWidth: true; implicitHeight: 36; document: studio.document }
            RowLayout { Layout.fillWidth: true; spacing: 10
                Text { text: HomeTheme.t("采样步数"); color: HomeTheme.secondary; font.pixelSize: 12 }
                AppSlider { Layout.fillWidth: true; from: 1; to: 100; stepSize: 1; value: studio.steps; Accessible.name: HomeTheme.t("采样步数"); onMoved: studio.steps=value }
                Text { text: studio.steps; color: HomeTheme.text; font.pixelSize: 12; Layout.preferredWidth: 24 }
            }
            HomeButton { Layout.alignment: Qt.AlignRight; text: HomeTheme.t("完成"); primary: true; onClicked: parameters.close() }
        }
    }
    HomeDialog {
        id: logs; objectName: "conversationLogsDialog"; title: HomeTheme.t("运行日志"); modal: true; width: Math.min(studio.log ? 800 : 440,Overlay.overlay.width-60); height: Math.min(studio.log ? 580 : 240,Overlay.overlay.height-80); anchors.centerIn: Overlay.overlay
        ColumnLayout { anchors.fill: parent; spacing: 12
            ScrollView { Layout.fillWidth: true; Layout.fillHeight: true; contentWidth: availableWidth; clip: true
                TextArea { readOnly: true; text: studio.log; placeholderText: HomeTheme.t("暂无运行记录"); placeholderTextColor: HomeTheme.muted; selectByMouse: true; wrapMode: TextEdit.Wrap; color: HomeTheme.text; font.family: "Menlo"; font.pixelSize: 11; padding: 10; background: Rectangle { radius: 7; color: HomeTheme.preview; border.color: HomeTheme.outline } }
            }
            HomeButton { Layout.alignment: Qt.AlignRight; text: HomeTheme.t("完成"); onClicked: logs.close() }
        }
    }
}
