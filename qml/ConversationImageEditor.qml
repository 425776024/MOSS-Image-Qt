import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MossStudio

CardSurface {
    id: root
    required property var thread
    property bool maskMode: false
    property bool embedded: false
    readonly property bool narrow: width < 620
    readonly property var doc: studio.document
    readonly property var edit: studio.editDocument
    readonly property var selected: doc.selected >= 0 && doc.selected < doc.regions.length ? doc.regions[doc.selected] : null
    property string maskTool: "brush"
    property bool showMask: true
    property bool advanced: false
    component FieldLabel: Text {
        Layout.preferredWidth: 32
        color: HomeTheme.secondary; font.pixelSize: 11
        verticalAlignment: Text.AlignVCenter
    }
    component InspectorField: HomeField {
        implicitHeight: 28; font.pixelSize: 12; leftPadding: 8; rightPadding: 8
        background: Rectangle { radius: 6; color: HomeTheme.field; border.color: parent.activeFocus ? HomeTheme.accent : HomeTheme.outline }
    }
    component InspectorSelect: AppSelect {
        implicitHeight: 28; font.pixelSize: 12; surfaceRadius: 6; outlined: true
    }
    signal closed()
    outlined: !embedded; radius: embedded ? 0 : 10; baseColor: HomeTheme.field
    function finish() { root.forceActiveFocus(); studio.flushWorkspace(); root.closed() }
    function changeGeometry(key,value) {
        if(!selected)return
        const r=Object.assign({},selected);r[key]=value;doc.moveRegion(doc.selected,r.x,r.y,r.width,r.height)
    }
    function textFocused() { const f=root.Window.window.activeFocusItem;return f && (f.hasOwnProperty("cursorPosition") || f.hasOwnProperty("inputMethodComposing")) }
    Shortcut { sequences: [StandardKey.Undo]; enabled: root.visible && !studio.busy && !root.textFocused(); onActivated: root.maskMode ? root.edit.undo() : root.doc.undo() }
    Shortcut { sequences: [StandardKey.Redo]; enabled: root.visible && !studio.busy && !root.textFocused(); onActivated: root.maskMode ? root.edit.redo() : root.doc.redo() }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: root.embedded ? 8 : 10; spacing: 6
        RowLayout {
            Layout.fillWidth: true; spacing: 4
            Text { visible: !root.embedded; text: root.maskMode ? "局部编辑" : "构图布局"; font.pixelSize: 13; font.weight: Font.DemiBold; color: HomeTheme.text }
            Item { visible: !root.embedded; Layout.preferredWidth: 6 }
            HomeButton { visible: !root.maskMode; text: root.width < 390 ? "" : "选择"; hint: "选择"; iconName: "select"; compact: true; quiet: true; active: !layoutCanvas.drawMode; implicitHeight: 28; onClicked: layoutCanvas.drawMode=false }
            HomeButton { visible: !root.maskMode; text: root.width < 390 ? "" : "对象"; hint: "画出对象区域"; iconName: "rectangle"; compact: true; quiet: true; active: layoutCanvas.drawMode && layoutCanvas.drawType === "object"; implicitHeight: 28; onClicked: {root.forceActiveFocus();root.doc.layout=true;layoutCanvas.drawType="object";layoutCanvas.drawMode=true} }
            HomeButton { visible: !root.maskMode; text: root.width < 390 ? "" : "文字"; hint: "画出文字区域"; iconName: "text"; compact: true; quiet: true; active: layoutCanvas.drawMode && layoutCanvas.drawType === "text"; implicitHeight: 28; onClicked: {root.forceActiveFocus();root.doc.layout=true;layoutCanvas.drawType="text";layoutCanvas.drawMode=true} }
            Repeater {
                model: root.maskMode ? [{key:"brush",label:"涂抹",icon:"brush"},{key:"rectangle",label:"框选",icon:"rectangle"},{key:"erase",label:"擦除",icon:"eraser"}] : []
                HomeButton { required property var modelData; text: root.width < 390 ? "" : modelData.label; hint: modelData.label; iconName: modelData.icon; compact: true; quiet: true; active: root.maskTool===modelData.key; implicitHeight: 28; onClicked: {root.maskTool=modelData.key;root.showMask=true} }
            }
            Item { Layout.fillWidth: true }
            IconButton { glyph: "undo"; hint: "撤销"; enabled: !studio.busy && (root.maskMode ? root.edit.canUndo : root.doc.canUndo); onClicked: {root.forceActiveFocus();root.maskMode ? root.edit.undo() : root.doc.undo()} }
            IconButton { glyph: "redo"; hint: "重做"; enabled: !studio.busy && (root.maskMode ? root.edit.canRedo : root.doc.canRedo); onClicked: {root.forceActiveFocus();root.maskMode ? root.edit.redo() : root.doc.redo()} }
            Rectangle { Layout.preferredHeight: 14; width: 1; color: HomeTheme.line; Layout.leftMargin: 3; Layout.rightMargin: 3 }
            IconButton { glyph: "check"; hint: "完成编辑"; iconColor: HomeTheme.accent; onClicked: root.finish() }
        }
        GridLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; columns: root.narrow ? 1 : 2; columnSpacing: 8; rowSpacing: 6
            Rectangle {
                readonly property real maximumPreviewHeight: Math.min(330,Math.max(210,root.height*.43))
                Layout.fillWidth: true; Layout.fillHeight: !root.narrow
                Layout.preferredHeight: root.narrow ? root.maskMode ? maximumPreviewHeight : Math.min(maximumPreviewHeight,Math.max(120,(width-16)*root.doc.canvasHeight/root.doc.canvasWidth+16)) : -1
                color: HomeTheme.preview; radius: 7; clip: true
                LayoutCanvas { id: layoutCanvas; objectName: "conversationLayoutCanvas"; anchors.fill: parent; visible: !root.maskMode; document: root.doc; canvasPadding: root.embedded ? 8 : 32; compact: root.embedded; enabled: !studio.busy }
                MaskCanvas { objectName: "conversationMaskCanvas"; anchors.fill: parent; visible: root.maskMode; document: root.edit; tool: root.maskTool; brushSize: brush.value; canvasPadding: root.embedded ? 8 : 24; showMask: root.showMask; showResult: false; enabled: !studio.busy; backgroundColor: HomeTheme.preview; Accessible.name: "局部编辑涂抹画布" }
            }
            ScrollView {
                id: inspectorScroll
                Layout.preferredWidth: root.narrow ? -1 : Math.min(260,root.width*.34); Layout.fillWidth: root.narrow; Layout.fillHeight: true; Layout.minimumHeight: 100; contentWidth: availableWidth; clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                Binding { target: inspectorScroll.contentItem; property: "boundsBehavior"; value: Flickable.StopAtBounds }
                ColumnLayout {
                    width: parent.width; spacing: 9; enabled: !studio.busy
                    ColumnLayout {
                        visible: !root.maskMode; Layout.fillWidth: true; spacing: 8
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            FieldLabel { text: "尺寸" }
                            CanvasSizePicker { objectName: "conversationCanvasSize"; Layout.fillWidth: true; Layout.minimumWidth: 0; document: root.doc }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            FieldLabel { text: "模板" }
                            InspectorSelect {
                                Layout.fillWidth: true; Layout.minimumWidth: 0; implicitHeight: 28; model: ["选择模板…","中文海报","手写便签","对象布局"]
                                onActivated: index => {if(index>0){root.forceActiveFocus();root.doc.loadTemplate(["poster","note","objects"][index-1]);root.thread.draft=root.doc.prompt;}currentIndex=0}
                                Accessible.name: "构图模板"
                            }
                        }
                        Rectangle { Layout.fillWidth: true; Layout.topMargin: 2; height: 1; color: HomeTheme.line }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 6
                            Text { text: "对象与文字"; color: HomeTheme.text; font.pixelSize: 12; font.weight: Font.Medium }
                            Text { text: root.doc.regions.length; color: HomeTheme.muted; font.pixelSize: 11 }
                            Item { Layout.fillWidth: true }
                            AppCheckBox { text: "启用布局"; font.pixelSize: 11; Accessible.name: "启用布局"; checked: root.doc.layout; onClicked: root.doc.layout=checked; Accessible.onToggleAction: root.doc.layout=!root.doc.layout }
                        }
                        CardSurface {
                            visible: root.doc.layout && root.doc.regions.length>0
                            Layout.fillWidth: true; implicitHeight: regionGrid.implicitHeight+8
                            outlined: true; radius: 6; baseColor: HomeTheme.field
                        GridView {
                            id: regionGrid; objectName: "conversationRegionGrid"
                            readonly property int columns: width>=360 ? 2 : 1
                            anchors.fill: parent; anchors.margins: 4; implicitHeight: Math.min(4,Math.ceil(count/columns))*cellHeight
                            cellWidth: width/columns; cellHeight: 28; clip: true
                            model: root.doc.regions; currentIndex: root.doc.selected
                            onCurrentIndexChanged: if(currentIndex>=0)positionViewAtIndex(currentIndex,GridView.Contain)
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            delegate: AbstractButton {
                                id: regionChoice; required property var modelData; required property int index
                                readonly property bool chosen: root.doc.selected===index
                                width: regionGrid.cellWidth-(regionGrid.columns>1 ? 4 : 0); height: 26; padding: 6; hoverEnabled: true
                                Accessible.name: "选择"+(modelData.type==="text" ? "文字：" : "对象：")+modelData.name
                                onClicked: {root.forceActiveFocus();root.doc.selected=index;layoutCanvas.drawMode=false}
                                background: Rectangle { radius: 5; color: regionChoice.chosen ? VideoCutTheme.controlSelection : regionChoice.hovered ? HomeTheme.hover : "transparent"; border.width: regionChoice.visualFocus ? 1 : 0; border.color: HomeTheme.accent }
                                contentItem: RowLayout {
                                    spacing: 6
                                    StudioIcon { Layout.preferredWidth: 14; Layout.preferredHeight: 14; name: regionChoice.modelData.type==="text" ? "text" : "rectangle"; ink: regionChoice.modelData.color }
                                    Text { Layout.fillWidth: true; Layout.minimumWidth: 0; text: regionChoice.modelData.name; textFormat: Text.PlainText; color: HomeTheme.text; font.pixelSize: 12; elide: Text.ElideRight }
                                    Text { text: Math.round(regionChoice.modelData.width)+"×"+Math.round(regionChoice.modelData.height); color: HomeTheme.muted; font.pixelSize: 9 }
                                }
                                ToolTip.visible: hovered; ToolTip.delay: 600
                                ToolTip.text: modelData.name+(modelData.caption ? "\n"+modelData.caption : "")
                            }
                        }
                        }
                        Text { visible: root.doc.layout && !root.doc.regions.length; Layout.fillWidth: true; text: "选择顶部对象或文字工具，在画布拖动添加。"; font.pixelSize: 11; color: HomeTheme.muted; wrapMode: Text.Wrap }
                        ColumnLayout {
                            visible: root.doc.layout && !!root.selected; Layout.fillWidth: true; spacing: 6
                            RowLayout {
                                Layout.fillWidth: true; spacing: 4
                                Text { Layout.fillWidth: true; text: root.selected && root.selected.type==="text" ? "文字属性" : "对象属性"; font.pixelSize: 12; font.weight: Font.Medium; color: HomeTheme.text }
                                IconButton { glyph: "copy"; hint: "复制所选对象"; onClicked: {root.forceActiveFocus();root.doc.duplicateSelected()} }
                                IconButton { glyph: "trash"; hint: "删除所选对象"; onClicked: {root.forceActiveFocus();root.doc.removeSelected()} }
                            }
                            RowLayout {
                                Layout.fillWidth: true; spacing: 8
                                FieldLabel { text: "名称" }
                                InspectorField { Layout.fillWidth: true; Accessible.name: "区域名称"; text: root.selected ? root.selected.name : ""; onEditingFinished: root.doc.updateRegion(root.doc.selected,"name",text) }
                            }
                            RowLayout {
                                Layout.fillWidth: true; spacing: 8
                                FieldLabel { text: "类型" }
                                InspectorSelect { Layout.fillWidth: true; Layout.minimumWidth: 0; model: ["对象区域","文字区域"]; currentIndex: root.selected && root.selected.type === "text" ? 1 : 0; Accessible.name: "区域类型"; onActivated: root.doc.updateRegion(root.doc.selected,"type",currentIndex===1 ? "text" : "object") }
                            }
                            RowLayout {
                                Layout.fillWidth: true; spacing: 8
                                FieldLabel { text: "描述"; Layout.alignment: Qt.AlignTop; Layout.topMargin: 7 }
                                InputArea { Layout.fillWidth: true; implicitHeight: 64; radius: 6; label: "区域描述"; value: root.selected ? root.selected.caption : ""; placeholder: root.selected && root.selected.type==="text" ? "文字的字体、颜色、样式…" : "外观、颜色、材质…"; onEdited: text=>root.doc.updateRegion(root.doc.selected,"caption",text) }
                            }
                            RowLayout {
                                visible: root.selected && root.selected.type === "text"; Layout.fillWidth: true; spacing: 8
                                FieldLabel { text: "文字"; Layout.alignment: Qt.AlignTop; Layout.topMargin: 7 }
                                InputArea { Layout.fillWidth: true; implicitHeight: 56; radius: 6; label: "文字原文"; value: root.selected ? root.selected.text : ""; placeholder: "需要出现在画面中的文字"; onEdited: text=>root.doc.updateRegion(root.doc.selected,"text",text) }
                            }
                            GridLayout {
                                Layout.fillWidth: true; columns: 2; columnSpacing: 6; rowSpacing: 5
                                Repeater {
                                    model: [{key:"x",label:"X"},{key:"y",label:"Y"},{key:"width",label:"宽度"},{key:"height",label:"高度"}]
                                    RowLayout {
                                        required property var modelData; Layout.fillWidth: true; spacing: 8
                                        FieldLabel { text: modelData.label }
                                        InspectorField { Layout.fillWidth: true; Layout.minimumWidth: 0; Accessible.name: "区域"+modelData.label; text: root.selected ? String(Math.round(root.selected[modelData.key])) : ""; validator: IntValidator { bottom: 0; top: 4096 } onEditingFinished: {if(acceptableInput)root.changeGeometry(modelData.key,Number(text));text=Qt.binding(()=>root.selected ? String(Math.round(root.selected[modelData.key])) : "")} }
                                    }
                                }
                            }
                        }
                    }
                    ColumnLayout {
                        visible: root.maskMode; Layout.fillWidth: true; spacing: 6
                        RowLayout {
                            Layout.fillWidth: true; spacing: 6
                            Text { Layout.fillWidth: true; text: "重绘选区"; color: HomeTheme.text; font.pixelSize: 12; font.weight: Font.Medium }
                            AppCheckBox { text: "显示选区"; font.pixelSize: 11; checked: root.showMask; onClicked: root.showMask=checked; Accessible.onToggleAction: root.showMask=!root.showMask }
                        }
                        Text { Layout.fillWidth: true; text: root.edit.blendMode === "direct" ? "直接采用模型输出，选区外也可能有细微变化。" : "涂抹要修改的区域，其他部分保留原图。"; color: HomeTheme.secondary; font.pixelSize: 11; wrapMode: Text.Wrap }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            FieldLabel { text: "贴合" }
                            InspectorSelect {
                                objectName: "repaintBlendMode"; Layout.fillWidth: true
                                model: ["羽化贴合", "直接贴合"]
                                currentIndex: root.edit.blendMode === "direct" ? 1 : 0
                                enabled: !studio.busy; Accessible.name: "重绘贴合模式"
                                onActivated: root.edit.blendMode = currentIndex === 1 ? "direct" : "feather"
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            FieldLabel { text: "笔刷" }
                            AppSlider { id: brush; Layout.fillWidth: true; Layout.minimumWidth: 40; implicitWidth: 80; implicitHeight: 28; from: 4; to: 240; value: 48; Accessible.name: "涂抹笔刷大小" }
                            Text { Layout.preferredWidth: 40; text: Math.round(brush.value)+" px"; horizontalAlignment: Text.AlignRight; color: HomeTheme.secondary; font.pixelSize: 11 }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            FieldLabel { text: "强度" }
                            AppSlider { Layout.fillWidth: true; Layout.minimumWidth: 40; implicitWidth: 80; implicitHeight: 28; from: .1; to: 1; stepSize: .05; value: root.edit.strength; Accessible.name: "重绘强度"; onMoved: root.edit.strength=value }
                            Text { Layout.preferredWidth: 40; text: root.edit.strength.toFixed(2); horizontalAlignment: Text.AlignRight; color: HomeTheme.secondary; font.pixelSize: 11 }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            FieldLabel { text: "羽化" }
                            AppSlider { Layout.fillWidth: true; Layout.minimumWidth: 40; implicitWidth: 80; implicitHeight: 28; from: 0; to: 64; stepSize: 1; value: root.edit.feather; enabled: !studio.busy && root.edit.blendMode !== "direct"; Accessible.name: "选区羽化"; onMoved: root.edit.feather=value }
                            Text { Layout.preferredWidth: 40; text: root.edit.feather+" px"; horizontalAlignment: Text.AlignRight; color: HomeTheme.secondary; font.pixelSize: 11 }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 6
                            Item { Layout.fillWidth: true }
                            HomeButton { text: "反选"; compact: true; quiet: true; implicitHeight: 26; onClicked: root.edit.invertMask() }
                            HomeButton { text: "清空选区"; compact: true; quiet: true; implicitHeight: 26; enabled: root.edit.hasMask; onClicked: root.edit.clearMask() }
                        }
                        Text { visible: !root.edit.hasMask; Layout.fillWidth: true; text: "尚未选择区域；直接发送将编辑整张图像。"; color: HomeTheme.muted; font.pixelSize: 11; wrapMode: Text.Wrap }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: HomeTheme.line }
                    AbstractButton {
                        Layout.fillWidth: true; implicitHeight: 28; padding: 4; hoverEnabled: true
                        Accessible.name: root.advanced ? "收起生成参数" : "展开生成参数"; onClicked: root.advanced=!root.advanced
                        background: Rectangle { radius: 5; color: parent.hovered ? HomeTheme.hover : "transparent" }
                        contentItem: RowLayout {
                            spacing: 6
                            StudioIcon { Layout.preferredWidth: 14; Layout.preferredHeight: 14; name: "settings"; ink: HomeTheme.secondary }
                            Text { Layout.fillWidth: true; text: "生成参数"; color: HomeTheme.secondary; font.pixelSize: 12 }
                            StudioIcon { Layout.preferredWidth: 12; Layout.preferredHeight: 12; name: "chevron"; rotation: root.advanced ? 180 : 0; ink: HomeTheme.muted }
                        }
                    }
                    ColumnLayout {
                        visible: root.advanced; Layout.fillWidth: true; spacing: 6
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            FieldLabel { text: "步数" }
                            InspectorField { Layout.fillWidth: true; Accessible.name: "采样步数"; text: String(studio.steps); validator: IntValidator { bottom: 1; top: 100 } onEditingFinished: {if(acceptableInput)studio.steps=Number(text);text=Qt.binding(()=>String(studio.steps))} }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            FieldLabel { text: "种子" }
                            InspectorField { Layout.fillWidth: true; Layout.minimumWidth: 0; Accessible.name: "随机种子"; text: String(studio.seed); validator: IntValidator { bottom: 0; top: 2147483647 } onEditingFinished: if(acceptableInput)studio.seed=Number(text) }
                            IconButton { glyph: "refresh"; hint: "更换随机种子"; onClicked: {root.forceActiveFocus();studio.randomizeSeed()} }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            FieldLabel { text: "CFG" }
                            InspectorField { Layout.fillWidth: true; Accessible.name: "引导强度 CFG"; text: String(studio.cfg); validator: DoubleValidator { bottom: 1; top: 10 } onEditingFinished: {if(acceptableInput)studio.cfg=Number(text);text=Qt.binding(()=>String(studio.cfg))} }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8; visible: !root.maskMode
                            FieldLabel { text: "偏移" }
                            InspectorField { Layout.fillWidth: true; Accessible.name: "采样偏移"; text: String(studio.flowShift); validator: DoubleValidator { bottom: 1; top: 10 } onEditingFinished: {if(acceptableInput)studio.flowShift=Number(text);text=Qt.binding(()=>String(studio.flowShift))} }
                        }
                        RowLayout {
                            visible: !root.maskMode; Layout.fillWidth: true; spacing: 8
                            FieldLabel { text: "风格"; Layout.alignment: Qt.AlignTop; Layout.topMargin: 7 }
                            InputArea { Layout.fillWidth: true; implicitHeight: 56; radius: 6; label: "视觉风格"; value: root.doc.style; placeholder: "视觉风格（可选）"; onEdited: text=>root.doc.style=text }
                        }
                        RowLayout {
                            visible: !root.maskMode; Layout.fillWidth: true; spacing: 8
                            FieldLabel { text: "排除"; Layout.alignment: Qt.AlignTop; Layout.topMargin: 7 }
                            InputArea { Layout.fillWidth: true; implicitHeight: 56; radius: 6; label: "负面提示词"; value: root.doc.negativePrompt; placeholder: "不希望出现的内容（可选）"; onEdited: text=>root.doc.negativePrompt=text }
                        }
                        RowLayout {
                            visible: !root.maskMode; Layout.fillWidth: true; spacing: 8
                            FieldLabel { text: "类别" }
                            InspectorField { Layout.fillWidth: true; Accessible.name: "布局类别"; text: root.doc.layoutMeta; placeholderText: "poster, illustration"; onEditingFinished: root.doc.layoutMeta=text }
                        }
                        HomeButton { visible: !root.maskMode; Layout.alignment: Qt.AlignRight; text: "布局 JSON…"; compact: true; quiet: true; onClicked: {layoutJson.value=root.doc.inputPreview;jsonDialog.open()} }
                    }
                }
            }
        }
    }
    HomeDialog {
        id: jsonDialog; objectName: "layoutJsonDialog"; modal: true; title: "布局 JSON"; width: Math.min(640,Overlay.overlay.width-60); height: Math.min(520,Overlay.overlay.height-80); anchors.centerIn: Overlay.overlay
        onOpened: {try{layoutJson.value=JSON.stringify(JSON.parse(root.doc.inputPreview),null,2)}catch(error){layoutJson.value=root.doc.inputPreview}jsonError.text=""}
        ColumnLayout {
            anchors.fill: parent
            InputArea { id: layoutJson; objectName: "layoutJsonInput"; Layout.fillWidth: true; Layout.fillHeight: true; label: "布局 JSON"; font.family: "Menlo"; font.pixelSize: 11; radius: 8; onEdited: text=>value=text }
            Text { id: jsonError; Layout.fillWidth: true; wrapMode: Text.Wrap; color: HomeTheme.danger; font.pixelSize: 12 }
            RowLayout { Layout.fillWidth: true; Item { Layout.fillWidth: true } HomeButton { text: "关闭"; quiet: true; onClicked: jsonDialog.close() } HomeButton { text: "导入布局"; primary: true; onClicked: {const error=root.doc.importLayout(layoutJson.value,root.doc.canvasWidth,root.doc.canvasHeight);jsonError.text=error;if(!error){root.thread.draft=root.doc.prompt;jsonDialog.close()}} } }
        }
    }
}
