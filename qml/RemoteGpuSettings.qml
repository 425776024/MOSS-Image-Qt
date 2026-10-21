import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Rectangle {
    id: root
    signal resumeRequested()
    implicitHeight: content.implicitHeight + 40
    color: StudioTheme.field
    border.color: StudioTheme.line
    radius: 10
    FolderDialog {
        id: archiveDialog
        title: "选择远程图片和日志的回收目录"
        onAccepted: studio.remoteGpu.chooseArchive(selectedFolder)
    }
    ColumnLayout {
        id: content
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 20 }
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            Text { text: "生图运行位置"; font.pixelSize: 17; font.weight: Font.DemiBold; color: StudioTheme.text }
            Item { Layout.fillWidth: true }
            StudioCombo {
                objectName: "settingsImageDevice"
                Accessible.name: "生图运行位置"
                Layout.preferredWidth: 220
                model: studio.remoteGpu.available ? ["本机 · Apple GPU", "远程 GPU"] : ["本机 · Apple GPU"]
                currentIndex: studio.imageDevice === "remote" ? 1 : 0
                enabled: !studio.busy && !studio.remoteGpu.busy
                onActivated: studio.imageDevice = currentIndex === 1 ? "remote" : "local"
            }
        }
        Text {
            visible: studio.imageDevice === "local"
            Layout.fillWidth: true; wrapMode: Text.Wrap
            text: "使用本机模型生成图像。文生图、Layout 和局部重绘共用此选择。"
            color: StudioTheme.muted; font.pixelSize: 12
        }
        ColumnLayout {
            visible: studio.imageDevice === "remote"
            Layout.fillWidth: true; spacing: 10
            Text {
                Layout.fillWidth: true; wrapMode: Text.Wrap
                text: "使用 Ubuntu 显卡生成，完成后自动回收图片与日志，并显示到当前画布。"
                color: StudioTheme.secondary; font.pixelSize: 12
            }
            RowLayout {
                Layout.fillWidth: true; enabled: !studio.busy && !studio.remoteGpu.busy
                StudioField { id: host; objectName: "remoteHost"; Accessible.name: "服务器地址"; Layout.fillWidth: true; text: studio.remoteGpu.host; placeholderText: "服务器地址" }
                StudioField { id: user; objectName: "remoteUser"; Accessible.name: "SSH 用户名"; Layout.preferredWidth: 130; text: studio.remoteGpu.user; placeholderText: "用户名" }
                StudioField { id: port; objectName: "remotePort"; Accessible.name: "SSH 端口"; Layout.preferredWidth: 85; text: String(studio.remoteGpu.port); validator: IntValidator { bottom: 1; top: 65535 } }
            }
            RowLayout {
                Layout.fillWidth: true
                StudioField {
                    id: password; objectName: "remotePassword"; Accessible.name: "SSH 连接密码"
                    Layout.fillWidth: true; echoMode: TextInput.Password
                    placeholderText: "连接密码（仅本次认证；已有 SSH 密钥可留空）"
                    enabled: !studio.remoteGpu.busy
                    onAccepted: connectButton.clicked()
                }
                StudioButton {
                    id: connectButton; objectName: "remoteConnect"; text: studio.remoteGpu.busy ? "连接中…" : "连接 / 检查"
                    enabled: !studio.remoteGpu.busy
                    onClicked: {
                        if (studio.busy || studio.remoteGpu.configure(host.text.trim(), user.text.trim(), Number(port.text))) {
                            const secret = password.text
                            password.clear()
                            studio.remoteGpu.connectServer(secret)
                        }
                    }
                }
            }
            Text {
                Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 12
                text: studio.remoteGpu.error || studio.remoteGpu.status
                color: studio.remoteGpu.error ? "#e6a092" : studio.remoteGpu.ready ? StudioTheme.accent : StudioTheme.muted
            }
            Text { text: "图片与日志回收目录"; font.pixelSize: 12; color: StudioTheme.secondary }
            RowLayout {
                Layout.fillWidth: true
                Text { Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; text: studio.remoteGpu.archiveRoot; color: StudioTheme.muted; font.pixelSize: 11 }
                StudioButton { text: "选择目录"; enabled: !studio.busy && !studio.remoteGpu.busy; onClicked: archiveDialog.open() }
                StudioButton { text: "打开"; onClicked: studio.remoteGpu.openArchive() }
            }
            StudioButton {
                objectName: "recoverRemoteImage"; visible: studio.remoteGpu.pendingCount > 0
                text: "恢复并回收远程任务（" + studio.remoteGpu.pendingCount + "）"
                enabled: !studio.busy && !studio.remoteGpu.busy && studio.remoteGpu.ready
                onClicked: { studio.resumeRemoteImage(); root.resumeRequested() }
            }
        }
    }
}
