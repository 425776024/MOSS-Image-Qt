import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root; objectName: "conversationInspector"
    required property var thread
    property var entry: ({})
    property var asset: ({})
    property bool editing: false
    property bool maskMode: false
    property bool packing: false
    property bool active: true
    readonly property var media: {const all=thread.assets;return (entry.assetIds || []).map(id=>thread.asset(id)).filter(a=>!!a.id)}
    readonly property var sourceEntry: entry.role === "user" ? entry : thread.messages.find(m=>m.id===entry.replyTo) || ({})
    signal closeRequested()
    signal previewRequested()
    signal assetSelected(string id)
    signal exportRequested(var asset)
    signal editRequested(string id)
    signal avatarRequested(string id)
    signal audioRequested(string id)
    signal packRequested(string id)
    signal reuseRequested(var entry)
    signal pickerRequested()
    color: HomeTheme.field
    ColumnLayout {
        anchors.fill: parent; spacing: 0
        RowLayout {
            Layout.fillWidth: true; Layout.preferredHeight: 44; Layout.leftMargin: 8; Layout.rightMargin: 8; spacing: 6
            IconButton { visible: root.editing || root.packing; glyph: "arrow"; rotation: 180; hint: "返回素材详情"; onClicked: root.previewRequested() }
            Text { Layout.fillWidth: true; text: root.editing ? root.maskMode ? "局部编辑" : "构图布局" : root.packing ? "打包视频" : "详情"; font.pixelSize: 13; font.weight: Font.Medium; color: HomeTheme.text }
            IconButton { visible: !root.editing && !root.packing && !!root.asset.id; glyph: "export"; hint: "导出素材"; onClicked: root.exportRequested(root.asset) }
            IconButton { glyph: "close"; hint: "收起详情面板"; onClicked: root.closeRequested() }
        }
        Rectangle { Layout.fillWidth: true; height: VideoCutTheme.hairline; color: HomeTheme.line }
        Loader {
            Layout.fillWidth: true; Layout.fillHeight: true
            visible: active; active: root.editing
            sourceComponent: Component { ConversationImageEditor { thread: root.thread; maskMode: root.maskMode; embedded: true; onClosed: root.previewRequested() } }
        }
        ScrollView {
            visible: !root.editing; Layout.fillWidth: true; Layout.fillHeight: true; contentWidth: availableWidth; clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                width: parent.width; spacing: 8
                Item { Layout.preferredHeight: 0 }
                ColumnLayout {
                    visible: root.packing; Layout.fillWidth: true; Layout.leftMargin: 8; Layout.rightMargin: 8; spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        Text { Layout.fillWidth: true; text: root.thread.selected.length+" 项素材 · "+root.thread.duration.toFixed(1)+" 秒"; color: HomeTheme.text; font.pixelSize: 12 }
                        IconButton { glyph: "plus"; hint: "增减打包素材"; onClicked: root.pickerRequested() }
                    }
                    ConversationTimeline { objectName: "inspectorTimeline"; Layout.fillWidth: true; thread: root.thread }
                    Text { Layout.fillWidth: true; text: "左右拖动片段调整时间，画面和声音独立排列。"; color: HomeTheme.secondary; font.pixelSize: 11; wrapMode: Text.Wrap }
                    CardSurface {
                        Layout.fillWidth: true; implicitHeight: selectedMedia.implicitHeight; radius: 8; outlined: true; baseColor: HomeTheme.field
                        ColumnLayout {
                            id: selectedMedia; width: parent.width; spacing: 0
                            RowLayout {
                                Layout.fillWidth: true; Layout.preferredHeight: 36; Layout.leftMargin: 8; Layout.rightMargin: 6; spacing: 4
                                Text { Layout.fillWidth: true; text: "已选素材 · "+root.thread.selected.length; color: HomeTheme.secondary; font.pixelSize: 11; font.weight: Font.Medium }
                                HomeButton { text: "顺序排列"; compact: true; quiet: true; implicitHeight: 26; onClicked: root.thread.arrange() }
                            }
                            Rectangle { Layout.fillWidth: true; height: 1; color: HomeTheme.line }
                            Repeater {
                                model: root.thread.selected
                                Item {
                                    id: mediaRow; required property string modelData; required property int index
                                    readonly property var item: {const all=root.thread.assets;return root.thread.asset(modelData)}
                                    Layout.fillWidth: true; implicitHeight: 48
                                    HoverHandler { id: rowHover }
                                    Rectangle { anchors.fill: parent; anchors.margins: 1; color: rowHover.hovered ? HomeTheme.hover : "transparent"; radius: 6 }
                                    RowLayout {
                                        anchors.fill: parent; anchors.margins: 8; spacing: 8
                                        Rectangle {
                                            Layout.preferredWidth: 32; Layout.preferredHeight: 32; radius: 5; clip: true
                                            color: mediaRow.item.kind === "audio" ? Qt.alpha(HomeTheme.accent,.10) : HomeTheme.preview
                                            Image { id: mediaThumb; anchors.fill: parent; visible: mediaRow.item.kind !== "audio"; source: mediaRow.item.kind === "image" ? mediaRow.item.url : mediaRow.item.poster || ""; fillMode: Image.PreserveAspectCrop; asynchronous: true; sourceSize: Qt.size(80,80) }
                                            StudioIcon { anchors.centerIn: parent; width: 19; height: 19; visible: mediaRow.item.kind === "audio" || mediaThumb.status !== Image.Ready; name: mediaRow.item.kind === "audio" ? "audio" : mediaRow.item.kind === "video" ? "play" : "image"; ink: HomeTheme.accent }
                                        }
                                        ColumnLayout {
                                            Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 2
                                            Text { Layout.fillWidth: true; text: mediaRow.item.name; color: HomeTheme.text; font.pixelSize: 12; elide: Text.ElideRight }
                                            Text { Layout.fillWidth: true; text: (mediaRow.item.kind === "image" ? "图像" : mediaRow.item.kind === "video" ? "视频" : "音频")+" · "+(mediaRow.item.duration || 0).toFixed(1)+" 秒"; color: HomeTheme.muted; font.pixelSize: 10; elide: Text.ElideRight }
                                        }
                                        IconButton { implicitWidth: 22; implicitHeight: 22; iconSize: 13; glyph: "close"; hint: "从打包中移除："+mediaRow.item.name; onClicked: root.thread.toggleSelected(mediaRow.modelData) }
                                    }
                                    Rectangle { visible: mediaRow.index<root.thread.selected.length-1; anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.leftMargin: 48; anchors.right: parent.right; anchors.rightMargin: 8; height: 1; color: HomeTheme.line }
                                }
                            }
                        }
                    }
                }
                ColumnLayout {
                    visible: !root.packing; Layout.fillWidth: true; Layout.leftMargin: 8; Layout.rightMargin: 8; spacing: 8
                    Rectangle {
                        visible: root.asset.kind === "image"; Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(420,Math.max(180,width/(root.asset.width>0 && root.asset.height>0 ? root.asset.width/root.asset.height : 1)))
                        color: HomeTheme.preview; radius: 7; clip: true
                        Image { objectName: "inspectorImage"; anchors.fill: parent; source: root.asset.kind === "image" ? root.asset.url || "" : ""; fillMode: Image.PreserveAspectFit; sourceSize: Qt.size(1600,1600); asynchronous: true; retainWhileLoading: true }
                    }
                    Loader {
                        Layout.fillWidth: true; visible: active; active: root.asset.kind === "video"
                        Layout.preferredHeight: item ? item.implicitHeight : 0
                        sourceComponent: Component { ConversationVideo { asset: root.asset; active: root.active; expanded: true; allowExpand: false } }
                    }
                    ConversationAudio { visible: root.asset.kind === "audio"; Layout.fillWidth: true; source: root.asset.kind === "audio" ? root.asset.url || "" : ""; seconds: root.asset.duration || 0; active: root.active && visible }
                    Text { visible: !!root.asset.id; Layout.fillWidth: true; text: root.asset.name || ""; color: HomeTheme.text; font.pixelSize: 14; font.weight: Font.Medium; wrapMode: Text.Wrap }
                    Text {
                        visible: !!root.asset.id; Layout.fillWidth: true; font.pixelSize: 11; color: HomeTheme.secondary
                        text: (root.asset.kind === "image" ? "图像" : root.asset.kind === "video" ? "视频" : "音频")+(root.asset.width>0 ? " · "+root.asset.width+" × "+root.asset.height : "")+(root.asset.kind !== "image" ? " · "+(root.asset.duration || 0).toFixed(1)+" 秒" : "")
                    }
                    Flow {
                        visible: root.media.length > 1; Layout.fillWidth: true; spacing: 6
                        Repeater { model: root.media
                            HomeButton { required property var modelData; text: modelData.name; iconName: modelData.kind === "audio" ? "audio" : modelData.kind === "video" ? "play" : "image"; active: root.asset.id === modelData.id; quiet: true; width: Math.min(160,implicitWidth); implicitHeight: 30; onClicked: root.assetSelected(modelData.id) }
                        }
                    }
                    Flow {
                        visible: !!root.asset.id; Layout.fillWidth: true; spacing: 6
                        HomeButton { visible: root.asset.kind === "image"; text: "局部编辑"; iconName: "brush"; implicitHeight: 30; primary: true; enabled: !studio.busy; onClicked: root.editRequested(root.asset.id) }
                        HomeButton { visible: root.asset.kind === "image"; text: "让画面说话"; iconName: "person"; implicitHeight: 30; quiet: true; enabled: !studio.busy; onClicked: root.avatarRequested(root.asset.id) }
                        HomeButton { visible: root.asset.kind === "audio"; text: "用作配音"; iconName: "audio"; implicitHeight: 30; enabled: !studio.busy; onClicked: root.audioRequested(root.asset.id) }
                        HomeButton { text: root.thread.selected.includes(root.asset.id) ? "编辑打包" : "加入打包"; iconName: "layers"; implicitHeight: 30; quiet: true; onClicked: root.packRequested(root.asset.id) }
                    }
                    Rectangle { visible: !!root.sourceEntry.text && !!root.asset.id; Layout.fillWidth: true; height: 1; color: HomeTheme.line }
                    TextArea {
                        visible: !!root.sourceEntry.text; Layout.fillWidth: true
                        text: root.sourceEntry.text || ""; textFormat: TextEdit.PlainText; readOnly: true; selectByMouse: true
                        wrapMode: TextEdit.Wrap; color: HomeTheme.text; font.pixelSize: 13; padding: 0; background: null
                    }
                    HomeButton { visible: !!root.sourceEntry.text; text: "再次使用这段描述"; iconName: "edit"; quiet: true; implicitHeight: 30; enabled: !studio.busy; onClicked: root.reuseRequested(root.sourceEntry) }
                    Repeater {
                        model: root.entry.role === "user" ? root.entry.refs || [] : []
                        HomeButton { required property string modelData; Layout.fillWidth: true; text: root.thread.asset(modelData).name || "素材"; iconName: "image"; quiet: true; implicitHeight: 30; onClicked: root.assetSelected(modelData) }
                    }
                    Text { visible: !root.asset.id && !root.sourceEntry.text; Layout.fillWidth: true; Layout.topMargin: 80; text: root.entry.status === "running" ? studio.status : root.entry.text || "点击对话中的卡片\n在这里预览或继续编辑"; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; color: HomeTheme.muted; font.pixelSize: 13 }
                }
                Item { Layout.preferredHeight: 12 }
            }
        }
    }
}
