import QtQuick
import QtQuick.Controls
import QtMultimedia
import MossStudio

CardSurface {
    id: root
    property url source
    property string title: ""
    property real seconds: 0
    property bool active: true
    property bool compact: false
    property bool showTime: true
    property real pendingSeek: -1
    signal interacted()
    readonly property bool playing: player.playbackState === MediaPlayer.PlayingState
    readonly property real duration: player.duration/1000 || waveform.seconds || seconds
    readonly property real position: pendingSeek >= 0 ? pendingSeek : player.position/1000
    readonly property string timeText: time(position)+" / "+time(duration)
    implicitWidth: 200
    implicitHeight: (compact ? 28 : 40)+(showTime ? 13 : 0)+(title.length ? 18 : 0)
    baseColor: compact ? "transparent" : HomeTheme.hover; radius: 6
    function pause() { player.pause() }
    function time(value) { const s=Math.floor(Math.max(0,value)); return Math.floor(s/60)+":"+String(s%60).padStart(2,"0") }
    function seek(value) {
        interacted()
        pendingSeek=Math.max(0,Math.min(duration,value))
        if(player.source.toString() !== source.toString())player.source=source
        applySeek()
    }
    function applySeek() {
        if(pendingSeek >= 0 && player.seekable && player.duration > 0) {
            player.position=Math.round(Math.min(pendingSeek*1000,player.duration));pendingSeek=-1
        }
    }
    onActiveChanged: if(!active)player.pause()
    onSourceChanged: {player.stop();player.source="";pendingSeek=-1}
    MediaPlayer {
        id: player; audioOutput: AudioOutput { }
        onSeekableChanged: root.applySeek()
        onDurationChanged: root.applySeek()
        onMediaStatusChanged: if(mediaStatus === MediaPlayer.LoadedMedia || mediaStatus === MediaPlayer.BufferedMedia)root.applySeek()
    }
    Text {
        id: audioTitle; objectName: "audioPreviewTitle"
        visible: root.title.length > 0
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        anchors.leftMargin: root.compact ? 0 : 8; anchors.rightMargin: root.compact ? 0 : 8
        height: 18; text: root.title; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter
        color: HomeTheme.text; font.pixelSize: 11
        HoverHandler { id: titleHover }
        ToolTip.visible: titleHover.hovered && audioTitle.truncated
        ToolTip.text: root.title; ToolTip.delay: 500
    }
    Item {
        anchors.fill: parent; anchors.leftMargin: root.compact ? 0 : 8; anchors.rightMargin: root.compact ? 0 : 8
        anchors.topMargin: root.title.length ? 18 : 0
        anchors.bottomMargin: root.showTime ? 13 : 0
        Item {
            id: scrub; objectName: "audioWaveformScrubber"
            anchors.fill: parent
            activeFocusOnTab: true
            Accessible.role: Accessible.Slider; Accessible.name: (root.title.length ? root.title+" · " : "")+HomeTheme.t("音频波形，")+root.time(root.position)+" / "+root.time(root.duration)
            Accessible.description: HomeTheme.t("点击或拖动定位；左右方向键调整一秒")
            Accessible.onIncreaseAction: root.seek(root.position+1)
            Accessible.onDecreaseAction: root.seek(root.position-1)
            Keys.onLeftPressed: root.seek(root.position-1)
            Keys.onRightPressed: root.seek(root.position+1)
            Keys.onPressed: event => {
                if(event.key === Qt.Key_Home){root.seek(0);event.accepted=true}
                else if(event.key === Qt.Key_End){root.seek(root.duration);event.accepted=true}
            }
            AudioWaveform {
                id: waveform; objectName: "audioWaveform"; anchors.fill: parent
                source: root.source; active: root.active
                progress: root.duration > 0 ? root.position/root.duration : 0
                waveColor: HomeTheme.dark ? "#78899e" : "#aab5c5"; playedColor: HomeTheme.accent
            }
            Rectangle { anchors.fill: parent; radius: 4; color: "transparent"; border.width: 1; border.color: scrub.activeFocus ? HomeTheme.accent : "transparent" }
            MouseArea {
                id: seekArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                function locate(x) {root.seek(x/Math.max(1,width)*root.duration)}
                onPressed: mouse => {scrub.forceActiveFocus();locate(mouse.x)}
                onPositionChanged: mouse => {if(pressed)locate(mouse.x)}
            }
            IconButton {
                objectName: "audioPlayPause"
                anchors.centerIn: parent; width: 24; height: 24; iconSize: 14
                glyph: root.playing ? "pause" : "play"; hint: root.playing ? HomeTheme.t("暂停试听") : HomeTheme.t("播放试听"); iconColor: HomeTheme.text
                opacity: seekArea.containsMouse || hovered || visualFocus ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 120 } }
                background: Rectangle { radius: width/2; color: Qt.alpha(HomeTheme.field,.9); border.color: HomeTheme.outline }
                onClicked: {
                    root.interacted()
                    if(root.playing)player.pause()
                    else { if(player.source.toString() !== root.source.toString())player.source=root.source; if(player.duration>0&&player.position>=player.duration-50)player.position=0; player.play() }
                }
            }
            ToolTip.visible: seekArea.containsMouse
            ToolTip.delay: 500
            ToolTip.text: player.error !== MediaPlayer.NoError ? player.errorString : waveform.error ? HomeTheme.t("波形暂不可用 · 仍可试听") : root.time(seekArea.mouseX/Math.max(1,width)*root.duration)
        }
    }
    Text {
        visible: root.showTime
        anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.rightMargin: root.compact ? 0 : 8; anchors.bottomMargin: 1
        text: root.timeText; color: HomeTheme.secondary; font.pixelSize: 8; font.family: "Menlo"
    }
}
