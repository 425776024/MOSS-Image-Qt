import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

HomeDialog {
    id: root
    property string severity: "info"
    property string message: ""
    property string detail: ""
    property string filePath: ""
    property bool detailsExpanded: false
    readonly property string fileName: filePath.split("/").pop()
    readonly property string directory: filePath.slice(0,filePath.lastIndexOf("/"))
    readonly property bool notification: severity === "success"
    title: message
    modal: !notification
    focus: !notification
    showCloseButton: notification
    anchors.centerIn: notification ? undefined : Overlay.overlay
    width: Math.min(notification ? 320 : 440,Overlay.overlay ? Overlay.overlay.width-40 : 440)
    x: parent ? parent.width-width-20 : 0
    y: parent ? parent.height-height-20 : 0
    closePolicy: notification ? Popup.CloseOnEscape | Popup.CloseOnPressOutside : Popup.CloseOnEscape
    defaultAction: notification ? null : done
    dialogIcon: severity === "success" ? "check" : severity === "error" ? "info" : "info"
    tone: severity === "error" ? HomeTheme.danger : HomeTheme.accent
    onOpened: {detailsExpanded=false;if(!notification)done.forceActiveFocus()}
    Timer { interval: 6000; running: root.opened && root.notification; onTriggered: root.close() }
    ColumnLayout {
        width: parent.width; spacing: 14
        Rectangle {
            visible: root.filePath.length>0 && !root.notification
            Layout.fillWidth: true
            implicitHeight: fileRow.implicitHeight+24
            radius: 8; color: HomeTheme.dialogInset
            RowLayout {
                id: fileRow
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
                spacing: 10
                StudioIcon { name: "document"; ink: HomeTheme.accent; Layout.preferredWidth: 24; Layout.preferredHeight: 24 }
                ColumnLayout {
                    Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 5
                    Label { objectName: "noticeFileName"; Layout.fillWidth: true; text: root.fileName; textFormat: Text.PlainText; elide: Text.ElideMiddle; color: HomeTheme.text; font.pixelSize: 13; font.weight: Font.Medium
                        ToolTip.visible: fileHover.hovered; ToolTip.text: root.fileName; HoverHandler { id: fileHover } }
                }
            }
        }
        Label { visible: !root.filePath && root.detail.length>0; Layout.fillWidth: true; text: root.severity === "error" ? HomeTheme.t("文件未能保存，请检查目标位置或重试。") : root.detail; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: HomeTheme.secondary; font.pixelSize: 13 }
        HomeButton { visible: root.severity === "error" && root.detail.length>0; text: HomeTheme.t(root.detailsExpanded ? "收起详情" : "查看详情"); quiet: true; compact: true; iconName: "info"; onClicked: root.detailsExpanded=!root.detailsExpanded }
        ScrollView {
            visible: root.detailsExpanded; Layout.fillWidth: true; Layout.preferredHeight: Math.min(140,errorText.implicitHeight)
            contentWidth: availableWidth; clip: true
            TextArea { id: errorText; objectName: "noticeErrorDetail"; text: root.detail; textFormat: TextEdit.PlainText; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; color: HomeTheme.secondary; font.pixelSize: 12; padding: 10; background: Rectangle { radius: 8; color: HomeTheme.preview } }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            HomeButton { visible: root.filePath.length>0; text: HomeTheme.t("打开文件夹"); iconName: "folder"; quiet: true; onClicked: Qt.openUrlExternally("file://"+encodeURI(root.directory).replace(/#/g,"%23").replace(/\?/g,"%3F")) }
            Item { Layout.fillWidth: true }
            HomeButton { id: done; objectName: "noticeDone"; visible: !root.notification; text: HomeTheme.t("完成"); primary: true; onClicked: root.accept(); Keys.onReturnPressed: root.accept(); Keys.onEnterPressed: root.accept() }
        }
    }
}
