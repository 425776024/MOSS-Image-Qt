import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var entry
    required property var thread
    property bool active: true
    property bool selected: false
    property bool textExpanded: false
    readonly property bool outgoing: entry.role === "user"
    readonly property var firstAsset: { const all=thread.assets; return thread.asset((entry.assetIds || [])[0] || "") }
    readonly property bool visual: firstAsset.kind === "image" || firstAsset.kind === "video"
    readonly property bool audio: !outgoing && firstAsset.kind === "audio"
    readonly property string summary: (entry.text || "").replace(/\s+/g," ")
    readonly property string taskLabel: outgoing ? "" : HomeTheme.taskLabel(entry.task)
    readonly property string assetDetail: (firstAsset.kind === "video" ? (firstAsset.duration || 0).toFixed(1)+HomeTheme.t(" 秒") : firstAsset.width+" × "+firstAsset.height)+((entry.assetIds || []).length > 1 ? " · "+entry.assetIds.length+HomeTheme.t(" 项素材") : "")
    readonly property real thumbnailRatio: firstAsset.width > 0 && firstAsset.height > 0 ? firstAsset.width/firstAsset.height : 1
    readonly property real thumbnailWidth: Math.min(48,48*thumbnailRatio)
    readonly property real thumbnailHeight: Math.min(48,48/thumbnailRatio)
    readonly property real visualCardWidth: Math.min(192,Math.max(128,Math.ceil(12+thumbnailWidth+6+Math.max(assetNameMeasure.advanceWidth+20,assetDetailMeasure.advanceWidth))))
    signal inspectRequested(string messageId, string assetId)
    signal layoutInteraction()
    signal assetExport(var asset)
    signal deleteRequested(string id)
    function inspect() {root.inspectRequested(entry.id,firstAsset.id || "")}
    function toggleText() {root.layoutInteraction();textExpanded=!textExpanded;root.inspect()}
    implicitHeight: Math.max(typeAvatar.visible ? typeAvatar.height : 0,bubble.implicitHeight+(audio ? audioTime.implicitHeight+3 : 0))
    FontMetrics { id: metrics; font.pixelSize: 13 }
    TextMetrics { id: measure; text: root.summary; font.pixelSize: 13 }
    TextMetrics { id: assetNameMeasure; text: root.firstAsset.name || HomeTheme.t("素材"); font.pixelSize: 12 }
    TextMetrics { id: assetDetailMeasure; text: root.assetDetail; font.pixelSize: 10 }
    GenerationAvatar {
        id: typeAvatar; objectName: "resultTypeAvatar"
        task: root.outgoing ? "" : root.entry.task || ""
        selected: root.selected; width: 30; height: 30
    }
    CardSurface {
        id: bubble; objectName: root.outgoing ? "requestCard" : "creationCard"
        width: Math.min(Math.max(0,root.width-(typeAvatar.visible ? 38 : 0)),root.outgoing ? Math.min(350,Math.max(60,measure.advanceWidth+34)) : root.visual ? root.visualCardWidth : 192)
        x: root.outgoing ? root.width-width : typeAvatar.visible ? 38 : 0
        implicitHeight: content.implicitHeight+12
        baseColor: root.outgoing ? HomeTheme.hover : HomeTheme.field
        outlined: !root.outgoing; radius: 8
        border.color: root.selected ? Qt.alpha(HomeTheme.accent,.45) : root.outgoing ? "transparent" : HomeTheme.line
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: root.outgoing ? root.summary : (root.taskLabel ? root.taskLabel+" · " : "")+HomeTheme.t("查看素材：")+(root.firstAsset.name || root.entry.text || HomeTheme.t("生成中"))
        Accessible.onPressAction: root.outgoing ? root.toggleText() : root.inspect()
        Keys.onReturnPressed: root.outgoing ? root.toggleText() : root.inspect()
        Keys.onPressed: event => {if(event.key===Qt.Key_Menu || (event.key===Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))){actions.popup(0,bubble.height+4);event.accepted=true}}
        HoverHandler { id: hover }
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.LeftButton | Qt.RightButton; onClicked: mouse => {if(mouse.button===Qt.RightButton)actions.popup();else root.outgoing ? root.toggleText() : root.inspect()} }
        ColumnLayout {
            id: content; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 6; spacing: 3
            RowLayout {
                visible: root.outgoing; Layout.fillWidth: true; Layout.rightMargin: 22; spacing: 3
                Item {
                    Layout.fillWidth: true; Layout.minimumWidth: 0
                    Layout.preferredHeight: root.textExpanded ? Math.min(metrics.lineSpacing*3,expandedText.implicitHeight) : metrics.lineSpacing
                    Text {
                        objectName: "requestSummary"
                        visible: !root.textExpanded; anchors.fill: parent; text: root.summary; textFormat: Text.PlainText
                        color: HomeTheme.text; font.pixelSize: 13; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter
                    }
                    Flickable {
                        id: requestScroll; objectName: "requestTextScroll"; visible: root.textExpanded; anchors.fill: parent; clip: true
                        contentWidth: width; contentHeight: expandedText.implicitHeight; boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                        Text {
                            id: expandedText; width: requestScroll.width-5; text: root.entry.text || ""
                            wrapMode: Text.Wrap; textFormat: Text.PlainText; font.pixelSize: 13; color: HomeTheme.text
                        }
                    }
                }
            }
            RowLayout {
                visible: !root.outgoing && root.visual; Layout.fillWidth: true; spacing: 6
                Item {
                    Layout.preferredWidth: root.thumbnailWidth; Layout.preferredHeight: root.thumbnailHeight
                    Rectangle {
                        anchors.fill: parent; visible: root.firstAsset.kind === "image"; color: HomeTheme.preview; radius: 5; clip: true
                        Image { anchors.fill: parent; source: root.firstAsset.kind === "image" ? root.firstAsset.url || "" : ""; fillMode: Image.PreserveAspectFit; sourceSize: Qt.size(160,160); asynchronous: true; retainWhileLoading: true }
                    }
                    Loader {
                        anchors.fill: parent; active: root.firstAsset.kind === "video"
                        sourceComponent: Component { ConversationVideo { asset: root.firstAsset; active: root.active; compact: true; onInteracted: root.inspect() } }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 3
                    Text { Layout.fillWidth: true; Layout.rightMargin: 20; text: root.firstAsset.name || HomeTheme.t("素材"); textFormat: Text.PlainText; elide: Text.ElideRight; font.pixelSize: 12; color: HomeTheme.text }
                    Text {
                        Layout.fillWidth: true; elide: Text.ElideRight; font.pixelSize: 10; color: HomeTheme.secondary
                        text: root.assetDetail
                    }
                }
            }
            ConversationAudio { id: audioPreview; objectName: "messageAudioPreview"; visible: root.audio; Layout.fillWidth: true; Layout.minimumWidth: 0; source: root.firstAsset.kind === "audio" ? root.firstAsset.url || "" : ""; seconds: root.firstAsset.duration || 0; active: root.active && visible; compact: true; showTime: false; onInteracted: root.inspect() }
            RowLayout {
                visible: !root.outgoing && !root.firstAsset.id; Layout.fillWidth: true; spacing: 6
                BusyIndicator { visible: root.entry.status === "running"; running: visible && root.active; implicitWidth: 16; implicitHeight: 16 }
                Text {
                    Layout.fillWidth: true; Layout.rightMargin: 20; text: root.entry.status === "running" ? HomeTheme.t(studio.status) : root.entry.text || HomeTheme.t("任务未完成")
                    font.pixelSize: 12; color: root.entry.status === "failed" ? HomeTheme.danger : HomeTheme.secondary
                    wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight
                }
            }
        }
        Row {
            anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 5; spacing: 2
            AppCheckBox {
                visible: !root.outgoing && !!root.firstAsset.id && root.thread.task === "pack"
                width: 18; height: 20; checked: root.thread.selected.includes(root.firstAsset.id)
                Accessible.name: HomeTheme.t("选择素材：")+root.firstAsset.name
                Accessible.onToggleAction: root.thread.toggleSelected(root.firstAsset.id)
                onClicked: root.thread.toggleSelected(root.firstAsset.id)
            }
            IconButton {
                objectName: "messageActions"; width: 20; height: 20; iconSize: 13; glyph: "more"; hint: HomeTheme.t("消息操作")
                visible: root.outgoing || root.thread.task !== "pack" || !root.firstAsset.id
                opacity: root.outgoing || hover.hovered || activeFocus || actions.opened ? 1 : 0
                onClicked: actions.popup(bubble.width-actions.width,bubble.height+4)
            }
        }
        HomeMenu {
            id: actions
            AppMenuItem {
                objectName: "messageToggleText"; visible: root.outgoing
                text: root.textExpanded ? HomeTheme.t("收起文字") : HomeTheme.t("展开文字，最多三行")
                onTriggered: root.toggleText()
            }
            AppMenuItem { text: HomeTheme.t("查看详情"); onTriggered: root.inspect() }
            AppMenuItem { visible: !!root.firstAsset.id; text: HomeTheme.t("导出素材"); onTriggered: root.assetExport(root.firstAsset) }
            AppMenuItem { visible: root.outgoing; text: HomeTheme.t("复制文字"); onTriggered: studio.copyText(root.entry.text || "") }
            MenuSeparator { }
            AppMenuItem { text: HomeTheme.t("删除消息"); destructive: true; enabled: !studio.busy && !root.thread.pending; onTriggered: root.deleteRequested(root.entry.id) }
        }
    }
    Text {
        id: audioTime; objectName: "messageAudioTime"
        visible: root.audio; anchors.top: bubble.bottom; anchors.topMargin: 3; anchors.right: bubble.right
        text: audioPreview.timeText; color: HomeTheme.secondary; font.pixelSize: 8; font.family: "Menlo"
    }
}
