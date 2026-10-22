import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import MossStudio

Item {
    id: root
    property var doc: studio.layerDocument
    property var selected: doc.selected >= 0 && doc.selected < doc.layers.length ? doc.layers[doc.selected] : null
    signal editReady()
    function textFocused() {
        const f = root.Window.window.activeFocusItem
        return f && (f.hasOwnProperty("cursorPosition") || f.hasOwnProperty("inputMethodComposing"))
    }
    Shortcut { sequences: [StandardKey.Undo]; enabled: root.visible && !studio.busy && !root.textFocused(); onActivated: doc.undo() }
    Shortcut { sequences: [StandardKey.Redo]; enabled: root.visible && !studio.busy && !root.textFocused(); onActivated: doc.redo() }
    Shortcut { sequences: [StandardKey.Save]; enabled: root.visible && !studio.busy; onActivated: saveDialog.open() }
    FileDialog {
        id: importDialog; title: HomeTheme.t("导入图层 · 可多选"); fileMode: FileDialog.OpenFiles
        nameFilters: [HomeTheme.t("图像与矢量 (*.png *.jpg *.jpeg *.webp *.svg)")]
        onAccepted: doc.importFiles(selectedFiles)
    }
    FileDialog { id: openDialog; title: HomeTheme.t("打开图层工程"); nameFilters: [HomeTheme.t("图层工程 (*.mosslayers.json *.json)")]; onAccepted: doc.loadProject(selectedFile) }
    FileDialog { id: saveDialog; title: HomeTheme.t("保存图层工程"); fileMode: FileDialog.SaveFile; nameFilters: [HomeTheme.t("图层工程 (*.mosslayers.json)")]; onAccepted: doc.saveProject(selectedFile) }
    FileDialog { id: exportDialog; title: HomeTheme.t("导出合成图片"); fileMode: FileDialog.SaveFile; nameFilters: [HomeTheme.t("PNG 图片 (*.png)")]; onAccepted: doc.exportPng(selectedFile) }
    DropArea {
        anchors.fill: parent; enabled: !studio.busy
        onDropped: drop => { if (drop.hasUrls) { doc.importFiles(drop.urls); drop.acceptProposedAction() } }
    }
    RowLayout {
        anchors.fill: parent; spacing: 0
        Rectangle {
            Layout.preferredWidth: 260; Layout.fillHeight: true; color: StudioTheme.panel
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: StudioTheme.line }
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 16; spacing: 12; enabled: !studio.busy
                Text { text: HomeTheme.t("素材图层"); color: StudioTheme.text; font.pixelSize: 15; font.weight: Font.DemiBold }
                StudioButton { Layout.fillWidth: true; text: HomeTheme.t("＋ 导入图片 / SVG"); primary: true; onClicked: importDialog.open() }
                StudioButton { Layout.fillWidth: true; text: HomeTheme.t("加入当前生成图"); enabled: studio.imageUrl !== ""; onClicked: studio.addGeneratedLayer() }
                Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: HomeTheme.t("可多选或拖入素材。列表上方的图层显示在画面上层。"); color: StudioTheme.muted; font.pixelSize: 11 }
                ListView {
                    id: layersList; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 5
                    model: doc.layers.slice().reverse()
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        property int layerIndex: doc.layers.length - index - 1
                        width: ListView.view.width; height: 67; radius: 8
                        color: doc.selected === layerIndex ? StudioTheme.selection : StudioTheme.elevated
                        border.color: doc.selected === layerIndex ? StudioTheme.line : "transparent"
                        MouseArea { anchors.fill: parent; onClicked: { root.forceActiveFocus(); doc.selected = layerIndex } }
                        RowLayout {
                            anchors.fill: parent; anchors.margins: 7; spacing: 8
                            Rectangle {
                                width: 46; height: 46; radius: 5; color: StudioTheme.field
                                Image { anchors.fill: parent; anchors.margins: 3; source: modelData.thumbnail; fillMode: Image.PreserveAspectFit; opacity: modelData.visible ? 1 : 0.35 }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 5
                                Text { Layout.fillWidth: true; text: modelData.name.replace(/[\r\n\u2028\u2029]+/g," "); elide: Text.ElideRight; font.pixelSize: 12; color: StudioTheme.text }
                                Text { text: (modelData.kind === "svg" ? HomeTheme.t("SVG 矢量") : HomeTheme.t("图像")) + " · " + Math.round(modelData.opacity*100) + "%"; color: StudioTheme.muted; font.pixelSize: 10 }
                            }
                            StudioButton { text: modelData.visible ? "◉" : "○"; implicitWidth: 28; implicitHeight: 28; Accessible.name: modelData.visible ? HomeTheme.t("隐藏图层") : HomeTheme.t("显示图层"); ToolTip.visible: hovered; ToolTip.text: Accessible.name; onClicked: doc.updateLayer(layerIndex,"visible",!modelData.visible) }
                        }
                    }
                    ScrollBar.vertical: ScrollBar {}
                    Text { anchors.centerIn: parent; visible: doc.layers.length === 0; text: HomeTheme.t("把图片或 SVG\n拖到这里开始"); horizontalAlignment: Text.AlignHCenter; lineHeight: 1.6; color: StudioTheme.muted; font.pixelSize: 13 }
                }
                RowLayout {
                    Layout.fillWidth: true
                    StudioButton { Layout.fillWidth: true; text: HomeTheme.t("上移"); enabled: doc.selected >= 0 && doc.selected < doc.layers.length-1; onClicked: doc.moveSelected(1) }
                    StudioButton { Layout.fillWidth: true; text: HomeTheme.t("下移"); enabled: doc.selected > 0; onClicked: doc.moveSelected(-1) }
                }
                RowLayout {
                    Layout.fillWidth: true
                    StudioButton { Layout.fillWidth: true; text: HomeTheme.t("打开工程"); onClicked: openDialog.open() }
                    StudioButton { Layout.fillWidth: true; text: HomeTheme.t("保存工程"); onClicked: saveDialog.open() }
                }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
            Rectangle {
                Layout.fillWidth: true; height: 52; color: StudioTheme.field
                RowLayout {
                    anchors.fill: parent; anchors.margins: 10
                    Text { Layout.fillWidth: true; text: HomeTheme.t("拖动移动 · 右下角缩放"); font.pixelSize: 12; color: StudioTheme.muted }
                    StudioCheckBox { text: HomeTheme.t("等比缩放"); checked: true; onToggled: canvas.keepAspect = checked }
                    StudioButton { iconName: "undo"; Accessible.name: HomeTheme.t("撤销"); enabled: doc.canUndo && !studio.busy; onClicked: { root.forceActiveFocus(); doc.undo() } }
                    StudioButton { iconName: "redo"; Accessible.name: HomeTheme.t("重做"); enabled: doc.canRedo && !studio.busy; onClicked: { root.forceActiveFocus(); doc.redo() } }
                }
            }
            LayerCanvas { id: canvas; Layout.fillWidth: true; Layout.fillHeight: true; document: doc; enabled: !studio.busy }
            Rectangle {
                Layout.fillWidth: true; height: 48; color: StudioTheme.chrome
                Text { anchors.fill: parent; anchors.margins: 12; text: doc.message || HomeTheme.t("透明棋盘仅用于预览，导出不会包含棋盘。"); elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter; color: StudioTheme.secondary; font.pixelSize: 11 }
            }
        }
        Rectangle {
            Layout.preferredWidth: 272; Layout.fillHeight: true; color: StudioTheme.panel
            Rectangle { width: 1; height: parent.height; color: StudioTheme.line }
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 16; spacing: 12
                ScrollView {
                    Layout.fillWidth: true; Layout.fillHeight: true; contentWidth: availableWidth; clip: true
                    ColumnLayout {
                        width: parent.width; spacing: 12; enabled: !studio.busy
                        Text { text: HomeTheme.t("画布"); font.pixelSize: 14; font.weight: Font.DemiBold; color: StudioTheme.text }
                        RowLayout {
                            Layout.fillWidth: true
                            StudioField { Layout.fillWidth: true; Accessible.name: HomeTheme.t("合成画布宽度"); text: String(doc.canvasWidth); validator: IntValidator { bottom: 256; top: 2048 } onEditingFinished: { if(acceptableInput)doc.setCanvasSize(Number(text),doc.canvasHeight); text=Qt.binding(()=>String(doc.canvasWidth)) } }
                            Text { text: "×"; color: StudioTheme.muted }
                            StudioField { Layout.fillWidth: true; Accessible.name: HomeTheme.t("合成画布高度"); text: String(doc.canvasHeight); validator: IntValidator { bottom: 256; top: 2048 } onEditingFinished: { if(acceptableInput)doc.setCanvasSize(doc.canvasWidth,Number(text)); text=Qt.binding(()=>String(doc.canvasHeight)) } }
                        }
                        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: HomeTheme.t("256–2048 px；改变画布时保留素材尺寸与位置。"); font.pixelSize: 10; color: StudioTheme.muted }
                        StudioCombo {
                            Layout.fillWidth: true; model: [HomeTheme.t("透明背景"),HomeTheme.t("白色背景"),HomeTheme.t("奶油白背景"),HomeTheme.t("黑色背景")]
                            property var values: ["transparent",StudioTheme.muted,"#f8f4ec","#000000"]
                            currentIndex: values.indexOf(doc.background)
                            onActivated: doc.setBackground(values[currentIndex])
                        }
                        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: HomeTheme.t("准备交给 AI 编辑时，建议选择不透明背景。"); color: StudioTheme.muted; font.pixelSize: 11 }
                        Rectangle { Layout.fillWidth: true; height: 1; color: StudioTheme.line }
                        Text { text: root.selected ? HomeTheme.t("选中图层") : HomeTheme.t("选择图层以调整"); font.pixelSize: 14; font.weight: Font.DemiBold; color: StudioTheme.text }
                        ColumnLayout {
                            Layout.fillWidth: true; visible: root.selected !== null; spacing: 12
                            StudioField { Layout.fillWidth: true; Accessible.name: HomeTheme.t("图层名称"); text: root.selected ? root.selected.name : ""; onEditingFinished: { doc.updateLayer(doc.selected,"name",text); text=Qt.binding(()=>root.selected?root.selected.name:"") } }
                            GridLayout {
                                Layout.fillWidth: true; columns: 2; columnSpacing: 8; rowSpacing: 9
                                Repeater {
                                    model: [{key:"x",title:HomeTheme.t("X 位置"),min:-32768,max:32768},{key:"y",title:HomeTheme.t("Y 位置"),min:-32768,max:32768},{key:"width",title:HomeTheme.t("图层宽度"),min:1,max:16384},{key:"height",title:HomeTheme.t("图层高度"),min:1,max:16384}]
                                    delegate: ColumnLayout {
                                        required property var modelData
                                        Layout.fillWidth: true; spacing: 4
                                        Text { text: modelData.title; font.pixelSize: 11; color: StudioTheme.muted }
                                        StudioField {
                                            Layout.fillWidth: true; Accessible.name: modelData.title
                                            text: root.selected ? String(Math.round(root.selected[modelData.key])) : ""
                                            validator: IntValidator { bottom: modelData.min; top: modelData.max }
                                            onEditingFinished: {
                                                if(root.selected&&acceptableInput)doc.updateLayer(doc.selected,modelData.key,Number(text))
                                                text=Qt.binding(()=>root.selected?String(Math.round(root.selected[modelData.key])):"")
                                            }
                                        }
                                    }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: HomeTheme.t("旋转角度"); Layout.fillWidth: true; color: StudioTheme.muted; font.pixelSize: 11 }
                                StudioField { Layout.preferredWidth: 96; Accessible.name: HomeTheme.t("旋转角度"); text: root.selected ? String(root.selected.rotation) : ""; validator: DoubleValidator { bottom: -180; top: 180 } onEditingFinished: if(root.selected&&acceptableInput)doc.updateLayer(doc.selected,"rotation",Number(text)) }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: HomeTheme.t("不透明度"); Layout.fillWidth: true; color: StudioTheme.muted; font.pixelSize: 11 }
                                Text { text: Math.round(opacitySlider.value*100)+"%"; color: StudioTheme.secondary; font.pixelSize: 11 }
                            }
                            StudioSlider { id: opacitySlider; Layout.fillWidth: true; from: 0; to: 1; value: root.selected ? root.selected.opacity : 1; onPressedChanged: if(!pressed&&root.selected)doc.updateLayer(doc.selected,"opacity",value); onMoved: if(!pressed&&root.selected)doc.updateLayer(doc.selected,"opacity",value) }
                            RowLayout {
                                Layout.fillWidth: true
                                StudioButton { Layout.fillWidth: true; text: HomeTheme.t("适应画布"); onClicked: doc.fitSelected() }
                                StudioButton { Layout.fillWidth: true; text: HomeTheme.t("居中"); onClicked: doc.centerSelected() }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                StudioButton { Layout.fillWidth: true; text: HomeTheme.t("复制图层"); onClicked: doc.duplicateSelected() }
                                StudioButton { Layout.fillWidth: true; text: HomeTheme.t("删除图层"); onClicked: { root.forceActiveFocus(); doc.removeSelected() } }
                            }
                            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; visible: root.selected && root.selected.kind === "svg"; text: HomeTheme.t("保留 SVG 源，合成时按最终画布绘制。复杂特效的显示以预览为准。"); font.pixelSize: 11; color: StudioTheme.muted }
                        }
                    }
                }
                StudioButton { Layout.fillWidth: true; text: HomeTheme.t("导出合成 PNG"); enabled: doc.hasContent && !studio.busy; onClicked: exportDialog.open() }
                StudioButton { Layout.fillWidth: true; implicitHeight: 44; primary: true; text: HomeTheme.t("合成并局部重绘 →"); enabled: doc.hasContent && !studio.busy; onClicked: { root.forceActiveFocus(); if(studio.editComposition())root.editReady() } }
                Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: HomeTheme.t("生成合成快照，保留原始图层。AI 编辑结果是一张图片。"); font.pixelSize: 10; color: StudioTheme.muted }
            }
        }
    }
}
