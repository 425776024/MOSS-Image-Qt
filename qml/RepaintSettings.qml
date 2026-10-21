import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Rectangle {
    id: root
    color: StudioTheme.panel
    property var edit: studio.editDocument
    FileDialog {
        id: sourceDialog; title: "选择局部重绘原图"
        nameFilters: ["图片 (*.png *.jpg *.jpeg *.webp)"]
        onAccepted: studio.importEditImage(selectedFile)
    }
    ScrollView {
        anchors.fill: parent; anchors.margins: 12; contentWidth: availableWidth; clip: true
        ColumnLayout {
            width: parent.width; spacing: 10; enabled: !studio.busy
            Text { text: "重绘原图"; font.pixelSize: 14; font.weight: Font.DemiBold; color: StudioTheme.text }
            RowLayout {
                Layout.fillWidth: true
                StudioButton { Layout.fillWidth: true; text: root.edit.ready ? "更换图片" : "导入图片"; iconName: "folder"; onClicked: sourceDialog.open() }
                StudioButton { Layout.fillWidth: true; text: "使用本页生成图"; enabled: studio.creationImageUrl !== ""; onClicked: studio.editGeneratedImage() }
            }
            Text { Layout.fillWidth: true; text: root.edit.ready ? "更换原图会清空选区和本次结果，生成内容仍保留。" : "支持宽、高均不超过 2048 px 的图片。"; wrapMode: Text.Wrap; font.pixelSize: 11; color: StudioTheme.muted }
            Rectangle { Layout.fillWidth: true; height: 1; color: StudioTheme.line }
            Text { text: "希望选区变成什么？"; font.pixelSize: 14; font.weight: Font.DemiBold; color: StudioTheme.text }
            InputArea {
                objectName: "repaintPrompt"; Layout.fillWidth: true; Layout.preferredHeight: 128
                label: "重绘描述"; value: root.edit.prompt
                placeholder: "例如：将外套改为深绿色，保留原来的褶皱和光影…"
                onEdited: text => root.edit.prompt = text
            }
            Text { Layout.fillWidth: true; text: root.edit.blendMode === "direct" ? "直接采用模型输出，不贴回原图；选区外可能有细微变化。" : "画笔或框选指定修改范围；选区以外保留原图。"; wrapMode: Text.Wrap; font.pixelSize: 11; color: StudioTheme.muted }
            StudioCombo {
                objectName: "repaintBlendMode"; Layout.fillWidth: true
                model: ["羽化贴合", "直接贴合"]
                currentIndex: root.edit.blendMode === "direct" ? 1 : 0
                Accessible.name: "重绘贴合模式"
                onActivated: root.edit.blendMode = currentIndex === 1 ? "direct" : "feather"
            }
            RowLayout {
                Layout.fillWidth: true
                Text { text: "重绘强度"; color: StudioTheme.secondary; font.pixelSize: 12 }
                Item { Layout.fillWidth: true }
                Text { text: Math.round(root.edit.strength * 100) + "%"; color: StudioTheme.accent; font.pixelSize: 12 }
            }
            StudioSlider { Layout.fillWidth: true; from: .1; to: 1; stepSize: .05; value: root.edit.strength; onMoved: root.edit.strength = value; Accessible.name: "重绘强度" }
            RowLayout {
                Layout.fillWidth: true
                Text { text: "边缘过渡"; color: StudioTheme.secondary; font.pixelSize: 12 }
                Item { Layout.fillWidth: true }
                StudioSpinBox { from: 0; to: 64; value: root.edit.feather; enabled: root.edit.blendMode !== "direct"; onValueModified: root.edit.feather = value; Accessible.name: "边缘过渡像素" }
                Text { text: "px"; color: StudioTheme.muted; font.pixelSize: 11 }
            }
            RowLayout {
                Layout.fillWidth: true
                Text { text: "采样步数"; color: StudioTheme.secondary; font.pixelSize: 12 }
                Item { Layout.fillWidth: true }
                StudioSpinBox { from: 1; to: 100; value: studio.steps; onValueModified: studio.steps = value; Accessible.name: "重绘采样步数" }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: StudioTheme.line }
            StudioButton { Layout.fillWidth: true; text: "以重绘结果继续修改"; iconName: "brush"; enabled: root.edit.resultUrl !== ""; onClicked: studio.useEditResult() }
            Text { Layout.fillWidth: true; text: "本页自动保存原图、选区、描述和结果。切回图像生成可继续编辑原始创作。"; wrapMode: Text.Wrap; font.pixelSize: 11; color: StudioTheme.muted; lineHeight: 1.4 }
        }
    }
}
