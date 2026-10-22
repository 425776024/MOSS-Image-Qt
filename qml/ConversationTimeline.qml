import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property var thread
    readonly property real extent: Math.max(15, Math.ceil((thread.duration+2)/5)*5)
    spacing: 4
    RowLayout {
        Layout.fillWidth: true; spacing: 0
        Item { Layout.preferredWidth: 44 }
        Item {
            Layout.fillWidth: true
            implicitHeight: 14
            Repeater { model: 5
                Text { required property int index; x: index*(parent.width-30)/4; text: (root.extent*index/4).toFixed(root.extent%4 ? 1 : 0)+"s"; color: HomeTheme.faint; font.pixelSize: 11; font.family: "Menlo" }
            }
        }
    }
    Repeater {
        model: ["visual","audio"]
        delegate: RowLayout {
            id: track
            required property string modelData
            Layout.fillWidth: true; spacing: 0
            Text { Layout.preferredWidth: 44; text: track.modelData === "visual" ? HomeTheme.t("画面") : HomeTheme.t("声音"); font.pixelSize: 12; color: HomeTheme.secondary }
            Rectangle {
                id: lane
                Layout.fillWidth: true; Layout.preferredHeight: 32; radius: 5; color: HomeTheme.hover; clip: true
                Repeater {
                    model: root.thread.clips.filter(c => c.lane === track.modelData)
                    delegate: Rectangle {
                        id: clip
                        required property var modelData
                        property bool moving: false
                        property real dragX: 0
                        x: moving ? dragX : modelData.start/root.extent*lane.width
                        width: Math.max(8,modelData.duration/root.extent*lane.width); height: 26; y: 3; radius: 4
                        color: track.modelData === "visual" ? Qt.alpha(HomeTheme.accent,.15) : Qt.alpha(HomeTheme.dark ? "#83C5AB" : "#317560",.14)
                        border.color: drag.active || activeFocus ? HomeTheme.accent : "transparent"
                        clip: true; focus: false; activeFocusOnTab: true
                        Accessible.role: Accessible.Button
                        Accessible.name: modelData.name+HomeTheme.t("，开始 ")+modelData.start.toFixed(1)+HomeTheme.t(" 秒，时长 ")+modelData.duration.toFixed(1)+HomeTheme.t(" 秒，左右拖动调整")
                        Keys.onLeftPressed: root.thread.moveClip(modelData.id,modelData.start-.5)
                        Keys.onRightPressed: root.thread.moveClip(modelData.id,modelData.start+.5)
                        RowLayout {
                            anchors.fill: parent; anchors.margins: 3; spacing: 4
                            Image { visible: track.modelData === "visual"; Layout.preferredWidth: 20; Layout.fillHeight: true; source: { const a=root.thread.asset(clip.modelData.assetId); return a.kind === "image" ? a.url : a.poster || "" } fillMode: Image.PreserveAspectCrop; asynchronous: true; retainWhileLoading: true; sourceSize: Qt.size(60,60) }
                            Text { Layout.fillWidth: true; text: clip.modelData.name+" · "+clip.modelData.duration.toFixed(1)+"s"; font.pixelSize: 11; color: HomeTheme.text; elide: Text.ElideRight }
                        }
                        MouseArea {
                            id: drag; anchors.fill: parent; acceptedButtons: Qt.LeftButton; cursorShape: Qt.SizeHorCursor
                            property real pressX: 0
                            property real origin: 0
                            property bool active: pressed
                            onPressed: mouse => {clip.forceActiveFocus();pressX=mapToItem(lane,mouse.x,mouse.y).x;origin=clip.x;clip.dragX=origin;clip.moving=true}
                            onPositionChanged: mouse => {if(pressed)clip.dragX=Math.max(0,Math.min(lane.width-clip.width,origin+mapToItem(lane,mouse.x,mouse.y).x-pressX))}
                            onReleased: {const next=clip.dragX/lane.width*root.extent;clip.moving=false;if(Math.abs(next-clip.modelData.start)>.05)root.thread.moveClip(clip.modelData.id,next)}
                            onCanceled: clip.moving=false
                        }
                    }
                }
                Text { anchors.centerIn: parent; visible: !root.thread.clips.some(c => c.lane === track.modelData); text: track.modelData === "visual" ? HomeTheme.t("选择图片或视频") : HomeTheme.t("选择配音或音效"); font.pixelSize: 11; color: HomeTheme.faint }
            }
        }
    }
}
