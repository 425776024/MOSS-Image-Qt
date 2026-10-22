import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia

Column {
    id: root
    required property var asset
    property bool active: true
    property bool expanded: false
    property bool compact: false
    property bool allowExpand: true
    onAssetChanged: {player.stop();player.source=""}
    signal expandRequested()
    signal interacted()
    readonly property bool playing: player.playbackState === MediaPlayer.PlayingState
    readonly property real ratio: asset.width > 0 && asset.height > 0 ? asset.width/asset.height : poster.sourceSize.width > 0 && poster.sourceSize.height > 0 ? poster.sourceSize.width/poster.sourceSize.height : 16/9
    spacing: 4
    function playPause() {
        interacted()
        if(playing)player.pause()
        else {if(player.source.toString() !== asset.url.toString())player.source=asset.url;if(player.duration>0&&player.position>=player.duration-50)player.position=0;player.play()}
    }
    function time(value) { const s=Math.floor(Math.max(0,value));return Math.floor(s/60)+":"+String(s%60).padStart(2,"0") }
    onActiveChanged: if(!active)player.pause()
    Rectangle {
        id: stage; objectName: "inlineVideoStage"
        width: Math.min(parent.width,(root.expanded ? 520 : root.compact ? 84 : 210)*root.ratio); height: width/root.ratio
        color: "#101214"; radius: 6; clip: true
        HoverHandler { id: hover }
        VideoOutput { id: output; anchors.fill: parent; fillMode: VideoOutput.PreserveAspectFit; endOfStreamPolicy: VideoOutput.KeepLastFrame }
        Image { id: poster; anchors.fill: parent; visible: player.source.toString() === ""; source: root.asset.poster || ""; fillMode: Image.PreserveAspectFit; asynchronous: true; retainWhileLoading: true; sourceSize: Qt.size(1280,1280) }
        TapHandler { onTapped: root.playPause() }
        IconButton {
            opacity: hover.hovered || hovered || visualFocus ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 120 } }
            anchors.centerIn: parent
            width: root.compact ? 28 : 36; height: width; iconSize: root.compact ? 14 : 17
            glyph: root.playing ? "pause" : "play"; iconColor: "white"; hint: root.playing ? HomeTheme.t("暂停视频") : HomeTheme.t("播放视频")
            background: Rectangle { radius: width/2; color: "#99101214"; border.color: "#50ffffff" }
            onClicked: root.playPause()
        }
        BusyIndicator { anchors.centerIn: parent; running: player.mediaStatus === MediaPlayer.LoadingMedia; visible: running; width: 28; height: 28 }
        Rectangle {
            id: transport; visible: !root.compact; anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 30
            opacity: hover.hovered || playButton.activeFocus || progress.activeFocus || expandButton.activeFocus ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 120 } }
            color: "#b0101214"
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 4; anchors.rightMargin: 4; spacing: 4
                IconButton { id: playButton; glyph: root.playing ? "pause" : "play"; hint: root.playing ? HomeTheme.t("暂停视频") : HomeTheme.t("播放视频"); iconColor: "white"; onClicked: root.playPause() }
                AppSlider { id: progress; Layout.fillWidth: true; from: 0; to: Math.max(1,player.duration); value: player.position; onMoved: player.position=value; Accessible.name: HomeTheme.t("视频进度"); accentColor: "#e0ffffff"; thumbColor: "white"; trackGradient: Gradient { GradientStop { position: 0; color: "#60ffffff" } GradientStop { position: 1; color: "#60ffffff" } } }
                Text { text: root.time(player.position/1000)+" / "+root.time(player.duration/1000 || root.asset.duration); color: "#eeffffff"; font.pixelSize: 9; font.family: "Menlo" }
                IconButton { id: expandButton; visible: root.allowExpand; glyph: "expand"; iconSize: 13; iconColor: "white"; hint: root.expanded ? HomeTheme.t("收起视频") : HomeTheme.t("展开视频"); onClicked: root.expandRequested() }
            }
        }
    }
    Text { visible: player.error !== MediaPlayer.NoError; width: parent.width; text: player.errorString; color: HomeTheme.danger; wrapMode: Text.Wrap; font.pixelSize: 11 }
    MediaPlayer { id: player; audioOutput: AudioOutput { } videoOutput: output }
}
