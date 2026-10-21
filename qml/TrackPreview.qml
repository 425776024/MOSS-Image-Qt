pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import MossStudio

Rectangle {
    id: root
    objectName: "trackPreview"
    property bool audiovisual: true
    property var timeline: studio.timeline
    property bool editingEnabled: !studio.busy
    property bool playing: false
    property string fixedImage: typeof studio !== "undefined" ? studio.pageVisualUrl || "" : ""
    property string speechPoster: fixedImage
    readonly property real canvasSize: stage.width
    readonly property var tracks: timeline ? timeline.tracks : []
    readonly property var speechTrack: find("speech")
    readonly property var sfxTrack: find("sfx")
    readonly property var backgroundTrack: find("background")
    readonly property color backgroundColor: backgroundTrack.color || "#202427"
    readonly property bool lightBackground: audiovisual && backgroundTrack.enabled && backgroundTrack.kind === "color"
        && (backgroundColor.r * 0.2126 + backgroundColor.g * 0.7152 + backgroundColor.b * 0.0722) > 0.55
    readonly property var selected: timeline ? timeline.selected : ({})
    readonly property double time: timeline ? timeline.playhead : 0
    property string dragKey: ""
    property int resizeCorner: -1
    property point pressPoint: Qt.point(0, 0)
    property rect originalRect: Qt.rect(0, 0, 1, 1)
    property rect pendingRect: Qt.rect(0, 0, 1, 1)
    property double clockTime: 0
    signal editRequested(string key)
    signal canvasClicked()
    signal importRequested()
    color: StudioTheme.background
    clip: true
    Accessible.name: "创作画布"

    function find(key) {
        for (let i = 0; i < tracks.length; ++i) if (tracks[i].key === key) return tracks[i]
        return ({key: key, enabled: false, source: "", kind: "", x: 0, y: 0, width: 1, height: 1, start: 0, duration: 30, gain: 1})
    }
    function active(track) {
        if(!audiovisual)return false
        const frameTime = timeline && time >= timeline.duration ? time - 0.0001 : time
        return !!track && track.enabled && frameTime >= track.start && frameTime < track.start + track.duration
    }
    function visual(track) { return !!track && track.key !== "image" && (track.kind === "color" || !!track.source && (track.kind === "image" || track.kind === "video")) }
    function rectFor(track) {
        return dragKey === track.key ? pendingRect : Qt.rect(Number(track.x || 0), Number(track.y || 0), Number(track.width || 1), Number(track.height || 1))
    }
    function togglePlayback() {
        if (!timeline || !audiovisual) return
        if (!playing && timeline.playhead >= timeline.duration - 0.01) timeline.playhead = 0
        playing = !playing
    }
    function syncPlayer(player, track, force) {
        if (!player.source.toString() || !active(track)) {
            player.pause()
            return
        }
        const target = Math.max(0, Math.round((time - track.start) * 1000))
        const end = player.duration > 0 && target >= player.duration - 25
        const desired = player.duration > 0 ? Math.min(target, Math.max(0, player.duration - 1)) : target
        if (force || Math.abs(player.position - desired) > (playing ? 250 : 30)) player.position = desired
        if (playing && !end) {
            if (player.playbackState !== MediaPlayer.PlayingState) player.play()
        } else if (player.playbackState === MediaPlayer.PlayingState) player.pause()
    }
    function synchronize(force) {
        syncPlayer(speechPlayer, speechTrack, force)
        syncPlayer(narrationPlayer, speechTrack, force)
        syncPlayer(effectPlayer, sfxTrack, force)
    }
    function cancelTransform() { dragKey = ""; resizeCorner = -1 }
    function finishTransform() {
        const key = dragKey, box = pendingRect
        cancelTransform()
        if (key && timeline) timeline.setTransform(key, box.x, box.y, box.width, box.height)
    }
    onPlayingChanged: {
        clockTime = Date.now()
        synchronize(true)
    }
    onAudiovisualChanged: { playing = false; cancelTransform(); speechVideo.clearOutput() }
    onTimeChanged: synchronize(false)
    onTracksChanged: {
        playing = false
        if (dragKey !== "") cancelTransform()
        Qt.callLater(function() { root.synchronize(true) })
    }
    onVisibleChanged: if (!visible) { playing = false; cancelTransform() }
    onEditingEnabledChanged: if (!editingEnabled) { playing = false; cancelTransform() }
    Connections {
        target: studio
        function onPageSwitchingChanged() {
            if (!studio.switchingPage) return
            root.playing = false
            root.cancelTransform()
            speechPlayer.stop(); narrationPlayer.stop(); effectPlayer.stop()
            speechVideo.clearOutput()
        }
        function onPageActivated() {
            speechVideo.clearOutput()
            Qt.callLater(function() { root.synchronize(true) })
        }
    }
    Component.onDestruction: { speechPlayer.stop(); narrationPlayer.stop(); effectPlayer.stop() }
    Keys.onEscapePressed: cancelTransform()
    Keys.onSpacePressed: togglePlayback()
    Timer {
        interval: 33; repeat: true; running: root.playing && root.visible
        onTriggered: {
            const now = Date.now()
            const elapsed = Math.min(0.2, (now - root.clockTime) / 1000)
            root.clockTime = now
            const next = Math.min(root.timeline.duration, root.timeline.playhead + elapsed)
            root.timeline.playhead = next
            if (next >= root.timeline.duration) root.playing = false
        }
    }
    MediaPlayer {
        id: speechPlayer
        objectName: "trackSpeechPlayer"
        source: root.audiovisual && (root.speechTrack.kind === "video" || root.speechTrack.kind === "audio") ? root.speechTrack.source : ""
        audioOutput: AudioOutput {
            muted: root.speechTrack.kind === "video" && !!root.speechTrack.audioSource
            volume: Number(root.speechTrack.gain === undefined ? 1 : root.speechTrack.gain)
        }
        videoOutput: speechVideo
        onSourceChanged: { stop(); speechVideo.clearOutput(); Qt.callLater(function() { root.synchronize(true) }) }
        onMediaStatusChanged: if (mediaStatus === MediaPlayer.LoadedMedia) root.synchronize(true)
    }
    MediaPlayer {
        id: narrationPlayer
        objectName: "trackNarrationPlayer"
        source: root.audiovisual && root.speechTrack.kind === "video" ? root.speechTrack.audioSource || "" : ""
        audioOutput: AudioOutput { volume: Number(root.speechTrack.gain === undefined ? 1 : root.speechTrack.gain) }
        onSourceChanged: { stop(); Qt.callLater(function() { root.synchronize(true) }) }
        onMediaStatusChanged: if (mediaStatus === MediaPlayer.LoadedMedia) root.synchronize(true)
    }
    MediaPlayer {
        id: effectPlayer
        objectName: "trackEffectPlayer"
        source: root.audiovisual ? root.sfxTrack.source || "" : ""
        audioOutput: AudioOutput { volume: Number(root.sfxTrack.gain === undefined ? 1 : root.sfxTrack.gain) }
        onSourceChanged: { stop(); Qt.callLater(function() { root.synchronize(true) }) }
        onMediaStatusChanged: if (mediaStatus === MediaPlayer.LoadedMedia) root.synchronize(true)
    }
    Item {
        id: stage
        objectName: "trackPreviewStage"
        anchors.centerIn: parent
        width: Math.max(1, Math.min(root.width - 48, root.height - 16))
        height: width

        Rectangle { anchors.fill: parent; anchors.margins: -5; radius: 3; color: "#18000000" }
        Rectangle { anchors.fill: parent; anchors.margins: -1; color: StudioTheme.canvas; border.color: StudioTheme.line }
        Item {
            anchors.fill: parent; clip: true
            Rectangle {
                readonly property rect box: root.rectFor(root.backgroundTrack)
                x: box.x * stage.width; y: box.y * stage.height
                width: box.width * stage.width; height: box.height * stage.height
                visible: root.active(root.backgroundTrack) && root.backgroundTrack.kind === "color"
                color: root.backgroundTrack.color || "#202427"
            }
            Image {
                readonly property rect box: root.rectFor(root.backgroundTrack)
                x: box.x * stage.width; y: box.y * stage.height
                width: box.width * stage.width; height: box.height * stage.height
                visible: root.active(root.backgroundTrack) && root.backgroundTrack.kind === "image"
                source: root.backgroundTrack.kind === "image" ? root.backgroundTrack.source : ""
                fillMode: Image.PreserveAspectFit; asynchronous: true; cache: true
            }
            Image {
                objectName: "fixedCanvasImage"
                anchors.fill: parent
                visible: root.fixedImage !== ""
                source: root.fixedImage
                // The live C++ composition provider reads Studio on its GUI thread.
                fillMode: Image.PreserveAspectFit; asynchronous: !root.audiovisual; cache: true
            }
            Image {
                readonly property rect box: root.rectFor(root.speechTrack)
                x: box.x * stage.width; y: box.y * stage.height
                width: box.width * stage.width; height: box.height * stage.height
                visible: root.active(root.speechTrack) && root.speechTrack.kind === "video" && !!root.speechTrack.source
                source: root.speechPoster
                fillMode: Image.PreserveAspectFit; asynchronous: true; cache: true
            }
            VideoOutput {
                id: speechVideo
                objectName: "pageVideoOutput"
                readonly property rect box: root.rectFor(root.speechTrack)
                x: box.x * stage.width; y: box.y * stage.height
                width: box.width * stage.width; height: box.height * stage.height
                visible: root.active(root.speechTrack) && root.speechTrack.kind === "video" && root.speechTrack.source
                fillMode: VideoOutput.PreserveAspectFit
                endOfStreamPolicy: VideoOutput.KeepLastFrame
            }
        }
        Rectangle {
            id: selectionBox
            readonly property rect box: root.rectFor(root.selected)
            x: box.x * stage.width; y: box.y * stage.height
            width: box.width * stage.width; height: box.height * stage.height
            visible: root.editingEnabled && !root.playing && root.selected.key === "speech" && root.visual(root.selected) && root.active(root.selected)
            color: "transparent"; border.width: 1; border.color: StudioTheme.accent
            Repeater {
                model: 4
                delegate: Rectangle {
                    required property int index
                    x: (index === 1 || index === 2 ? selectionBox.width : 0) - 4
                    y: (index >= 2 ? selectionBox.height : 0) - 4
                    width: 8; height: 8; radius: 1
                    color: StudioTheme.accent; border.color: StudioTheme.accentInk
                }
            }
            Rectangle {
                x: 0; y: -21; height: 18; width: selectionLabel.implicitWidth + 12; radius: 3
                color: StudioTheme.accent
                Text { id: selectionLabel; anchors.centerIn: parent; text: root.selected.name || ""; color: StudioTheme.accentInk; font.pixelSize: 10 }
            }
        }
        MouseArea {
            id: transformPointer
            objectName: "previewTransformGesture"
            anchors.fill: parent; anchors.margins: -10
            enabled: root.editingEnabled && !root.playing && root.timeline.selectedTrack === "speech"
            hoverEnabled: true; preventStealing: true
            cursorShape: root.dragKey ? root.resizeCorner < 0 ? Qt.ClosedHandCursor : root.resizeCorner % 2 === 0 ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor : Qt.ArrowCursor
            function normalized(mouse) { return Qt.point((mouse.x - 10) / stage.width, (mouse.y - 10) / stage.height) }
            function contains(box, p) { return p.x >= box.x && p.x <= box.x + box.width && p.y >= box.y && p.y <= box.y + box.height }
            onPressed: mouse => {
                root.forceActiveFocus()
                const p = normalized(mouse)
                let target = root.selected
                let corner = -1
                if (selectionBox.visible) {
                    const box = root.rectFor(target)
                    const corners = [Qt.point(box.x, box.y), Qt.point(box.x + box.width, box.y), Qt.point(box.x + box.width, box.y + box.height), Qt.point(box.x, box.y + box.height)]
                    for (let i = 0; i < 4; ++i)
                        if (Math.abs((p.x - corners[i].x) * stage.width) <= 10 && Math.abs((p.y - corners[i].y) * stage.height) <= 10) { corner = i; break }
                    if (corner < 0 && !contains(box, p)) target = null
                } else target = null
                if (!target) {
                    const candidates = [root.speechTrack]
                    for (let i = 0; i < candidates.length; ++i)
                        if (root.visual(candidates[i]) && root.active(candidates[i]) && contains(root.rectFor(candidates[i]), p)) { target = candidates[i]; break }
                }
                if (!target) { root.timeline.select("image"); root.canvasClicked(); return }
                root.timeline.select(target.key)
                root.editRequested(target.key)
                root.pressPoint = p
                root.originalRect = root.rectFor(target)
                root.pendingRect = root.originalRect
                root.resizeCorner = corner
                root.dragKey = target.key
            }
            onPositionChanged: mouse => {
                if (!pressed || !root.dragKey) return
                const p = normalized(mouse), box = root.originalRect
                const dx = p.x - root.pressPoint.x, dy = p.y - root.pressPoint.y
                if (root.resizeCorner < 0) {
                    root.pendingRect = Qt.rect(Math.max(-2, -box.width + 0.04, Math.min(0.96, box.x + dx)),
                                               Math.max(-2, -box.height + 0.04, Math.min(0.96, box.y + dy)), box.width, box.height)
                    return
                }
                const left = root.resizeCorner === 0 || root.resizeCorner === 3
                const top = root.resizeCorner < 2
                let x = left ? Math.min(box.x + box.width - 0.04, box.x + dx) : box.x
                let y = top ? Math.min(box.y + box.height - 0.04, box.y + dy) : box.y
                let w = left ? box.x + box.width - x : Math.max(0.04, box.width + dx)
                let h = top ? box.y + box.height - y : Math.max(0.04, box.height + dy)
                if (!(mouse.modifiers & Qt.ShiftModifier)) {
                    const scale = Math.min(4 / box.width, 4 / box.height, Math.max(w / box.width, h / box.height))
                    w = box.width * scale; h = box.height * scale
                    if (left) x = box.x + box.width - w
                    if (top) y = box.y + box.height - h
                }
                root.pendingRect = Qt.rect(Math.max(-2, Math.min(2, x)), Math.max(-2, Math.min(2, y)), Math.min(4, w), Math.min(4, h))
            }
            onReleased: root.finishTransform()
            onCanceled: root.cancelTransform()
            onDoubleClicked: if (root.timeline && root.visual(root.selected)) root.timeline.resetTransform(root.selected.key)
        }
        LayerCanvas {
            objectName: "canvasObjectOverlay"
            anchors.fill: parent; document: studio.layerDocument; overlayMode: true
            visible: !root.audiovisual
            enabled: root.editingEnabled && !root.playing && !(root.timeline.selectedTrack === "speech" && root.speechTrack.kind === "video" && !!root.speechTrack.source)
            onCanvasClicked: {root.timeline.select("image");root.canvasClicked()}
        }
        Column {
            anchors.centerIn: parent; spacing: 14
            visible: !root.fixedImage && (!root.audiovisual || root.speechTrack.kind !== "video" && root.backgroundTrack.kind !== "image") && studio.layerDocument.layers.length === 0
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 56; height: 56; radius: 16; color: StudioTheme.selection
                StudioIcon { anchors.centerIn: parent; width: 26; height: 26; name: "spark"; ink: StudioTheme.accent }
            }
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "从一个想法开始"; font.pixelSize: 20; font.weight: Font.Medium; color: root.lightBackground ? "#263B35" : StudioTheme.text }
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: root.audiovisual ? "切换到图像模式创建画面，或单独合成语音和音效" : "在下方描述画面，或导入已有素材"; font.pixelSize: 12; color: root.lightBackground ? "#607069" : StudioTheme.muted }
            StudioButton { anchors.horizontalCenter: parent.horizontalCenter; text: root.audiovisual ? "去图像模式" : "导入图片"; iconName: "plus"; implicitHeight: 32; enabled: root.editingEnabled; onClicked: root.importRequested() }
        }
    }
    RowLayout {
        visible: root.dragKey !== ""
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.margins: 8; height: 18
        Text { text: "1:1"; color: StudioTheme.muted; font.pixelSize: 10 }
        Item { Layout.fillWidth: true }
        Text {
            text: root.dragKey ? "松开应用 · Esc 取消" : root.selected.key === "speech" && root.visual(root.selected) ? "拖动数字人 · 四角缩放" : "固定画布 · 拖动对象 · 右下角缩放"
            color: StudioTheme.muted; font.pixelSize: 10
        }
        StudioButton {
            visible: root.visual(root.selected); implicitHeight: 23; quiet: true; text: "复位"
            enabled: root.editingEnabled; hint: "恢复选中元素的位置和尺寸"
            onClicked: root.timeline.resetTransform(root.selected.key)
        }
    }
}
