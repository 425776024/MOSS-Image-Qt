import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import MossStudio

RowLayout {
    id: root
    property var edit: studio.editDocument
    property int viewMode: 1
    property string tool: "brush"
    property string observedResult: ""
    spacing: 0
    Connections {
        target: root.edit
        function onChanged() {
            if (root.observedResult !== root.edit.resultUrl) {
                root.observedResult = root.edit.resultUrl
                root.viewMode = root.edit.resultUrl !== "" ? 2 : 1
            }
        }
    }
    Shortcut { sequences: [StandardKey.Undo]; enabled: root.visible && !editPrompt.activeFocus && !studio.busy; onActivated: root.edit.undo() }
    Shortcut { sequences: [StandardKey.Redo]; enabled: root.visible && !editPrompt.activeFocus && !studio.busy; onActivated: root.edit.redo() }
    FileDialog {
        id: importDialog; title: "导入需要修改的图片"
        nameFilters: ["图片 (*.png *.jpg *.jpeg *.webp)"]
        onAccepted: { studio.importEditImage(selectedFile); root.viewMode = 1 }
    }
    FileDialog {
        id: exportDialog; title: "导出重绘结果"; fileMode: FileDialog.SaveFile
        nameFilters: ["PNG 图片 (*.png)"]
        onAccepted: studio.exportEditResult(selectedFile)
    }
    Rectangle {
        Layout.preferredWidth: 290; Layout.fillHeight: true; color: StudioTheme.panel
        ScrollView {
            anchors.fill: parent; anchors.margins: 18; anchors.bottomMargin: studio.busy ? 112 : 70
            contentWidth: availableWidth; clip: true
            ColumnLayout {
                width: parent.width; spacing: 15
                RowLayout {
                    Layout.fillWidth: true
                    StudioButton { Layout.fillWidth: true; text: "导入图片"; enabled: !studio.busy; onClicked: importDialog.open() }
                    StudioButton { text: "使用生成图"; enabled: !studio.busy && studio.imageUrl !== ""; onClicked: { studio.editGeneratedImage(); root.viewMode = 1 } }
                }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.Wrap; color: StudioTheme.muted; font.pixelSize: 11
                    text: root.edit.ready ? root.edit.imageWidth + " × " + root.edit.imageHeight + " · 原图尺寸" : "支持 PNG、JPEG、WebP，宽高不超过 2048 像素"
                }
                Text { text: "修改后的画面"; color: StudioTheme.text; font.weight: Font.DemiBold; font.pixelSize: 14 }
                Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: 175; radius: 7; color: StudioTheme.field
                    border.color: editPrompt.activeFocus ? StudioTheme.accent : StudioTheme.line
                    ScrollView {
                        anchors.fill: parent; anchors.margins: 5; clip: true
                        TextArea {
                            id: editPrompt; objectName: "editPrompt"; text: root.edit.prompt
                            enabled: !studio.busy; wrapMode: TextArea.Wrap; selectByMouse: true
                            font.pixelSize: 13; color: StudioTheme.text; placeholderTextColor: StudioTheme.muted
                            placeholderText: "例如：蓝色机器人旁是一个红色方块，右侧有绿色盆栽，柔和的摄影棚光线。"
                            onTextChanged: if (activeFocus) root.edit.prompt = text
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true; text: "描述希望得到的内容；复杂替换可提高重绘强度。"
                    wrapMode: Text.Wrap; font.pixelSize: 11; color: StudioTheme.muted; lineHeight: 1.4
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: StudioTheme.line }
                StudioCombo {
                    Layout.fillWidth: true; model: ["羽化贴合", "直接贴合"]
                    currentIndex: root.edit.blendMode === "direct" ? 1 : 0
                    enabled: !studio.busy; Accessible.name: "重绘贴合模式"
                    onActivated: root.edit.blendMode = currentIndex === 1 ? "direct" : "feather"
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "重绘强度"; color: StudioTheme.secondary; font.pixelSize: 12 }
                    Item { Layout.fillWidth: true }
                    Text { text: Math.round(root.edit.strength * 100) + "%"; color: StudioTheme.accent; font.pixelSize: 12 }
                }
                StudioSlider { Layout.fillWidth: true; from: 0.1; to: 1; stepSize: 0.05; value: root.edit.strength; enabled: !studio.busy; onMoved: root.edit.strength = value }
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "边缘过渡"; color: StudioTheme.secondary; font.pixelSize: 12 }
                    Item { Layout.fillWidth: true }
                    StudioSpinBox { from: 0; to: 64; value: root.edit.feather; enabled: !studio.busy && root.edit.blendMode !== "direct"; onValueModified: root.edit.feather = value }
                    Text { text: "px"; color: StudioTheme.muted; font.pixelSize: 11 }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "采样步数"; color: StudioTheme.secondary; font.pixelSize: 12 }
                    Item { Layout.fillWidth: true }
                    StudioSpinBox { from: 1; to: 100; value: studio.steps; editable: true; enabled: !studio.busy; onValueModified: studio.steps = value }
                }
                Text {
                    Layout.fillWidth: true; text: root.edit.blendMode === "direct" ? "直接采用模型输出，不贴回原图；选区外可能有细微变化。" : "选区内重绘，选区外保留原图。边缘过渡只发生在选区内。"
                    wrapMode: Text.Wrap; lineHeight: 1.5; font.pixelSize: 11; color: StudioTheme.muted
                }
                StudioButton {
                    Layout.fillWidth: true; text: "以结果继续修改"; enabled: !studio.busy && root.edit.resultUrl !== ""
                    onClicked: { studio.useEditResult(); root.viewMode = 1 }
                }
                StudioButton { Layout.fillWidth: true; text: "导出重绘结果"; enabled: root.edit.resultUrl !== ""; onClicked: exportDialog.open() }
                Item { height: 10 }
            }
        }
        ColumnLayout {
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 18; spacing: 8
            StudioButton {
                objectName: "generateEdit"; Layout.fillWidth: true; implicitHeight: 44; primary: true
                text: studio.busy ? "正在处理…" : "✦  重绘选区"
                enabled: !studio.busy && root.edit.hasMask && root.edit.prompt.trim().length > 0
                onClicked: { root.forceActiveFocus(); studio.generateEdit() }
            }
            StudioButton { Layout.fillWidth: true; visible: studio.busy; text: "取消任务"; onClicked: studio.cancel() }
        }
        Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: StudioTheme.line }
    }
    ColumnLayout {
        Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
        Rectangle {
            Layout.fillWidth: true; height: 54; color: StudioTheme.field
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 5
                Repeater {
                    model: [{title:"画笔",key:"brush"},{title:"矩形",key:"rectangle"},{title:"擦除",key:"erase"}]
                    StudioButton {
                        required property var modelData
                        text: modelData.title; active: root.tool === modelData.key && root.viewMode === 1
                        enabled: root.edit.ready && !studio.busy
                        onClicked: { root.tool = modelData.key; root.viewMode = 1 }
                    }
                }
                StudioSlider { id: brushSize; Layout.preferredWidth: 90; from: 8; to: 200; value: 48; stepSize: 2; enabled: root.tool !== "rectangle" && !studio.busy }
                Text { text: Math.round(brushSize.value) + " px"; color: StudioTheme.muted; font.pixelSize: 10 }
                Item { Layout.fillWidth: true }
                StudioButton { text: "撤销"; enabled: root.edit.canUndo && !studio.busy; onClicked: root.edit.undo() }
                StudioButton { text: "重做"; enabled: root.edit.canRedo && !studio.busy; onClicked: root.edit.redo() }
                StudioButton { text: "清空"; enabled: root.edit.hasMask && !studio.busy; onClicked: root.edit.clearMask() }
            }
        }
        Item {
            Layout.fillWidth: true; Layout.fillHeight: true
            MaskCanvas {
                id: maskCanvas; objectName: "maskCanvas"; anchors.fill: parent; document: root.edit
                tool: root.tool; brushSize: brushSize.value
                showResult: root.viewMode === 2; showMask: root.viewMode === 1
                enabled: !studio.busy && root.viewMode === 1
            }
            Column {
                anchors.centerIn: parent; spacing: 16; visible: !root.edit.ready
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: "只改变你选中的部分"; font.pixelSize: 25; color: StudioTheme.secondary }
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: "导入图片，然后涂抹或画框选择需要修改的区域"; font.pixelSize: 13; color: StudioTheme.muted }
                StudioButton { anchors.horizontalCenter: parent.horizontalCenter; primary: true; text: "导入图片"; onClicked: importDialog.open() }
            }
        }
        Rectangle {
            Layout.fillWidth: true; height: 60; color: StudioTheme.field
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 15; anchors.rightMargin: 15
                Text { Layout.fillWidth: true; text: root.viewMode === 1 ? "紫色区域将被重绘" : root.viewMode === 0 ? "原始图片" : "重绘结果"; color: StudioTheme.muted; font.pixelSize: 11 }
                StudioButton { text: "原图"; active: root.viewMode === 0; enabled: root.edit.ready; onClicked: root.viewMode = 0 }
                StudioButton { text: "选区"; active: root.viewMode === 1; enabled: root.edit.ready; onClicked: root.viewMode = 1 }
                StudioButton { text: "结果"; active: root.viewMode === 2; enabled: root.edit.resultUrl !== ""; onClicked: root.viewMode = 2 }
            }
        }
    }
}
