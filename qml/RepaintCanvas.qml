import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MossStudio

Item {
    id: root
    property var edit: studio.editDocument
    property Item toolbarHost
    property Item footerHost
    property Item floatingHost
    property Item backdropSource
    property string tool: "brush"
    property int viewMode: edit.resultUrl !== "" ? 2 : 1
    property string observedSource: edit.sourcePath
    property string observedResult: edit.resultUrl
    Connections {
        target: root.edit
        function onChanged() {
            if (root.observedSource !== root.edit.sourcePath || root.observedResult !== root.edit.resultUrl) {
                root.observedSource = root.edit.sourcePath
                root.observedResult = root.edit.resultUrl
                root.viewMode = root.edit.resultUrl !== "" ? 2 : 1
            }
        }
    }
    Item {
        objectName: "repaintToolbar"
        parent: root.toolbarHost
        anchors.fill: parent
        visible: root.visible
        RowLayout {
            anchors.fill: parent; spacing: 4
            StudioButton { objectName: "repaintBrush"; text: HomeTheme.t("画笔"); iconName: "brush"; active: root.tool === "brush"; quiet: true; enabled: root.edit.ready && !studio.busy; onClicked: { root.tool = "brush"; root.viewMode = 1 } }
            StudioButton { objectName: "repaintRectangle"; text: HomeTheme.t("框选"); iconName: "rectangle"; active: root.tool === "rectangle"; quiet: true; enabled: root.edit.ready && !studio.busy; onClicked: { root.tool = "rectangle"; root.viewMode = 1 } }
            StudioButton { objectName: "repaintEraser"; text: HomeTheme.t("擦除"); iconName: "eraser"; active: root.tool === "erase"; quiet: true; enabled: root.edit.ready && !studio.busy; onClicked: { root.tool = "erase"; root.viewMode = 1 } }
            Item { Layout.fillWidth: true }
            StudioButton { objectName: "repaintUndo"; iconName: "undo"; hint: HomeTheme.t("撤销选区"); quiet: true; enabled: root.edit.canUndo && !studio.busy; onClicked: { root.forceActiveFocus(); root.edit.undo(); root.viewMode = 1 } }
            StudioButton { objectName: "repaintRedo"; iconName: "redo"; hint: HomeTheme.t("重做选区"); quiet: true; enabled: root.edit.canRedo && !studio.busy; onClicked: { root.forceActiveFocus(); root.edit.redo(); root.viewMode = 1 } }
            StudioButton { objectName: "repaintClear"; text: HomeTheme.t("清空"); hint: HomeTheme.t("清空选区"); quiet: true; enabled: root.edit.hasMask && !studio.busy; onClicked: { root.edit.clearMask(); root.viewMode = 1 } }
        }
    }
    Item {
        anchors.fill: parent
        MaskCanvas {
            anchors.fill: parent; document: root.edit
            tool: root.tool; brushSize: brush.value
            showResult: root.viewMode === 2; showMask: root.viewMode === 1
            enabled: !studio.busy && root.viewMode === 1
            Accessible.name: HomeTheme.t("局部重绘画布")
        }
        Column {
            visible: !root.edit.ready
            anchors.centerIn: parent; spacing: 14
            StudioIcon { anchors.horizontalCenter: parent.horizontalCenter; name: "brush"; width: 36; height: 36; ink: StudioTheme.muted }
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: HomeTheme.t("让局部更接近你的想法"); font.pixelSize: 18; color: StudioTheme.secondary }
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: HomeTheme.t("导入图片，或先在本页生成图像"); font.pixelSize: 12; color: StudioTheme.muted }
        }
        FrostedPanel {
            objectName: "repaintBrushControls"
            parent: root.floatingHost
            visible: root.visible && root.edit.ready && root.viewMode === 1
            anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottomMargin: 12; width: 232; height: 38; radius: 8
            backdropSource: root.backdropSource
            RowLayout {
                anchors.fill: parent; anchors.margins: 8; spacing: 10
                Text { text: root.tool === "rectangle" ? HomeTheme.t("拖动画出选区") : HomeTheme.t("笔刷"); color: StudioTheme.secondary; font.pixelSize: 11 }
                StudioSlider { id: brush; visible: root.tool !== "rectangle"; Layout.fillWidth: true; from: 8; to: 200; value: 48; enabled: !studio.busy; Accessible.name: HomeTheme.t("笔刷大小") }
                Text { visible: root.tool !== "rectangle"; text: Math.round(brush.value) + " px"; color: StudioTheme.secondary; font.pixelSize: 10 }
                Text { visible: root.tool === "rectangle"; text: HomeTheme.t("绿色区域将被重绘"); color: StudioTheme.secondary; font.pixelSize: 10 }
            }
        }
    }
    FrostedPanel {
        objectName: "repaintViewControls"
        parent: root.footerHost
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom; anchors.bottomMargin: 12
        width: Math.min(parent.width - 40, viewControls.implicitWidth + 32); height: 40
        visible: root.visible; z: 20
        backdropSource: root.backdropSource; radius: 10
        RowLayout {
            id: viewControls
            anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 4
            StudioButton { objectName: "repaintOriginalView"; text: HomeTheme.t("原图"); implicitHeight: 28; active: root.viewMode === 0; quiet: true; enabled: root.edit.ready; onClicked: root.viewMode = 0 }
            StudioButton { objectName: "repaintMaskView"; text: HomeTheme.t("选区"); implicitHeight: 28; active: root.viewMode === 1; quiet: true; enabled: root.edit.ready; onClicked: root.viewMode = 1 }
            StudioButton { objectName: "repaintResultView"; text: HomeTheme.t("结果"); implicitHeight: 28; active: root.viewMode === 2; quiet: true; enabled: root.edit.resultUrl !== ""; onClicked: root.viewMode = 2 }
            Item { Layout.fillWidth: true }
            Text { visible: root.edit.ready; text: root.edit.imageWidth + " × " + root.edit.imageHeight; color: StudioTheme.muted; font.pixelSize: 10 }
        }
    }
}
