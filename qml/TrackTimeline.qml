pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    objectName: "trackTimeline"
    property var timeline: studio.timeline
    property bool editingEnabled: !studio.busy
    property bool playing: false
    property bool collapsed: false
    readonly property var tracks: timeline ? timeline.tracks : []
    readonly property real duration: Math.max(0.1, timeline ? timeline.duration : 30)
    readonly property int labelWidth: 132
    readonly property int toolbarHeight: 34
    readonly property int rowHeight: 28
    readonly property real laneWidth: Math.max(1, width - labelWidth - 60)
    readonly property real pixelsPerSecond: dragKey !== "" ? gestureScale : laneWidth / duration
    property real gestureScale: 1
    property string dragKey: ""
    property string dragMode: ""
    property real pressX: 0
    property real originalStart: 0
    property real originalDuration: 30
    property real pendingStart: 0
    property real pendingDuration: 30
    property bool dragChanged: false
    signal togglePlayback()
    signal editRequested(string key)
    signal exportRequested(string key)
    implicitHeight: collapsed ? toolbarHeight : 120
    color: StudioTheme.panel
    clip: true
    Accessible.name: HomeTheme.t("音频范围")

    function track(key) {
        for (let i = 0; i < tracks.length; ++i) if (tracks[i].key === key) return tracks[i]
        return ({key:key, start:0, duration:30, source:"", kind:"audio", enabled:false})
    }
    function requestEdit(key) { if (timeline) { timeline.select(key); editRequested(key) } }
    function stamp(value) {
        const tenth = Math.floor(Math.max(0, value) * 10 + 0.0001)
        return Math.floor(tenth / 600).toString().padStart(2, "0") + ":" + Math.floor(tenth / 10 % 60).toString().padStart(2, "0") + "." + (tenth % 10)
    }
    function startFor(item) { return dragKey === item.key ? pendingStart : Number(item.start || 0) }
    function durationFor(item) { return dragKey === item.key ? pendingDuration : Number(item.duration || 30) }
    function finishDrag(commit) {
        const key = dragKey, start = pendingStart, length = pendingDuration, changed = dragChanged
        dragKey = ""; dragMode = ""; dragChanged = false
        if (commit && changed && key !== "" && timeline) timeline.setRange(key, start, length)
    }
    function toggleTrack(key) { if (editingEnabled && timeline) timeline.setValue(key, "enabled", !track(key).enabled) }
    function keyMove(direction, modifiers) {
        if (!timeline) return
        const step = modifiers & Qt.ShiftModifier ? 1 : 0.1
        const key = timeline.selectedTrack, item = track(key)
        if (modifiers & Qt.AltModifier && editingEnabled && key !== "background" && key !== "image" && item.source)
            timeline.setRange(key, Math.max(0, item.start + direction * step), item.duration)
        else timeline.playhead = Math.max(0, Math.min(duration, timeline.playhead + direction * step))
    }
    onCollapsedChanged: finishDrag(false)
    onEditingEnabledChanged: if (!editingEnabled) finishDrag(false)
    onVisibleChanged: if (!visible) finishDrag(false)
    Keys.onEscapePressed: finishDrag(false)
    Keys.onSpacePressed: togglePlayback()
    Keys.onLeftPressed: event => keyMove(-1, event.modifiers)
    Keys.onRightPressed: event => keyMove(1, event.modifiers)
    Keys.onReturnPressed: if (timeline && timeline.selectedTrack !== "image") requestEdit(timeline.selectedTrack)
    Keys.onPressed: event => { if (event.key === Qt.Key_M && timeline && timeline.selectedTrack !== "image") { toggleTrack(timeline.selectedTrack); event.accepted = true } }

    Rectangle { width: parent.width; height: 3; color: StudioTheme.background }
    RowLayout {
        x: 8; y: 4; width: parent.width - 16; height: 28; spacing: 8
        StudioButton {
            objectName: "timelinePlay"; implicitWidth: 28; implicitHeight: 26; quiet: true
            iconName: root.playing ? "" : "play"; text: root.playing ? "Ⅱ" : ""
            hint: root.playing ? HomeTheme.t("暂停预览 · 空格") : HomeTheme.t("播放预览 · 空格")
            onClicked: root.togglePlayback()
        }
        Text { objectName: "timelineTime"; text: root.stamp(root.timeline ? root.timeline.playhead : 0) + " / " + root.stamp(root.duration); color: StudioTheme.secondary; font.pixelSize: 11; font.family: "Menlo" }
        Item { Layout.fillWidth: true }
        Text { visible: root.width > 540; text: HomeTheme.t("按最长音频自动适配"); color: StudioTheme.muted; font.pixelSize: 10 }
        StudioButton {
            objectName: "collapseAudioRanges"; implicitWidth: 26; implicitHeight: 26; quiet: true; iconName: "chevron"
            rotation: root.collapsed ? 180 : 0
            hint: root.collapsed ? HomeTheme.t("展开音频范围") : HomeTheme.t("收起音频范围")
            onClicked: root.collapsed = !root.collapsed
        }
    }
    Item {
        id: ranges
        x: 12; y: root.toolbarHeight; width: root.width - 24; height: 76; visible: !root.collapsed
        Repeater {
            model: ["speech", "sfx", "background"]
            delegate: Item {
                id: row
                required property string modelData
                required property int index
                readonly property var item: root.track(modelData)
                readonly property bool background: modelData === "background"
                readonly property bool hasSource: !!item.source
                readonly property string label: modelData === "speech" ? HomeTheme.t("配音 / 数字人") : modelData === "sfx" ? HomeTheme.t("音效") : HomeTheme.t("背景")
                readonly property color tint: modelData === "speech" ? StudioTheme.accent : modelData === "sfx" ? StudioTheme.warning : StudioTheme.secondary
                x: 0; y: index * root.rowHeight; width: ranges.width; height: background ? 20 : root.rowHeight
                StudioButton {
                    objectName: "editTrack-" + row.modelData; x: 19; y: 0; width: root.labelWidth - 19; height: row.height
                    text: row.label; quiet: true; active: !!root.timeline && root.timeline.selectedTrack === row.modelData
                    hint: HomeTheme.t("编辑") + row.label; onClicked: root.requestEdit(row.modelData)
                }
                Item {
                    id: muteControl
                    objectName: "toggleTrack-" + row.modelData; width: 18; height: row.height
                    readonly property string hint: (row.item.enabled ? row.background ? HomeTheme.t("隐藏") : HomeTheme.t("静音") : row.background ? HomeTheme.t("显示") : HomeTheme.t("启用")) + row.label
                    Accessible.role: Accessible.Button; Accessible.name: hint; activeFocusOnTab: root.editingEnabled
                    Accessible.onPressAction: root.toggleTrack(row.modelData)
                    Keys.onReturnPressed: root.toggleTrack(row.modelData)
                    Keys.onSpacePressed: root.toggleTrack(row.modelData)
                    StudioIcon { anchors.centerIn: parent; width: 14; height: 14; name: row.background ? "layers" : row.modelData === "speech" ? "person" : "audio"; ink: row.item.enabled ? row.tint : StudioTheme.faint }
                    Rectangle { anchors.centerIn: parent; width: 17; height: 1; rotation: -45; visible: !row.item.enabled; color: StudioTheme.muted }
                    Rectangle { anchors.fill: parent; anchors.margins: 1; radius: 3; color: "transparent"; border.color: muteControl.activeFocus ? StudioTheme.accent : "transparent" }
                    MouseArea { id: mutePointer; anchors.fill: parent; hoverEnabled: true; enabled: root.editingEnabled; cursorShape: Qt.PointingHandCursor; onClicked: root.toggleTrack(row.modelData) }
                    ToolTip.visible: mutePointer.containsMouse; ToolTip.text: hint; ToolTip.delay: 500
                }
                Rectangle {
                    id: lane
                    objectName: "audioRangeLane-" + row.modelData
                    x: root.labelWidth; y: row.background ? 5 : 4; width: root.laneWidth; height: row.background ? 10 : 20; radius: 3
                    color: StudioTheme.elevated; clip: true
                    Rectangle {
                        id: clipBlock
                        objectName: "timelineClip-" + row.modelData; visible: row.hasSource || row.background
                        x: row.background ? 0 : root.startFor(row.item) * root.pixelsPerSecond; y: 2
                        width: row.background ? lane.width : Math.max(3, root.durationFor(row.item) * root.pixelsPerSecond)
                        height: lane.height - 4; radius: 2
                        color: Qt.alpha(row.tint, StudioTheme.dark ? .30 : .18)
                        opacity: row.item.enabled ? 1 : 0.4
                        border.color: root.timeline && root.timeline.selectedTrack === row.modelData ? StudioTheme.accent : "transparent"
                        clip: true
                        Text {
                            anchors.fill: parent; anchors.leftMargin: 9; anchors.rightMargin: 9; visible: !row.background
                            text: (row.item.kind === "video" ? HomeTheme.t("数字人视频") : row.modelData === "speech" ? HomeTheme.t("配音") : HomeTheme.t("音效")) + " · " + root.durationFor(row.item).toFixed(1) + "s"
                            color: StudioTheme.text; font.pixelSize: 10
                            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                        }
                        Rectangle { visible: !row.background; x: 2; y: 3; width: 4; height: parent.height - 6; radius: 1; color: row.tint }
                        Rectangle { visible: !row.background; anchors.right: parent.right; anchors.rightMargin: 2; y: 3; width: 4; height: parent.height - 6; radius: 1; color: row.tint }
                    }
                    Text { anchors.centerIn: parent; visible: !row.hasSource && !row.background; text: row.modelData === "speech" ? HomeTheme.t("+ 添加配音 / 数字人") : HomeTheme.t("+ 添加音效"); color: StudioTheme.muted; font.pixelSize: 10 }
                    MouseArea {
                        id: rangePointer
                        objectName: "rangeGesture-" + row.modelData
                        anchors.fill: parent; hoverEnabled: true; preventStealing: true
                        property bool seeking: false
                        cursorShape: row.background || !row.hasSource ? Qt.PointingHandCursor : root.dragKey === row.modelData ? root.dragMode === "move" ? Qt.ClosedHandCursor : Qt.SizeHorCursor : Qt.OpenHandCursor
                        function seek(x) { if (root.timeline) root.timeline.playhead = Math.max(0, Math.min(root.duration, x / root.pixelsPerSecond)) }
                        onPressed: mouse => {
                            root.forceActiveFocus(); root.requestEdit(row.modelData); seeking = false
                            if (row.background || !row.hasSource || !root.editingEnabled) return
                            const left = row.item.start * root.pixelsPerSecond, right = (row.item.start + row.item.duration) * root.pixelsPerSecond
                            if (mouse.x < left || mouse.x > right) { seeking = true; seek(mouse.x); return }
                            root.gestureScale = root.pixelsPerSecond; root.pressX = mouse.x
                            root.originalStart = Number(row.item.start); root.originalDuration = Number(row.item.duration)
                            root.pendingStart = root.originalStart; root.pendingDuration = root.originalDuration; root.dragChanged = false
                            root.dragMode = mouse.x - left < 7 ? "left" : right - mouse.x < 7 ? "right" : "move"; root.dragKey = row.modelData
                        }
                        onPositionChanged: mouse => {
                            if (!pressed) return
                            if (seeking) { seek(mouse.x); return }
                            if (root.dragKey !== row.modelData) return
                            const delta = Math.round((mouse.x - root.pressX) / root.gestureScale * 10) / 10
                            root.dragChanged = root.dragChanged || Math.abs(mouse.x - root.pressX) > 2
                            if (root.dragMode === "move") root.pendingStart = Math.max(0, Math.min(600 - root.originalDuration, root.originalStart + delta))
                            else if (root.dragMode === "left") {
                                root.pendingStart = Math.max(0, Math.min(root.originalStart + root.originalDuration - 0.1, root.originalStart + delta))
                                root.pendingDuration = root.originalStart + root.originalDuration - root.pendingStart
                            } else root.pendingDuration = Math.max(0.1, Math.min(600 - root.originalStart, root.originalDuration + delta))
                        }
                        onReleased: mouse => { if (root.dragKey === row.modelData && !root.dragChanged) seek(mouse.x); root.finishDrag(true); seeking = false }
                        onCanceled: { root.finishDrag(false); seeking = false }
                        ToolTip.visible: containsMouse && !pressed && row.hasSource && !row.background
                        ToolTip.text: HomeTheme.t("拖动移动 · 两端调整时长 · Alt + 方向键微调"); ToolTip.delay: 800
                    }
                }
                StudioButton {
                    objectName: "exportTrack-" + row.modelData; x: root.labelWidth + root.laneWidth + 6; y: 0; width: 26; height: row.height
                    quiet: true; iconName: "export"; enabled: row.hasSource && root.editingEnabled
                    hint: HomeTheme.t("导出") + row.label; onClicked: root.exportRequested(row.modelData)
                }
            }
        }
        Rectangle {
            objectName: "audioRangePlayhead"
            x: root.labelWidth + Math.min(root.laneWidth, (root.timeline ? root.timeline.playhead : 0) * root.pixelsPerSecond)
            y: 3; width: 1; height: 72; color: StudioTheme.accent
        }
    }
}
