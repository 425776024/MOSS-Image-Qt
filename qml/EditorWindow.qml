import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: win
    objectName: "editorWindow"
    width: 1440; height: 900; minimumWidth: 1180; minimumHeight: 740
    visible: false; topPadding: 0; transientParent: null
    title: HomeTheme.t("新身数字人 · ") + (creationMode === "av" ? HomeTheme.t("音视频创作") : HomeTheme.t("图像创作"))
    color: StudioTheme.background; font.family: "PingFang SC"; font.pixelSize: 13
    property string creationMode: "image"
    property int page: 0
    property bool navCollapsed: true
    property bool showLog: false
    signal homeRequested(string destination)
    function selectMode(value) { if(value !== "image" && value !== "av")return; contentItem.forceActiveFocus(); page = 0; creationMode = value }
    function openInspector(context) { selectMode(["speech","voice","sfx","background"].includes(context) ? "av" : "image"); if(context !== "image")creation.openInspector(context) }
    function commitWorkspace() { creation.forceActiveFocus(); studio.flushWorkspace() }
    onVisibleChanged: if(!visible)creation.suspendPreview()
    onClosing: { commitWorkspace(); homeRequested("works") }
    onPageChanged: {
        if(page === 3){homeRequested("models");page = 0}
        else if(page === 2){homeRequested("works");page = 0}
        else if(page === 4){creationMode = "image";studio.repainting = true;page = 0}
        else if(page === 1){creationMode = "av";page = 0;creation.openInspector("speech")}
    }
    palette.highlight: StudioTheme.accent; palette.highlightedText: StudioTheme.accentInk
    palette.window: StudioTheme.panel; palette.windowText: StudioTheme.text; palette.text: StudioTheme.text
    palette.base: StudioTheme.field; palette.button: StudioTheme.elevated; palette.buttonText: StudioTheme.text
    palette.light: StudioTheme.hover; palette.mid: StudioTheme.line; palette.dark: StudioTheme.background; palette.placeholderText: StudioTheme.faint
    Shortcut { sequences: [StandardKey.Save]; enabled: win.visible; onActivated: saveDialog.open() }
    FileDialog { id: saveDialog; title: HomeTheme.t("保存工程"); fileMode: FileDialog.SaveFile; nameFilters: [HomeTheme.t("Moss 工程 (*.moss.json)")]; onAccepted: studio.saveProject(selectedFile) }
    FileDialog { id: exportDialog; title: HomeTheme.t("导出整页视频"); fileMode: FileDialog.SaveFile; nameFilters: [HomeTheme.t("MP4 视频 (*.mp4)")]; onAccepted: studio.exportTimeline(selectedFile) }
    ColumnLayout {
        anchors.fill: parent; spacing: 0
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 54; color: StudioTheme.chrome
            MouseArea { anchors.fill: parent; onPressed: win.startSystemMove(); onDoubleClicked: win.visibility === Window.Maximized ? win.showNormal() : win.showMaximized() }
            RowLayout {
                x: Qt.platform.os === "osx" ? 84 : 14; anchors.verticalCenter: parent.verticalCenter; spacing: 10
                StudioButton { iconName: "sidebar"; quiet: true; hint: HomeTheme.t("保存并返回主页"); onClicked: {win.commitWorkspace();win.homeRequested("works")} }
                Text { text: "Moss"; color: StudioTheme.accent; font.pixelSize: 19; font.weight: Font.DemiBold }
                Rectangle { width: 1; height: 18; color: StudioTheme.line }
                Text { text: HomeTheme.t("创作工作台"); color: StudioTheme.secondary; font.pixelSize: 12 }
            }
            Rectangle {
                anchors.centerIn: parent; width: 228; height: 38; radius: 7; color: StudioTheme.field; border.color: StudioTheme.line
                RowLayout { anchors.fill: parent; anchors.margins: 3; spacing: 3
                    StudioButton { objectName: "imageModeTab"; Layout.fillWidth: true; Layout.fillHeight: true; text: HomeTheme.t("图像"); iconName: "image"; quiet: true; active: win.creationMode === "image"; Accessible.role: Accessible.PageTab; Accessible.selected: active; onClicked: win.selectMode("image") }
                    StudioButton { objectName: "avModeTab"; Layout.fillWidth: true; Layout.fillHeight: true; text: HomeTheme.t("音视频"); iconName: "play"; quiet: true; active: win.creationMode === "av"; Accessible.role: Accessible.PageTab; Accessible.selected: active; onClicked: win.selectMode("av") }
                }
            }
            RowLayout {
                anchors.right: parent.right; anchors.rightMargin: 14; anchors.verticalCenter: parent.verticalCenter; spacing: 8
                StudioButton { text: HomeTheme.t("主题"); iconName: "background"; quiet: true; onClicked: themeMenu.popup()
                    Menu { id: themeMenu
                        MenuItem { text: HomeTheme.t("跟随系统"); checkable: true; checked: workspace.appearanceMode === "system"; onTriggered: workspace.appearanceMode = "system" }
                        MenuItem { text: HomeTheme.t("浅色"); checkable: true; checked: workspace.appearanceMode === "light"; onTriggered: workspace.appearanceMode = "light" }
                        MenuItem { text: HomeTheme.t("深色"); checkable: true; checked: workspace.appearanceMode === "dark"; onTriggered: workspace.appearanceMode = "dark" }
                        MenuSeparator { }
                        MenuItem { text: HomeTheme.t("深空"); checkable: true; checked: workspace.themePreset === "dark"; onTriggered: workspace.themePreset = "dark" }
                        MenuItem { text: HomeTheme.t("石墨"); checkable: true; checked: workspace.themePreset === "graphite"; onTriggered: workspace.themePreset = "graphite" }
                        MenuItem { text: HomeTheme.t("青绿"); checkable: true; checked: workspace.themePreset === "teal"; onTriggered: workspace.themePreset = "teal" }
                        MenuItem { text: HomeTheme.t("蓝色"); checkable: true; checked: workspace.themePreset === "blue"; onTriggered: workspace.themePreset = "blue" }
                    }
                }
                StudioButton { iconName: "save"; text: HomeTheme.t("保存"); quiet: true; onClicked: saveDialog.open() }
                StudioButton { iconName: "more"; quiet: true; hint: HomeTheme.t("更多操作"); onClicked: moreMenu.popup()
                    Menu { id: moreMenu
                        MenuItem { visible: win.creationMode === "image"; text: HomeTheme.t("打开图层管理"); onTriggered: creation.openInspector("object") }
                        MenuItem { visible: win.creationMode === "image"; text: HomeTheme.t("独立图层工程"); enabled: !studio.busy; onTriggered: win.page = 5 }
                        MenuItem { visible: win.creationMode === "av"; text: HomeTheme.t("生成本页音视频"); enabled: !studio.busy; onTriggered: studio.generatePageVideo() }
                        MenuItem { text: HomeTheme.t("模型与计算设置"); onTriggered: win.homeRequested("models") }
                    }
                }
                StudioButton { objectName: "exportTimeline"; text: win.creationMode === "image" ? HomeTheme.t("导出图像") : HomeTheme.t("导出"); iconName: "export"; primary: true; implicitHeight: 32; leftPadding: 14; rightPadding: 14; enabled: !studio.busy && (win.creationMode === "av" || studio.hasCanvasContent)
                    onClicked: if(win.creationMode === "image")creation.exportAsset("image"); else exportMenu.popup()
                    Menu { id: exportMenu
                        MenuItem { text: HomeTheme.t("整页视频 · MP4"); onTriggered: exportDialog.open() }
                        MenuItem { text: HomeTheme.t("配音 / 数字人原始素材"); enabled: studio.timeline.tracks.some(t => t.key === "speech" && !!t.source); onTriggered: creation.exportAsset("speech") }
                        MenuItem { text: HomeTheme.t("音效 · WAV"); enabled: studio.timeline.tracks.some(t => t.key === "sfx" && !!t.source); onTriggered: creation.exportAsset("sfx") }
                    }
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: StudioTheme.line }
        }
        Rectangle {
            visible: studio.error !== ""; Layout.fillWidth: true; Layout.preferredHeight: 42; color: Qt.alpha(StudioTheme.danger, .10)
            RowLayout { anchors.fill: parent; anchors.margins: 8
                Text { Layout.fillWidth: true; text: HomeTheme.t(studio.error); elide: Text.ElideRight; color: StudioTheme.danger; font.pixelSize: 12 }
                StudioButton { text: HomeTheme.t("查看日志"); onClicked: win.showLog = true }
                StudioButton { text: HomeTheme.t("关闭"); onClicked: studio.dismissError() }
            }
        }
        StackLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; currentIndex: win.page === 5 ? 1 : 0
            CreationPage { id: creation; audiovisual: win.creationMode === "av"; onModeRequested: mode => win.selectMode(mode); onNavigate: page => win.page = page }
            CompositionPage { onEditReady: {win.selectMode("image");win.page = 4} }
        }
        Rectangle {
            visible: win.showLog; Layout.fillWidth: true; Layout.preferredHeight: 150; color: StudioTheme.field
            ScrollView { anchors.fill: parent; anchors.margins: 10; clip: true
                TextArea { readOnly: true; selectByMouse: true; text: studio.log; wrapMode: TextEdit.Wrap; font.family: "Menlo"; font.pixelSize: 10; color: StudioTheme.secondary }
            }
        }
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 26; color: StudioTheme.chrome
            RowLayout { anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 12; spacing: 12
                Rectangle { width: 5; height: 5; radius: 3; color: studio.busy ? StudioTheme.accent : StudioTheme.faint }
                Text { Layout.fillWidth: true; text: HomeTheme.t(studio.status); color: StudioTheme.secondary; font.pixelSize: 11; elide: Text.ElideRight }
                Text { text: win.creationMode === "image" ? HomeTheme.t("图像模式") : HomeTheme.t("音视频模式 · 共享合成图像"); color: StudioTheme.muted; font.pixelSize: 11 }
                ProgressBar { visible: studio.busy; Layout.preferredWidth: 130; value: studio.progress; indeterminate: studio.progress === 0 }
                StudioButton { text: win.showLog ? HomeTheme.t("收起日志") : HomeTheme.t("运行日志"); quiet: true; implicitHeight: 22; onClicked: win.showLog = !win.showLog }
            }
        }
    }
}
