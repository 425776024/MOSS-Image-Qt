import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

FrostedPanel {
    id: root
    tintOpacity: 0.82
    implicitWidth: 136
    implicitHeight: 44 + list.contentHeight + 28 + (studio.canRestorePage ? 42 : 0)
    signal selected()
    function commitFocus() { root.forceActiveFocus() }
    // Updating the list destroys its delegates. Keep the complete action in
    // the persistent rail so the post-switch signal has a living QML context.
    function openPage(index) { commitFocus(); studio.selectPage(index); selected() }
    function copyPage(index) { commitFocus(); studio.duplicatePage(index); selected() }
    function removePage(index) { commitFocus(); studio.deletePage(index); selected() }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 1; spacing: 0
        RowLayout {
            Layout.fillWidth: true; Layout.preferredHeight: 44
            Layout.leftMargin: 12; Layout.rightMargin: 8
            Text { text: HomeTheme.t("页面"); color: StudioTheme.text; font.pixelSize: 12; font.weight: Font.DemiBold }
            Text { text: studio.creationPages.length; color: StudioTheme.muted; font.pixelSize: 10 }
            Item { Layout.fillWidth: true }
            StudioButton {
                objectName: "newCreationPage"; iconName: "plus"; quiet: true
                hint: HomeTheme.t("新建空白创作页"); enabled: !studio.busy
                onClicked: { root.commitFocus(); studio.newPage(); root.selected() }
            }
        }
        ListView {
            id: list
            objectName: "creationPages"
            Layout.fillWidth: true; Layout.fillHeight: true
            leftMargin: 10; rightMargin: 10; topMargin: 2; bottomMargin: 8
            spacing: 12; clip: true; model: studio.creationPages
            currentIndex: studio.currentPage
            function revealCurrentPage() {
                Qt.callLater(function() { list.positionViewAtIndex(list.currentIndex, ListView.Contain) })
            }
            onCurrentIndexChanged: revealCurrentPage()
            onHeightChanged: revealCurrentPage()
            Component.onCompleted: revealCurrentPage()
            ScrollBar.vertical: ScrollBar { width: 5 }
            delegate: Column {
                id: card
                required property var modelData
                required property int index
                property var document: modelData.document
                property string preview: modelData.visual || modelData.image || ""
                property bool isVideo: !!modelData.video
                property bool selected: studio.currentPage === index
                width: list.width - 20; spacing: 6
                AbstractButton {
                    id: pageButton
                    objectName: "creationPage" + card.index
                    width: parent.width; height: width; hoverEnabled: true
                    enabled: !studio.busy
                    Accessible.name: HomeTheme.t("创作页 ") + (card.index + 1) + "，" + (card.document.prompt || (card.preview ? HomeTheme.t("导入画面") : HomeTheme.t("空白画布")))
                    background: Rectangle {
                        radius: 9; color: card.selected ? StudioTheme.selection : StudioTheme.background
                        border.color: card.selected ? StudioTheme.accent : pageButton.hovered ? StudioTheme.muted : StudioTheme.line
                        border.width: 1
                    }
                    contentItem: Item {
                        Rectangle {
                            id: paper
                            anchors.centerIn: parent
                            width: Math.min(parent.width - 10, (parent.height - 10) * card.document.width / card.document.height)
                            height: width * card.document.height / card.document.width
                            color: card.preview || card.document.layout ? "#F3F1EA" : StudioTheme.elevated
                            Image { anchors.fill: parent; source: card.preview || ""; fillMode: Image.PreserveAspectFit; asynchronous: true }
                            Repeater {
                                model: !card.preview && card.document.layout ? card.document.regions : []
                                Rectangle {
                                    required property var modelData
                                    x: modelData.x / card.document.width * paper.width
                                    y: modelData.y / card.document.height * paper.height
                                    width: modelData.width / card.document.width * paper.width
                                    height: modelData.height / card.document.height * paper.height
                                    color: Qt.alpha(modelData.color, 0.15); border.color: modelData.color
                                }
                            }
                            StudioIcon { anchors.centerIn: parent; visible: !card.preview && !card.document.layout; name: "image"; width: 25; height: 25; ink: StudioTheme.faint }
                        }
                        StudioButton {
                            anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 4
                            visible: pageButton.hovered || hovered || pageMenu.opened
                            iconName: "more"; implicitWidth: 30; implicitHeight: 26; hint: HomeTheme.t("页面操作")
                            onClicked: pageMenu.popup()
                        }
                    }
                    onClicked: root.openPage(card.index)
                    MouseArea { anchors.fill: parent; acceptedButtons: Qt.RightButton; onClicked: pageMenu.popup() }
                    Menu {
                        id: pageMenu
                        MenuItem { text: HomeTheme.t("复制此页"); onTriggered: root.copyPage(card.index) }
                        MenuItem { text: HomeTheme.t("删除此页"); onTriggered: root.removePage(card.index) }
                    }
                }
                RowLayout {
                    width: parent.width; spacing: 7
                    Text { text: String(card.index + 1).padStart(2, "0"); color: card.selected ? StudioTheme.accent : StudioTheme.faint; font.pixelSize: 10; font.family: "Menlo" }
                    Text {
                        Layout.fillWidth: true; text: card.document.prompt ? card.document.prompt.replace(/\s+/g, " ") : card.modelData.edit && card.modelData.edit.prompt ? card.modelData.edit.prompt : card.preview ? HomeTheme.t("导入画面") : HomeTheme.t("空白画布")
                        elide: Text.ElideRight; color: card.selected ? StudioTheme.text : StudioTheme.secondary; font.pixelSize: 11
                    }
                }
            }
        }
        StudioButton {
            visible: studio.canRestorePage; Layout.fillWidth: true; Layout.leftMargin: 10; Layout.rightMargin: 10; Layout.bottomMargin: 10
            text: HomeTheme.t("撤销删除"); iconName: "undo"; enabled: !studio.busy
            onClicked: { root.commitFocus(); studio.restoreDeletedPage(); root.selected() }
        }
        Text {
            Layout.alignment: Qt.AlignHCenter; Layout.bottomMargin: 10; Layout.topMargin: 6
            text: HomeTheme.t("第 ") + (studio.currentPage + 1) + " / " + studio.creationPages.length + HomeTheme.t(" 页"); color: StudioTheme.faint; font.pixelSize: 10
        }
    }
}
