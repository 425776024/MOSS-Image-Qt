import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import MossStudio

CardSurface {
    id: root
    property url source
    property real seconds: 0
    property bool active: true
    property bool compact: false
    property real pendingSeek: -1
    signal interacted()
    readonly property bool playing: player.playbackState === MediaPlayer.PlayingState
    readonly property real duration: player.duration/1000 || waveform.seconds || seconds
    readonly property real position: pendingSeek >= 0 ? pendingSeek : player.position/1000
    implicitWidth: 200
    implicitHeight: compact ? 28 : 40
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
    RowLayout {
        anchors.fill: parent; anchors.leftMargin: root.compact ? 0 : 8; anchors.rightMargin: root.compact ? 0 : 8; spacing: 5
        IconButton {
            Layout.preferredWidth: 24; Layout.preferredHeight: 24; iconSize: 14; glyph: root.playing ? "pause" : "play"; hint: root.playing ? "暂停试听" : "播放试听"; iconColor: HomeTheme.text
            onClicked: {
                root.interacted()
                if(root.playing)player.pause()
                else { if(player.source.toString() !== root.source.toString())player.source=root.source; if(player.duration>0&&player.position>=player.duration-50)player.position=0; player.play() }
            }
        }
        Item {
            id: scrub; objectName: "audioWaveformScrubber"
            Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredWidth: 130; Layout.preferredHeight: 26
            activeFocusOnTab: true
            Accessible.role: Accessible.Slider; Accessible.name: "音频波形，"+root.time(root.position)+" / "+root.time(root.duration)
            Accessible.description: "点击或拖动定位；左右方向键调整一秒"
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
            ToolTip.visible: seekArea.containsMouse
            ToolTip.delay: 500
            ToolTip.text: player.error !== MediaPlayer.NoError ? player.errorString : waveform.error ? "波形暂不可用 · 仍可试听" : root.time(seekArea.mouseX/Math.max(1,width)*root.duration)
        }
        Text { text: root.time(root.position)+" / "+root.time(root.duration); color: HomeTheme.secondary; font.pixelSize: root.compact ? 9 : 10; font.family: "Menlo" }
    }
}
