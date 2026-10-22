import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    id: root
    objectName: "creationWorkspace"
    property bool audiovisual: false
    property string imageContext: "canvas"
    property string videoContext: "speech"
    property bool imageInspectorOpen: false
    signal modeRequested(string mode)
    onAudiovisualChanged: {
        suspendPreview()
        if(audiovisual) {
            imageContext = inspectorContext; imageInspectorOpen = inspectorOpen
            studio.repainting = false; layoutEditing = false; studio.layerDocument.selected = -1
            inspectorContext = videoContext; inspectorOpen = true
            studio.timeline.select(videoContext === "voice" ? "speech" : videoContext)
        } else {
            videoContext = inspectorContext
            inspectorContext = imageContext; inspectorOpen = imageInspectorOpen
            studio.timeline.select("image")
        }
    }
    property var doc: studio.document
    property var selected: doc.selected >= 0 && doc.selected < doc.regions.length ? doc.regions[doc.selected] : null
    property bool overlay: true
    property bool showOutput: true
    property bool advanced: false
    property bool layoutEditing: false
    property bool pagesExpanded: true
    property bool inspectorOpen: false
    readonly property int floatingFooterHeight: audiovisual ? 148 : 88
    property string inspectorContext: "canvas"
    property int observedLayer: -1
    function suspendPreview() { trackPreview.playing = false }
    function closeInspector() {
        inspectorOpen = false
        if(inspectorContext === "object")studio.layerDocument.selected = -1
        if(inspectorContext === "region")doc.selected = -1
        studio.timeline.select("image")
        root.forceActiveFocus()
    }
    function toggleInspector(context) {
        root.forceActiveFocus()
        if (inspectorOpen && inspectorContext === context) closeInspector()
        else openInspector(context)
    }
    function togglePreview() {
        if(!audiovisual)return
        root.forceActiveFocus()
        layoutEditing = false
        studio.repainting = false
        closeInspector()
        trackPreview.togglePlayback()
    }
    function seekPreview(value) {
        layoutEditing = false
        studio.repainting = false
        trackPreview.playing = false
        studio.timeline.playhead = value
    }
    function stamp(value) {
        const seconds = Math.floor(Math.max(0, value))
        return String(Math.floor(seconds / 60)).padStart(2, "0") + ":" + String(seconds % 60).padStart(2, "0")
    }
    function openInspector(context) {
        const audioContext = ["speech","voice","sfx","background"].includes(context)
        if(audioContext !== audiovisual)modeRequested(audioContext ? "av" : "image")
        trackPreview.playing = false
        if (audioContext) {
            studio.timeline.select(context === "voice" ? "speech" : context); layoutEditing = false; studio.repainting = false
            studio.layerDocument.selected = -1
        } else {
            studio.timeline.select("image")
            if (context === "repaint") { layoutEditing = false; studio.repainting = true }
            else if(context !== "region") { studio.repainting = false; if(context === "object")layoutEditing = false }
        }
        inspectorContext = context
        inspectorOpen = true
    }
    function undoCanvas() {
        root.forceActiveFocus()
        if(studio.repainting)studio.editDocument.undo()
        else if(layoutEditing)doc.undo()
        else studio.layerDocument.undo()
    }
    readonly property string currentTrack: studio.timeline.selectedTrack
    readonly property var track: studio.timeline.selected
    property string exportKey: "image"
    function refreshPage() {
        trackPreview.playing = false
        canvas.drawMode = false
        showOutput = true
        observedLayer = studio.layerDocument.selected
        observedSelection = doc.selected
        layoutEditing = !audiovisual && inspectorOpen && inspectorContext === "region" && doc.layout
        inspectorTab = layoutEditing ? 1 : 0
        if (inspectorOpen) {
            if (inspectorContext === "region" && !doc.layout) inspectorContext = "canvas"
            openInspector(inspectorContext)
        } else studio.repainting = false
    }
    onVisibleChanged: if (!visible) trackPreview.playing = false
    onCurrentTrackChanged: {
        if (studio.switchingPage) return
        layoutEditing = false
        if (currentTrack !== "image") studio.repainting = false
    }
    function exportAsset(key) { if(key === "image"){canvasExport.open();return} exportKey = key; trackExport.open() }
    FileDialog { id: canvasExport; title: HomeTheme.t("导出画布"); fileMode: FileDialog.SaveFile; nameFilters: [HomeTheme.t("PNG 图片 (*.png)")]; onAccepted: studio.exportCanvas(selectedFile) }
    FileDialog {
        id: objectImport; title: HomeTheme.t("导入画布对象"); fileMode: FileDialog.OpenFiles
        nameFilters: [HomeTheme.t("图像与矢量 (*.png *.jpg *.jpeg *.webp *.svg)")]
        onAccepted: { studio.layerDocument.importFiles(selectedFiles); root.openInspector("object") }
    }
    FileDialog {
        id: trackExport; title: HomeTheme.t("导出原始素材"); fileMode: FileDialog.SaveFile
        nameFilters: { const row = studio.timeline.track(root.exportKey); const ext = String(row.source || "").split(".").pop().toLowerCase(); return [HomeTheme.t("素材 (*.") + (ext || "png") + ")"] }
        onAccepted: studio.exportTrack(root.exportKey, selectedFile)
    }
    FileDialog { id: imageImport; title: HomeTheme.t("导入画面"); nameFilters: [HomeTheme.t("图片 (*.png *.jpg *.jpeg *.webp)")]; onAccepted: studio.importTrackImage("image",selectedFile) }
    Shortcut { sequence: "Space"; enabled: root.visible && root.audiovisual && !root.textFocused() && !studio.busy; onActivated: root.togglePreview() }
    readonly property bool hasSound: studio.sound.settings.voiceSource !== "none" || studio.sound.settings.sfxEnabled
    property int inspectorTab: 0
    property int observedSelection: -1
    Connections {
        target: doc
        function onSelectionChanged() {
            if (studio.switchingPage) return
            if (doc.selected === root.observedSelection) return
            root.observedSelection = doc.selected
            if (doc.selected >= 0 && doc.layout && root.layoutEditing) { root.inspectorTab = 1; root.openInspector("region") }
        }
    }
    Connections {
        target: studio.layerDocument
        function onChanged() {
            if (studio.switchingPage) return
            const selected = studio.layerDocument.selected
            if(selected === root.observedLayer)return
            root.observedLayer = selected
            if(selected >= 0 && !root.audiovisual && !root.layoutEditing && !studio.repainting)root.openInspector("object")
        }
    }
    Connections {
        target: studio
        function onPagesChanged() { if (!doc.layout) root.inspectorTab = 0 }
        function onPageActivated() { root.refreshPage() }
    }
    property string layoutError: ""
    signal navigate(int page)
    // Text controls own Undo/Redo while typing, including region text and JSON.
    function textFocused() {
        const f = root.Window.window.activeFocusItem
        return f && (f.hasOwnProperty("cursorPosition") || f.hasOwnProperty("inputMethodComposing"))
    }
    Shortcut { sequence: "Escape"; enabled: root.visible && root.inspectorOpen; onActivated: root.closeInspector() }
    Shortcut { sequences: [StandardKey.Undo]; enabled: root.visible && !root.audiovisual && !root.textFocused() && !studio.busy; onActivated: root.undoCanvas() }
    Shortcut { sequences: [StandardKey.Redo]; enabled: root.visible && !root.audiovisual && !root.textFocused() && !studio.busy; onActivated: studio.repainting ? studio.editDocument.redo() : root.layoutEditing ? doc.redo() : studio.layerDocument.redo() }
    function templateFor(name) {
        root.forceActiveFocus()
        doc.loadTemplate(name)
        showOutput = false; layoutEditing = true
    }
    FileDialog {
        id: layoutFile; title: HomeTheme.t("导入 MOSS Layout JSON"); nameFilters: ["Layout JSON (*.json)"]
        onAccepted: { const text = studio.readText(selectedFile); if (text !== "") { layoutDraft.value = text; layoutDialog.open() } }
    }
    FileDialog {
        id: layoutSave; title: HomeTheme.t("导出 MOSS Layout JSON"); fileMode: FileDialog.SaveFile; nameFilters: ["Layout JSON (*.json)"]
        onAccepted: studio.saveText(selectedFile, doc.inputPreview)
    }
    HomeDialog {
        id: longDialog; title: HomeTheme.t("画面描述 · 长文编辑"); width: Math.min(root.width - 40, 900); height: Math.min(root.height - 30, 690)
        anchors.centerIn: parent; modal: true; standardButtons: Dialog.Close
        ColumnLayout {
            anchors.fill: parent; spacing: 12
            Text { Layout.fillWidth: true; text: HomeTheme.t("写明主体、动作、环境、光线与构图。支持中文、英文、Tag 串和手动换行。"); wrapMode: Text.Wrap; color: StudioTheme.secondary }
            InputArea { Layout.fillWidth: true; Layout.fillHeight: true; objectName: "longPrompt"; label: HomeTheme.t("完整画面描述"); value: doc.prompt; onEdited: text => doc.prompt = text }
            Text { text: doc.prompt.length + HomeTheme.t(" 字符 · 模型上限 4K tokens，字符数不等于 token 数"); color: StudioTheme.muted; font.pixelSize: 11 }
        }
    }
    HomeDialog {
        id: previewDialog; title: HomeTheme.t("实际生成输入"); width: Math.min(root.width - 40, 860); height: Math.min(root.height - 30, 680)
        anchors.centerIn: parent; modal: true; standardButtons: Dialog.Close
        ColumnLayout {
            anchors.fill: parent; spacing: 12
            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: doc.layout ? HomeTheme.t("框坐标会转换为 MOSS 的中心坐标。下面的内容将直接提交给模型。") : HomeTheme.t("画面描述与视觉风格会组合成下面的提示词。"); color: StudioTheme.secondary }
            InputArea { Layout.fillWidth: true; Layout.fillHeight: true; label: HomeTheme.t("实际提示词"); readOnly: true; value: doc.inputPreview }
            RowLayout {
                Layout.fillWidth: true
                Text { Layout.fillWidth: true; text: doc.inputPreview.length + HomeTheme.t(" 字符（含布局信息）"); color: StudioTheme.muted; font.pixelSize: 11 }
                StudioButton { text: HomeTheme.t("复制"); onClicked: studio.copyText(doc.inputPreview) }
                StudioButton { text: HomeTheme.t("导出 JSON"); visible: doc.layout; onClicked: layoutSave.open() }
            }
        }
    }
    HomeDialog {
        id: layoutDialog; title: HomeTheme.t("导入 / 编辑 MOSS Layout"); width: Math.min(root.width - 40, 860); height: Math.min(root.height - 30, 700)
        anchors.centerIn: parent; modal: true; standardButtons: Dialog.Cancel
        onOpened: { root.layoutError = ""; importW.value = doc.canvasWidth; importH.value = doc.canvasHeight }
        ColumnLayout {
            anchors.fill: parent; spacing: 10
            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: HomeTheme.t("粘贴 Prompt Enhancer 或外部工具生成的 Layout JSON。尺寸必须与该布局一致；应用将替换当前画面输入，可撤销。"); color: StudioTheme.secondary; font.pixelSize: 12 }
            RowLayout {
                Text { text: HomeTheme.t("原布局尺寸"); color: StudioTheme.secondary }
                StudioSpinBox { id: importW; from: 256; to: 4096; stepSize: 16; editable: true }
                Text { text: "×"; color: StudioTheme.secondary }
                StudioSpinBox { id: importH; from: 256; to: 4096; stepSize: 16; editable: true }
                Item { Layout.fillWidth: true }
                StudioButton { text: HomeTheme.t("读取文件"); onClicked: layoutFile.open() }
            }
            InputArea { id: layoutDraft; objectName: "layoutJson"; Layout.fillWidth: true; Layout.fillHeight: true; label: "Layout JSON"; onEdited: text => value = text; placeholder: HomeTheme.t("包含 summary、items、bbox_coord_space、bbox_format 的 JSON") }
            Text { visible: root.layoutError !== ""; Layout.fillWidth: true; wrapMode: Text.Wrap; text: root.layoutError; color: "#B56A4A" }
            StudioButton {
                Layout.alignment: Qt.AlignRight; primary: true; text: HomeTheme.t("应用到画布"); enabled: !studio.busy
                onClicked: {
                    root.layoutError = doc.importLayout(layoutDraft.value, importW.value, importH.value)
                    if (root.layoutError === "") { doc.setCanvasSize(doc.canvasWidth,doc.canvasWidth); root.layoutEditing = true; studio.repainting = false; studio.timeline.select("image"); root.showOutput = true; layoutDialog.close(); root.closeInspector() }
                }
            }
        }
    }
    Item {
        anchors.fill: parent
        Item {
            anchors.fill: parent
            CreationPages {
                id: pageRail
                backdropSource: canvasSurface
                objectName: "creationPageRail"
                visible: root.pagesExpanded
                x: 14; y: 76; width: 136
                height: Math.min(parent.height - y - root.floatingFooterHeight, implicitHeight)
                z: 10
                onSelected: { root.showOutput = true; canvas.drawMode = false }
            }
            Item {
                id: stageHost
                anchors.fill: parent
                readonly property bool compactToolbar: width < 1200
                Rectangle { anchors.fill: parent; color: StudioTheme.background }
                ColumnLayout {
                    anchors.fill: parent; spacing: 0
                    FrostedPanel {
                        objectName: "creationToolbar"
                        Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredHeight: 48
                        Layout.leftMargin: 14; Layout.rightMargin: 14; Layout.topMargin: 12; Layout.bottomMargin: 8
                        backdropSource: canvasSurface
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 4
                            StudioButton { objectName: "toggleCreationPages"; iconName: "sidebar"; quiet: true; active: root.pagesExpanded; hint: root.pagesExpanded ? HomeTheme.t("收起创作页") : HomeTheme.t("展开创作页"); onClicked: root.pagesExpanded = !root.pagesExpanded }
                            Rectangle { width: 1; height: 20; color: StudioTheme.line; Layout.leftMargin: 3; Layout.rightMargin: 3 }
                            StudioButton {
                                visible: !root.audiovisual; text: HomeTheme.t("导入"); iconName: "plus"; quiet: true; enabled: !studio.busy
                                onClicked: importMenu.popup()
                                Menu {
                                    id: importMenu
                                    MenuItem { text: HomeTheme.t("图片 / SVG 对象…"); onTriggered: objectImport.open() }
                                    MenuItem { text: HomeTheme.t("Layout 矩形框…"); onTriggered: layoutFile.open() }
                                    MenuSeparator { }
                                    MenuItem { text: HomeTheme.t("替换画布底图…"); onTriggered: imageImport.open() }
                                }
                            }
                            StudioButton { objectName: "layoutMode"; visible: !root.audiovisual; text: HomeTheme.t("布局"); hint: HomeTheme.t("Layout · 规划文字与对象区域"); iconName: "rectangle"; quiet: true; active: root.layoutEditing; enabled: !studio.busy; onClicked: { studio.repainting = false; root.closeInspector(); root.layoutEditing = !root.layoutEditing; if(root.layoutEditing)doc.layout = true } }
                            StudioButton { objectName: "repaintMode"; visible: !root.audiovisual; text: studio.repainting ? HomeTheme.t("完成重绘") : stageHost.compactToolbar ? HomeTheme.t("重绘") : HomeTheme.t("局部重绘"); hint: HomeTheme.t("局部重绘"); iconName: "brush"; quiet: true; active: studio.repainting; enabled: !studio.busy; onClicked: { if(studio.repainting){studio.repainting=false;root.closeInspector()}else root.openInspector("repaint") } }
                            StudioButton { objectName: "canvasObjects"; visible: !root.audiovisual; iconName: "layers"; text: HomeTheme.t("对象"); quiet: true; hint: HomeTheme.t("画布对象"); active: root.inspectorOpen && root.inspectorContext === "object"; onClicked: root.toggleInspector("object") }
                            Rectangle { width: 1; height: 20; color: StudioTheme.line; Layout.leftMargin: 4; Layout.rightMargin: 4 }
                            StudioButton { objectName: "speechSettings"; visible: root.audiovisual; text: HomeTheme.t("数字人"); iconName: "person"; implicitHeight: 32; quiet: true; active: root.inspectorOpen && root.inspectorContext === "speech"; onClicked: root.toggleInspector("speech") }
                            StudioButton { objectName: "voiceSettings"; visible: root.audiovisual; text: HomeTheme.t("语音合成"); iconName: "volume"; quiet: true; active: root.inspectorOpen && root.inspectorContext === "voice"; onClicked: root.toggleInspector("voice") }
                            StudioButton { objectName: "sfxSettings"; visible: root.audiovisual; text: HomeTheme.t("音效"); iconName: "audio"; implicitHeight: 32; quiet: true; active: root.inspectorOpen && root.inspectorContext === "sfx"; onClicked: root.toggleInspector("sfx") }
                            StudioButton { objectName: "backgroundSettings"; visible: root.audiovisual; text: HomeTheme.t("背景"); iconName: "background"; implicitHeight: 32; quiet: true; active: root.inspectorOpen && root.inspectorContext === "background"; onClicked: root.toggleInspector("background") }
                            Item { Layout.fillWidth: true }
                            StudioButton { visible: root.audiovisual; text: HomeTheme.t("编辑合成图像"); iconName: "image"; quiet: true; onClicked: root.modeRequested("image") }
                            Rectangle { width: 1; height: 20; color: StudioTheme.line; Layout.leftMargin: 4; Layout.rightMargin: 4 }
                            RowLayout {
                                objectName: "toolbarPlayback"; visible: root.audiovisual
                                spacing: 6
                                StudioButton {
                                    objectName: "previewPlayback"; quiet: true; iconName: trackPreview.playing ? "pause" : "play"
                                    hint: trackPreview.playing ? HomeTheme.t("暂停预览 · 空格") : HomeTheme.t("播放预览 · 空格"); enabled: !studio.busy
                                    active: trackPreview.playing
                                    onClicked: root.togglePreview()
                                }
                                Text { text: root.stamp(studio.timeline.playhead); font.family: "Menlo"; font.pixelSize: 10; color: StudioTheme.secondary }
                                StudioSlider {
                                    objectName: "previewSeek"; Layout.preferredWidth: stageHost.compactToolbar ? 64 : 108
                                    Accessible.name: HomeTheme.t("预览进度"); from: 0; to: studio.timeline.duration; value: studio.timeline.playhead
                                    enabled: !studio.busy
                                    onPressedChanged: if (pressed) root.seekPreview(value)
                                    onMoved: root.seekPreview(value)
                                }
                                Text { text: root.stamp(studio.timeline.duration); font.family: "Menlo"; font.pixelSize: 10; color: StudioTheme.muted }
                            }
                            Rectangle { width: 1; height: 20; color: StudioTheme.line; Layout.leftMargin: 4; Layout.rightMargin: 4 }
                            StudioButton { visible: !root.audiovisual; iconName: "undo"; quiet: true; hint: HomeTheme.t("撤销画布编辑"); enabled: !studio.busy && (studio.repainting ? studio.editDocument.canUndo : root.layoutEditing ? doc.canUndo : studio.layerDocument.canUndo); onClicked: root.undoCanvas() }
                            StudioButton { objectName: "canvasSettings"; visible: !root.audiovisual; text: stageHost.compactToolbar ? "" : HomeTheme.t("画面设置"); iconName: "settings"; quiet: true; hint: HomeTheme.t("画布尺寸、风格与生成参数"); active: root.inspectorOpen && root.inspectorContext === "canvas"; onClicked: root.toggleInspector("canvas") }
                        }
                    }
                    ColumnLayout {
                        id: canvasWorkspace
                        objectName: "canvasWorkspace"
                        Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumWidth: 0
                        // Floating controls never resize or recenter the canvas.
                        // Keep the footer space even when its composer is hidden.
                        Layout.bottomMargin: root.floatingFooterHeight
                        spacing: 0
                        Item {
                            Layout.fillWidth: true; Layout.preferredHeight: 56
                            FrostedPanel {
                                objectName: "canvasContextToolbar"
                                // Stable side clearances keep this row accessible with either floating panel open.
                                x: pageRail.x + pageRail.width + 12; y: 8
                                width: parent.width - x - inspectorPanel.width - 28; height: 40
                                backdropSource: canvasSurface; radius: 10
                                RowLayout {
                                    anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 8; spacing: 8
                                    Text { text: (root.audiovisual ? HomeTheme.t("共享合成画面 ") : HomeTheme.t("画布 ")) + String(studio.currentPage + 1).padStart(2, "0"); color: StudioTheme.secondary; font.pixelSize: 11 }
                                    Text { text: "·"; color: StudioTheme.faint }
                                    Text { text: doc.canvasWidth + " × " + doc.canvasHeight; color: StudioTheme.muted; font.pixelSize: 11 }
                                    Rectangle { visible: studio.repainting; width: 1; height: 18; color: StudioTheme.line }
                                    Item { id: repaintToolsHost; visible: studio.repainting; Layout.fillWidth: true; Layout.fillHeight: true }
                                    Item { visible: !studio.repainting; Layout.fillWidth: true }
                                    Text { visible: !studio.repainting; text: root.layoutEditing ? HomeTheme.t("布局编辑") : Math.round(trackPreview.canvasSize / doc.canvasWidth * 100) + HomeTheme.t("% · 适应"); color: StudioTheme.muted; font.pixelSize: 10 }
                                }
                            }
                        }
                        Item {
                            id: canvasViewport
                            objectName: "canvasViewport"
                            Layout.fillWidth: true; Layout.fillHeight: true
                            Item {
                                id: canvasSurface
                                objectName: "canvasSurface"
                                anchors.fill: parent
                                TrackPreview {
                                    id: trackPreview; anchors.fill: parent; audiovisual: root.audiovisual
                                    fixedImage: root.audiovisual ? studio.canvasCompositeUrl : studio.pageVisualUrl
                                     visible: !studio.repainting && !root.layoutEditing
                                    onImportRequested: if(root.audiovisual)root.modeRequested("image"); else imageImport.open()
                                    onEditRequested: key => root.openInspector(key)
                                    onCanvasClicked: root.closeInspector()
                                }
                                LayoutCanvas { id: canvas; anchors.fill: parent; visible: root.layoutEditing && !studio.repainting; enabled: !studio.busy; document: doc; imageSource: studio.pageVisualUrl; showRegions: root.overlay }
                                RepaintCanvas { anchors.fill: parent; visible: studio.repainting; toolbarHost: repaintToolsHost; footerHost: stageHost; floatingHost: canvasViewport; backdropSource: canvasSurface }
                            }
                            DropArea {
                                anchors.fill: parent; enabled: !studio.busy && !root.audiovisual
                                onDropped: drop => { if(drop.hasUrls){studio.layerDocument.importFiles(drop.urls);root.openInspector("object");drop.acceptProposedAction()} }
                            }
                            FrostedPanel {
                                objectName: "layoutTools"
                                visible: root.layoutEditing && !studio.repainting
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.bottom: parent.bottom; anchors.bottomMargin: 8
                                width: layoutToolsRow.implicitWidth + 12; height: layoutToolsRow.implicitHeight + 12
                                backdropSource: canvasSurface; radius: 10; z: 1
                                RowLayout {
                                    id: layoutToolsRow
                                    anchors.centerIn: parent; spacing: 5
                                    StudioButton { text: HomeTheme.t("选择"); iconName: "select"; quiet: true; active: !canvas.drawMode; onClicked: canvas.drawMode = false }
                                    StudioButton { objectName: "drawObject"; text: HomeTheme.t("对象框"); iconName: "plus"; active: canvas.drawMode && canvas.drawType === "object"; onClicked: {canvas.drawMode=true;canvas.drawType="object"} }
                                    StudioButton { objectName: "drawText"; text: HomeTheme.t("文字框"); iconName: "text"; active: canvas.drawMode && canvas.drawType === "text"; onClicked: {canvas.drawMode=true;canvas.drawType="text"} }
                                    StudioButton { text: HomeTheme.t("导入 Layout"); quiet: true; onClicked: layoutFile.open() }
                                    StudioButton { text: root.overlay ? HomeTheme.t("隐藏框") : HomeTheme.t("显示框"); quiet: true; onClicked: root.overlay = !root.overlay }
                                }
                            }
                        }
                    }
                }
                FrostedPanel {
                    id: promptCard
                    objectName: "promptComposer"
                    visible: !root.audiovisual && !studio.repainting
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 12
                    width: Math.min(parent.width - 40, 820); height: 64
                    z: 20
                    backdropSource: canvasSurface
                    border.color: promptInput.editing ? StudioTheme.accent : StudioTheme.glassLine
                    RowLayout {
                        anchors.fill: parent; anchors.margins: 10; spacing: 8
                        InputArea {
                            id: promptInput
                            objectName: "canvasPrompt"; Layout.fillWidth: true; Layout.fillHeight: true
                            color: "transparent"; border.width: 0; label: HomeTheme.t("画面描述"); value: doc.prompt
                            editor.padding: 0; editor.topPadding: 4; editor.font.pixelSize: 13
                            placeholder: HomeTheme.t("描述你想生成的画面…"); onEdited: text => doc.prompt = text; enabled: !studio.busy
                        }
                        StudioButton { visible: canvasWorkspace.width > 650; iconName: "model"; hint: (studio.imageDevice === "remote" ? HomeTheme.t("远程 GPU") : HomeTheme.t("本机生成")) + " · " + studio.steps + HomeTheme.t(" 步 · 点击设置"); quiet: true; onClicked: root.toggleInspector("canvas") }
                        StudioButton { iconName: "expand"; hint: HomeTheme.t("展开画面描述"); quiet: true; onClicked: longDialog.open() }
                        Rectangle { width: 1; height: 24; color: StudioTheme.line }
                        StudioButton { objectName: "generateCanvas"; iconName: "spark"; text: studio.busy ? HomeTheme.t("取消") : HomeTheme.t("生成"); primary: true; implicitHeight: 38; leftPadding: 16; rightPadding: 16; onClicked: {root.forceActiveFocus();trackPreview.playing=false;if(studio.busy)studio.cancel();else studio.generateTrack("image")} }
                    }
                }
                TrackTimeline {
                    id: avTimeline; visible: root.audiovisual
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    anchors.leftMargin: 14; anchors.rightMargin: 14; anchors.bottomMargin: 10
                    height: 128; playing: trackPreview.playing; z: 21
                    onTogglePlayback: root.togglePreview()
                    onEditRequested: key => root.openInspector(key)
                    onExportRequested: key => root.exportAsset(key)
                }
                FrostedPanel {
                    id: inspectorPanel
                    objectName: "canvasInspector"
                    visible: root.inspectorOpen
                    anchors.right: parent.right; anchors.rightMargin: 14; anchors.top: parent.top; anchors.topMargin: 76; anchors.bottom: parent.bottom; anchors.bottomMargin: root.floatingFooterHeight
                    width: 304
                    z: 20; backdropSource: canvasSurface; tintOpacity: 0.84
                    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: wheel => wheel.accepted = true }
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 1; anchors.bottomMargin: 10; spacing: 0
                        RowLayout {
                            Layout.fillWidth: true; Layout.preferredHeight: 48; Layout.leftMargin: 16; Layout.rightMargin: 10
                            Text { Layout.fillWidth: true; text: ({canvas:HomeTheme.t("画面生成"),region:HomeTheme.t("区域属性"),object:HomeTheme.t("画布对象"),repaint:HomeTheme.t("局部重绘"),speech:HomeTheme.t("数字人"),voice:HomeTheme.t("语音合成"),sfx:HomeTheme.t("音效生成"),background:HomeTheme.t("背景")})[root.inspectorContext] || HomeTheme.t("画布"); color: StudioTheme.text; font.pixelSize: 12; font.weight: Font.DemiBold }
                            StudioButton { iconName: "export"; quiet: true; hint: HomeTheme.t("导出当前内容"); visible: root.inspectorContext !== "object" && root.inspectorContext !== "region"; enabled: !studio.busy && !!root.track.source; onClicked: root.exportAsset(root.currentTrack) }
                            StudioButton { objectName: "closeCanvasInspector"; iconName: "close"; implicitWidth: 26; quiet: true; hint: HomeTheme.t("收起设置"); onClicked: root.closeInspector() }
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: StudioTheme.line }
                        StackLayout {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            currentIndex: ({canvas:0,region:1,repaint:2,speech:3,sfx:4,background:5,object:6,voice:7})[root.inspectorContext] || 0
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true; color: "transparent"
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: StudioTheme.line }
            ScrollView {
                anchors.fill: parent; anchors.margins: 12; anchors.topMargin: 10; anchors.bottomMargin: 10; contentWidth: availableWidth; clip: true
                ColumnLayout {
                    width: parent.width; spacing: 12; enabled: !studio.busy
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        StudioButton { Layout.fillWidth: true; text: HomeTheme.t("导入画面"); iconName: "folder"; onClicked: imageImport.open() }
                        StudioButton { Layout.fillWidth: true; text: HomeTheme.t("生成图"); visible: studio.creationImageUrl !== ""; active: studio.pageVisualUrl === studio.creationImageUrl; onClicked: studio.usePageVisual(false) }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: HomeTheme.t("画面描述"); font.weight: Font.DemiBold; font.pixelSize: 14; color: StudioTheme.text }
                        Item { Layout.fillWidth: true }
                        StudioButton { iconName: "expand"; hint: HomeTheme.t("展开描述编辑器"); quiet: true; implicitHeight: 26; onClicked: longDialog.open() }
                    }
                    InputArea { Layout.fillWidth: true; Layout.preferredHeight: 120; objectName: "promptEditor"; label: HomeTheme.t("画面描述"); value: doc.prompt; placeholder: HomeTheme.t("想画什么？\n描述主体、环境、动作、光线和整体构图…"); onEdited: text => doc.prompt = text }
                    Text { Layout.fillWidth: true; text: doc.prompt.length + HomeTheme.t(" 字符 · 支持多行与长提示词"); color: StudioTheme.muted; font.pixelSize: 11 }
                    Text { Layout.fillWidth: true; visible: doc.inputPreview.length > 6000; wrapMode: Text.Wrap; text: HomeTheme.t("输入较长：模型最多接受 4K tokens（含布局）。超过上限可能被引擎截断，请精简重复描述。"); color: "#A77949"; font.pixelSize: 11 }
                    Text { text: HomeTheme.t("视觉风格"); font.pixelSize: 11; color: StudioTheme.secondary }
                    StudioCombo {
                        id: styleBox; Layout.fillWidth: true
                        property var values: ["", "Photorealistic photography, natural lighting, realistic materials.", "Clean editorial poster, deliberate typography, generous whitespace.", "Anime illustration, expressive character design, detailed linework.", "Soft watercolor illustration, textured paper, delicate brushwork.", "Minimalist 3D render, soft studio lighting, tactile materials."]
                        model: [HomeTheme.t("由描述决定"), HomeTheme.t("摄影写实"), HomeTheme.t("平面海报"), HomeTheme.t("动漫插画"), HomeTheme.t("水彩手绘"), HomeTheme.t("3D 产品")]
                        currentIndex: values.indexOf(doc.style)
                        onActivated: doc.style = values[currentIndex]
                    }
                    Text { Layout.fillWidth: true; visible: doc.layout; text: HomeTheme.t("文字原文与对象细节在右侧填写；场景描述负责整体关系。"); wrapMode: Text.Wrap; font.pixelSize: 11; color: StudioTheme.muted }
                    Text { visible: root.layoutEditing; text: HomeTheme.t("从示例开始"); color: StudioTheme.secondary; font.pixelSize: 11 }
                    RowLayout {
                        visible: root.layoutEditing; Layout.fillWidth: true; spacing: 4
                        TemplateCard { Layout.fillWidth: true; text: HomeTheme.t("中文海报"); kind: "poster"; onClicked: root.templateFor("poster") }
                        TemplateCard { Layout.fillWidth: true; text: HomeTheme.t("手写便签"); kind: "note"; onClicked: root.templateFor("note") }
                        TemplateCard { Layout.fillWidth: true; text: HomeTheme.t("对象布局"); kind: "objects"; onClicked: root.templateFor("objects") }
                    }
                    Text { visible: root.layoutEditing; text: HomeTheme.t("示例会替换画面输入，可以撤销。"); color: StudioTheme.muted; font.pixelSize: 10 }
                    Rectangle { Layout.fillWidth: true; height: 1; color: StudioTheme.line }
                    Text { text: HomeTheme.t("画布与质量"); font.weight: Font.DemiBold; font.pixelSize: 14; color: StudioTheme.text }
                    StudioCombo {
                        Layout.fillWidth: true; model: [HomeTheme.t("方形 · 1024 × 1024"), HomeTheme.t("大方图 · 1536 × 1536"), HomeTheme.t("快速草图 · 512 × 512")]
                        currentIndex: doc.canvasWidth === 1536 ? 1 : doc.canvasWidth === 512 ? 2 : 0
                        onActivated: { const size = [1024,1536,512][currentIndex]; doc.setCanvasSize(size,size); studio.layerDocument.setCanvasSize(size,size) }
                    }
                    StudioCombo {
                        Layout.fillWidth: true; model: [HomeTheme.t("草图 · 20 步"), HomeTheme.t("标准 · 50 步（官方推荐）"), HomeTheme.t("精细 · 70 步"), HomeTheme.t("自定义步数")]
                        currentIndex: studio.steps === 20 ? 0 : studio.steps === 50 ? 1 : studio.steps === 70 ? 2 : 3
                        onActivated: { if (currentIndex < 3) studio.steps = [20,50,70][currentIndex] }
                    }
                    Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: HomeTheme.t("长文字与复杂布局优先使用标准质量。大尺寸会增加耗时和内存。"); color: StudioTheme.muted; font.pixelSize: 11 }
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: HomeTheme.t("随机种子"); color: StudioTheme.secondary; font.pixelSize: 11 }
                        Item { Layout.fillWidth: true }
                        StudioButton { text: HomeTheme.t("换一个"); implicitHeight: 28; onClicked: studio.randomizeSeed() }
                    }
                    StudioField { objectName: "generationSeed"; Layout.fillWidth: true; Accessible.name: HomeTheme.t("随机种子"); text: String(studio.seed); validator: IntValidator { bottom: 0; top: 2147483647 } onEditingFinished: if (acceptableInput) studio.seed = Number(text) }
                    Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: HomeTheme.t("修改描述时保留种子，便于对照；换种子可探索新构图。"); color: StudioTheme.muted; font.pixelSize: 10 }
                    StudioButton { Layout.fillWidth: true; text: root.advanced ? HomeTheme.t("▾  高级输入与参数") : HomeTheme.t("▸  高级输入与参数"); onClicked: root.advanced = !root.advanced }
                    ColumnLayout {
                        visible: root.advanced; Layout.fillWidth: true; spacing: 10
                        Text { text: HomeTheme.t("不希望出现的内容"); color: StudioTheme.secondary; font.pixelSize: 11 }
                        InputArea { Layout.fillWidth: true; implicitHeight: 96; label: HomeTheme.t("负面提示词"); value: doc.negativePrompt; placeholder: HomeTheme.t("可留空，例如：低清晰度、模糊文字…"); onEdited: text => doc.negativePrompt = text }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: HomeTheme.t("步数"); color: StudioTheme.secondary; Layout.fillWidth: true }
                            StudioSpinBox { from: 1; to: 100; value: studio.steps; editable: true; onValueModified: studio.steps = value }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: HomeTheme.t("引导强度"); color: StudioTheme.secondary; Layout.fillWidth: true }
                            StudioField { Layout.preferredWidth: 100; text: String(studio.cfg); Accessible.name: HomeTheme.t("引导强度 CFG"); validator: DoubleValidator { bottom: 1; top: 10 } onEditingFinished: if (acceptableInput) studio.cfg = Number(text) }
                        }
                        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: HomeTheme.t("CFG 默认 3；官方建议 3–6。长描述通常保持 3，短描述可适当提高。"); font.pixelSize: 10; color: StudioTheme.muted }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: HomeTheme.t("采样偏移"); color: StudioTheme.secondary; Layout.fillWidth: true }
                            StudioField { Layout.preferredWidth: 100; text: String(studio.flowShift); Accessible.name: HomeTheme.t("采样偏移"); validator: DoubleValidator { bottom: 1; top: 10 } onEditingFinished: if (acceptableInput) studio.flowShift = Number(text) }
                        }
                        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: HomeTheme.t("默认 5。引擎使用 Euler；负分支跳过第 22 层的官方策略已自动启用。"); font.pixelSize: 10; color: StudioTheme.muted }
                        Text { visible: doc.layout; text: HomeTheme.t("布局类别（逗号分隔，可留空）"); color: StudioTheme.secondary; font.pixelSize: 11 }
                        StudioField { visible: doc.layout; Layout.fillWidth: true; text: doc.layoutMeta; placeholderText: "poster, illustration"; onEditingFinished: doc.layoutMeta = text }
                        StudioButton { Layout.fillWidth: true; text: HomeTheme.t("导入 Layout JSON"); onClicked: { layoutDraft.value = ""; layoutDialog.open() } }
                        StudioButton { visible: doc.layout; Layout.fillWidth: true; text: HomeTheme.t("编辑当前 Layout JSON"); onClicked: { layoutDraft.value = doc.inputPreview; layoutDialog.open() } }
                    }
                    StudioButton { Layout.fillWidth: true; text: HomeTheme.t("检查实际生成输入"); onClicked: previewDialog.open() }
                    Text { Layout.fillWidth: true; text: "MOSS-Image SFT · " + studio.imageQuant; color: StudioTheme.secondary; font.pixelSize: 11 }
                    StudioButton { Layout.fillWidth: true; text: studio.imageReady ? HomeTheme.t("模型设置") : HomeTheme.t("准备模型"); onClicked: root.navigate(3) }
                    Item { height: 8 }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true; color: "transparent"
            Rectangle { width: 1; height: parent.height; color: StudioTheme.line }
            ScrollView {
                anchors.fill: parent; anchors.margins: 16; contentWidth: availableWidth; clip: true
                ColumnLayout {
                    width: parent.width; spacing: 12; enabled: !studio.busy
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: HomeTheme.t("区域属性"); font.weight: Font.DemiBold; font.pixelSize: 14; color: StudioTheme.text }
                        Item { Layout.fillWidth: true }
                        StudioButton { text: HomeTheme.t("返回画面"); implicitHeight: 26; quiet: true; onClicked: root.inspectorTab = 0 }
                    }
                    ListView {
                        Layout.fillWidth: true; Layout.preferredHeight: Math.min(152, doc.regions.length*38); clip: true; model: doc.regions; spacing: 3
                        delegate: Rectangle {
                            required property var modelData; required property int index
                            width: ListView.view.width; height: 35; radius: 6; color: doc.selected === index ? StudioTheme.selection : "transparent"
                            RowLayout {
                                anchors.fill: parent; anchors.margins: 8; spacing: 8
                                Text { text: modelData.type === "text" ? "T" : "▧"; color: modelData.color; font.weight: Font.DemiBold }
                                Text { Layout.fillWidth: true; text: modelData.name.replace(/[\r\n\u2028\u2029]+/g," "); font.pixelSize: 12; color: StudioTheme.secondary; elide: Text.ElideRight }
                                Text { visible: modelData.caption.trim() === "" || (modelData.type === "text" && modelData.text.trim() === ""); text: HomeTheme.t("待填写"); font.pixelSize: 10; color: "#AA815B" }
                            }
                            MouseArea { anchors.fill: parent; onClicked: { root.forceActiveFocus(); doc.selected = index } }
                        }
                    }
                    Text { visible: root.selected === null; Layout.fillWidth: true; wrapMode: Text.Wrap; text: HomeTheme.t("点击「＋ 对象」或「T 文字」，然后在画布上拖出区域。\n\n选择一个区域后，在这里填写内容与位置。"); lineHeight: 1.5; color: StudioTheme.muted; font.pixelSize: 12 }
                    ColumnLayout {
                        visible: root.selected !== null; Layout.fillWidth: true; spacing: 10
                        Rectangle { Layout.fillWidth: true; height: 1; color: StudioTheme.line }
                        StudioField { Layout.fillWidth: true; Accessible.name: HomeTheme.t("区域名称"); text: root.selected ? root.selected.name : ""; placeholderText: HomeTheme.t("区域名称"); onTextEdited: doc.updateRegion(doc.selected, "name", text) }
                        StudioCombo { Layout.fillWidth: true; model: [HomeTheme.t("对象区域"), HomeTheme.t("文字区域")]; currentIndex: root.selected && root.selected.type === "text" ? 1 : 0; onActivated: doc.updateRegion(doc.selected, "type", currentIndex === 1 ? "text" : "object") }
                        Text { visible: root.selected && root.selected.type === "text"; text: HomeTheme.t("要画出的文字 · 保留换行"); color: StudioTheme.secondary; font.pixelSize: 12 }
                        InputArea { visible: root.selected && root.selected.type === "text"; Layout.fillWidth: true; implicitHeight: 125; objectName: "regionText"; label: HomeTheme.t("画面中的文字"); value: root.selected ? root.selected.text : ""; placeholder: HomeTheme.t("标题、段落或标语\n每行文字会按原文提交"); onEdited: text => doc.updateRegion(doc.selected, "text", text) }
                        Text { text: root.selected && root.selected.type === "text" ? HomeTheme.t("文字外观与排版建议") : HomeTheme.t("对象细节"); color: StudioTheme.secondary; font.pixelSize: 12 }
                        InputArea { Layout.fillWidth: true; implicitHeight: 125; objectName: "regionCaption"; label: HomeTheme.t("区域描述"); value: root.selected ? root.selected.caption : ""; placeholder: root.selected && root.selected.type === "text" ? HomeTheme.t("例如：深蓝色粗体、居中、每行留出间距…") : HomeTheme.t("外观、颜色、材质、动作…"); onEdited: text => doc.updateRegion(doc.selected, "caption", text) }
                        RowLayout {
                            visible: root.selected && root.selected.type === "text"; Layout.fillWidth: true; spacing: 4
                            Repeater {
                                model: [{title:HomeTheme.t("粗体"),text:"Bold sans-serif lettering."},{title:HomeTheme.t("手写"),text:"Clear handwritten lettering."},{title:HomeTheme.t("居中"),text:"Center aligned text."}]
                                delegate: StudioButton {
                                    required property var modelData
                                    Layout.fillWidth: true; text: modelData.title; implicitHeight: 29
                                    onClicked: { const old = root.selected.caption; if (old.indexOf(modelData.text) < 0) doc.updateRegion(doc.selected,"caption",old + (old ? "\n" : "") + modelData.text) }
                                }
                            }
                        }
                        Text { text: HomeTheme.t("位置与大小 · px"); color: StudioTheme.secondary; font.pixelSize: 12 }
                        GridLayout {
                            columns: 2; Layout.fillWidth: true; columnSpacing: 8; rowSpacing: 6
                            Repeater {
                                model: [{key:"x",title:HomeTheme.t("X  左侧")},{key:"y",title:HomeTheme.t("Y  顶部")},{key:"width",title:HomeTheme.t("宽度")},{key:"height",title:HomeTheme.t("高度")}]
                                delegate: ColumnLayout {
                                    required property var modelData
                                    Layout.fillWidth: true; spacing: 4
                                    Text { text: modelData.title; font.pixelSize: 10; color: StudioTheme.muted }
                                    StudioField {
                                        Layout.fillWidth: true; Accessible.name: modelData.title
                                        text: root.selected ? String(Math.round(root.selected[modelData.key])) : ""
                                        validator: IntValidator { bottom: 0; top: 4096 }
                                        onEditingFinished: {
                                            if (root.selected && acceptableInput) {
                                                const r = Object.assign({}, root.selected); r[modelData.key] = Number(text)
                                                doc.moveRegion(doc.selected, r.x, r.y, r.width, r.height)
                                            }
                                            text = Qt.binding(() => root.selected ? String(Math.round(root.selected[modelData.key])) : "")
                                        }
                                    }
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            StudioButton { Layout.fillWidth: true; text: HomeTheme.t("水平居中"); onClicked: doc.moveRegion(doc.selected, (doc.canvasWidth-root.selected.width)/2, root.selected.y, root.selected.width, root.selected.height) }
                            StudioButton { Layout.fillWidth: true; text: HomeTheme.t("复制区域"); onClicked: doc.duplicateSelected() }
                        }
                        StudioButton { Layout.fillWidth: true; text: HomeTheme.t("删除区域"); onClicked: { root.forceActiveFocus(); doc.removeSelected() } }
                        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: HomeTheme.t("框控制位置与比例，外观文字是生成建议。输出仍是一张图片。"); color: StudioTheme.muted; font.pixelSize: 10; lineHeight: 1.4 }
                    }
                    Item { height: 10 }
                }
            }
        }
                RepaintSettings { objectName: "repaintInspector"; Layout.fillWidth: true; Layout.fillHeight: true; color: "transparent" }
                NarrationSettings { objectName: "speechInspector"; mode: "speech"; Layout.fillWidth: true; Layout.fillHeight: true; color: "transparent"; onModelSettings: root.navigate(3) }
                NarrationSettings { objectName: "sfxInspector"; mode: "sfx"; Layout.fillWidth: true; Layout.fillHeight: true; color: "transparent"; onModelSettings: root.navigate(3) }
                BackgroundSettings { objectName: "backgroundInspector"; Layout.fillWidth: true; Layout.fillHeight: true; color: "transparent" }
                        CanvasObjectSettings { Layout.fillWidth: true; Layout.fillHeight: true; color: "transparent" }
                        NarrationSettings { objectName: "voiceInspector"; mode: "voice"; Layout.fillWidth: true; Layout.fillHeight: true; color: "transparent"; onModelSettings: root.navigate(3) }
                        }
            ColumnLayout {
                visible: root.inspectorContext !== "object" && root.inspectorContext !== "region" && root.inspectorContext !== "background"
                Layout.fillWidth: true; Layout.margins: 12; spacing: 8
                Rectangle { visible: root.currentTrack === "speech" || root.currentTrack === "sfx"; Layout.fillWidth: true; height: 1; color: StudioTheme.line }
                RowLayout {
                    visible: root.currentTrack === "speech" || root.currentTrack === "sfx"
                    Layout.fillWidth: true; enabled: !studio.busy
                    Text { text: HomeTheme.t("播放设置"); color: StudioTheme.secondary; font.pixelSize: 11; Layout.fillWidth: true }
                    StudioButton {
                        objectName: "toggleElementAudio"
                        text: root.track.enabled ? HomeTheme.t("已启用") : HomeTheme.t("已静音")
                        iconName: root.track.enabled ? "volume" : "muted"
                        hint: root.track.enabled ? HomeTheme.t("静音当前声音") : HomeTheme.t("启用当前声音")
                        quiet: true; active: !root.track.enabled
                        onClicked: studio.timeline.setValue(root.currentTrack, "enabled", !root.track.enabled)
                    }
                }
                RowLayout {
                    visible: root.currentTrack === "image"
                    Layout.fillWidth: true; spacing: 6
                    StudioCombo {
                        objectName: "imageDeviceSelector"; Accessible.name: HomeTheme.t("生图运行位置")
                        Layout.fillWidth: true; model: studio.remoteGpu.available ? [HomeTheme.t("本机 · Apple GPU"), HomeTheme.t("远程 GPU")] : [HomeTheme.t("本机 · Apple GPU")]
                        currentIndex: studio.imageDevice === "remote" ? 1 : 0
                        enabled: !studio.busy && !studio.remoteGpu.busy
                        onActivated: studio.imageDevice = currentIndex === 1 ? "remote" : "local"
                    }
                    StudioButton { text: HomeTheme.t("设置"); quiet: true; onClicked: root.navigate(3) }
                }
                Text {
                    visible: root.currentTrack === "image" && studio.imageDevice === "remote" && !studio.busy
                    Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 10
                    text: studio.remoteGpu.ready ? HomeTheme.t("远程 GPU 已连接 · 完成后自动回收") : HomeTheme.t("请在设置中连接远程 GPU")
                    color: studio.remoteGpu.ready ? StudioTheme.accent : StudioTheme.muted
                }
                RowLayout {
                    visible: root.currentTrack === "speech" || root.currentTrack === "sfx"; Layout.fillWidth: true; enabled: !studio.busy; spacing: 6
                    Text { text: HomeTheme.t("开始"); color: StudioTheme.muted; font.pixelSize: 11 }
                    StudioField { objectName: "elementStart"; Layout.fillWidth: true; text: Number(root.track.start || 0).toFixed(1); Accessible.name: HomeTheme.t("开始时间（秒）"); validator: DoubleValidator { bottom: 0; top: 599.9 } onEditingFinished: { if (acceptableInput) studio.timeline.setRange(root.currentTrack,Number(text),root.track.duration); text = Qt.binding(() => Number(root.track.start || 0).toFixed(1)) } }
                    Text { text: HomeTheme.t("时长"); color: StudioTheme.muted; font.pixelSize: 11 }
                    StudioField { objectName: "elementDuration"; Layout.fillWidth: true; text: Number(root.track.duration || 30).toFixed(1); Accessible.name: HomeTheme.t("播放时长（秒）"); validator: DoubleValidator { bottom: .1; top: 600 } onEditingFinished: { if (acceptableInput) studio.timeline.setRange(root.currentTrack,root.track.start,Number(text)); text = Qt.binding(() => Number(root.track.duration || 30).toFixed(1)) } }
                    Text { text: HomeTheme.t("秒"); color: StudioTheme.muted; font.pixelSize: 11 }
                }
                Text { Layout.fillWidth: true; wrapMode: Text.Wrap; maximumLineCount: 2; text: studio.busy ? HomeTheme.t(studio.status) : root.currentTrack === "image" ? HomeTheme.t("参数应用于当前画布") : HomeTheme.t("视频总时长自动跟随最长音频"); color: StudioTheme.muted; font.pixelSize: 10 }
                StudioButton {
                    objectName: "generateTrack"; iconName: root.currentTrack === "image" ? "spark" : "audio"
                    visible: root.inspectorContext === "voice" || root.inspectorContext === "speech" || root.inspectorContext === "sfx" || root.inspectorContext === "repaint"
                    Layout.fillWidth: true; implicitHeight: 38; primary: true
                    text: studio.busy ? HomeTheme.t("取消当前任务") : root.inspectorContext === "voice" ? HomeTheme.t("合成语音") : root.currentTrack === "speech" ? (studio.pageVisualUrl ? HomeTheme.t("生成配音与数字人") : HomeTheme.t("合成配音")) : root.currentTrack === "sfx" ? HomeTheme.t("生成音效") : studio.repainting ? HomeTheme.t("重绘选区") : HomeTheme.t("生成图像")
                    enabled: true
                    onClicked: { root.forceActiveFocus(); trackPreview.playing = false; if (studio.busy) studio.cancel(); else if(root.inspectorContext === "voice"){ studio.sound.set("voiceSource","tts"); studio.generateTrack("voice") }else studio.generateTrack(root.currentTrack) }
                }
            }
                        }
                    }
                }
            }
    }
}
