import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia

ColumnLayout {
    id: root
    spacing: 0
    property bool showVideo: true
    onVisibleChanged: if (!visible) player.pause()
    Rectangle {
        Layout.fillWidth: true; Layout.fillHeight: true; color: StudioTheme.background
        Image {
            anchors.fill: parent; anchors.margins: 24
            source: studio.pageVisualUrl; fillMode: Image.PreserveAspectFit
            // Keep the page image as the poster until a decoded frame arrives.
        }
        MediaPlayer {
            id: player; objectName: "pageVideoPlayer"
            source: studio.pageVideoUrl; audioOutput: AudioOutput {}
            videoOutput: video
            onSourceChanged: { stop(); root.showVideo = true }
        }
        VideoOutput { id: video; anchors.fill: parent; anchors.margins: 24; fillMode: VideoOutput.PreserveAspectFit; endOfStreamPolicy: VideoOutput.KeepLastFrame; visible: root.showVideo && studio.pageVideoUrl !== "" }
        Column {
            anchors.centerIn: parent; spacing: 14; visible: studio.pageVisualUrl === ""
            StudioIcon { anchors.horizontalCenter: parent.horizontalCenter; name: "audio"; width: 36; height: 36; ink: StudioTheme.muted }
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "为这一页加上声音"; font.pixelSize: 18; color: StudioTheme.secondary }
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "先生成或导入图片，再添加配音音频"; font.pixelSize: 12; color: StudioTheme.muted }
        }
    }
    Rectangle {
        Layout.fillWidth: true; height: 38; color: StudioTheme.field
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 10
            StudioButton { text: "图像"; implicitHeight: 28; quiet: true; active: !root.showVideo || studio.pageVideoUrl === ""; hint: "查看页面图像"; onClicked: { root.showVideo = false; player.pause() } }
            StudioButton { text: "视频"; implicitHeight: 28; quiet: true; active: root.showVideo && studio.pageVideoUrl !== ""; enabled: studio.pageVideoUrl !== ""; hint: "查看生成视频"; onClicked: root.showVideo = true }
            Rectangle { width: 1; height: 16; color: StudioTheme.line }
            StudioButton {
                implicitHeight: 28; quiet: true; iconName: "play"
                hint: player.playbackState === MediaPlayer.PlayingState ? "暂停" : "播放"
                enabled: studio.pageVideoUrl !== ""
                onClicked: { root.showVideo = true; player.playbackState === MediaPlayer.PlayingState ? player.pause() : player.play() }
            }
            StudioSlider { Layout.fillWidth: true; from: 0; to: Math.max(1, player.duration); value: player.position; enabled: studio.pageVideoUrl !== ""; onMoved: player.position = value; Accessible.name: "页面视频进度" }
            Text { text: (player.position / 1000).toFixed(1) + " / " + (player.duration / 1000).toFixed(1) + " 秒"; font.pixelSize: 10; color: StudioTheme.muted }
        }
    }
}
