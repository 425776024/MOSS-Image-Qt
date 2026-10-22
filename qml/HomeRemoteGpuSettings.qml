import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

HomeSection {
    id: root; objectName: "homeRemoteGpuSettings"
    title: HomeTheme.t("图像计算"); iconName: "monitor"
    property bool advanced: false
    signal resumeRequested()
    FolderDialog { id: archiveDialog; title: HomeTheme.t("选择结果回收目录"); onAccepted: studio.remoteGpu.chooseArchive(selectedFolder) }
    RowLayout {
        Layout.fillWidth: true; spacing: 10; enabled: !studio.busy && !studio.remoteGpu.busy
        HomeChoiceCard { Layout.fillWidth: true; title: HomeTheme.t("这台 Mac"); detail: "Apple GPU"; iconName: "monitor"; selected: studio.imageDevice==="local"; onClicked: studio.imageDevice="local" }
        HomeChoiceCard { Layout.fillWidth: true; title: HomeTheme.t("远程 GPU"); detail: HomeTheme.t("SSH 服务器"); iconName: "server"; selected: studio.imageDevice==="remote"; onClicked: studio.imageDevice="remote" }
    }
    ColumnLayout {
        visible: studio.imageDevice==="remote"; Layout.fillWidth: true; spacing: 10
        RowLayout {
            Layout.fillWidth: true; spacing: 8; enabled: !studio.busy && !studio.remoteGpu.busy
            HomeField { id: host; objectName: "remoteHost"; Accessible.name: HomeTheme.t("服务器地址"); Layout.fillWidth: true; text: studio.remoteGpu.host; placeholderText: HomeTheme.t("服务器地址") }
            HomeField { id: user; objectName: "remoteUser"; Accessible.name: HomeTheme.t("SSH 用户名"); Layout.preferredWidth: 120; text: studio.remoteGpu.user; placeholderText: HomeTheme.t("用户名") }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            HomeField { id: password; objectName: "remotePassword"; Accessible.name: HomeTheme.t("SSH 连接密码"); Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: HomeTheme.t("密码（使用 SSH 密钥时留空）"); enabled: !studio.remoteGpu.busy; onAccepted: connectButton.clicked() }
            HomeButton {
                id: connectButton; objectName: "remoteConnect"; text: studio.remoteGpu.busy ? HomeTheme.t("连接中…") : studio.remoteGpu.ready ? HomeTheme.t("检查连接") : HomeTheme.t("连接"); primary: !studio.remoteGpu.ready; enabled: !studio.remoteGpu.busy
                onClicked: {
                    if(studio.busy || studio.remoteGpu.configure(host.text.trim(),user.text.trim(),Number(port.text))) {
                        const secret=password.text; password.clear(); studio.remoteGpu.connectServer(secret)
                    }
                }
            }
        }
        Text { Layout.fillWidth: true; visible: !!text; text: studio.remoteGpu.error || studio.remoteGpu.status; color: studio.remoteGpu.error ? HomeTheme.danger : HomeTheme.secondary; font.pixelSize: 11; wrapMode: Text.Wrap }
        HomeButton { text: HomeTheme.t("连接选项"); iconName: "settings"; compact: true; quiet: true; onClicked: root.advanced=!root.advanced }
        ColumnLayout {
            Layout.fillWidth: true; visible: root.advanced; spacing: 8
            RowLayout { Layout.fillWidth: true; spacing: 10
                Text { text: HomeTheme.t("SSH 端口"); font.pixelSize: 11; color: HomeTheme.secondary }
                HomeField { id: port; objectName: "remotePort"; Accessible.name: HomeTheme.t("SSH 端口"); Layout.preferredWidth: 90; text: String(studio.remoteGpu.port); validator: IntValidator { bottom: 1; top: 65535 } enabled: !studio.busy && !studio.remoteGpu.busy }
                Item { Layout.fillWidth: true }
            }
            RowLayout { Layout.fillWidth: true; spacing: 8
                Text { text: HomeTheme.t("结果目录"); font.pixelSize: 11; color: HomeTheme.secondary }
                Text { Layout.fillWidth: true; Layout.minimumWidth: 0; text: studio.remoteGpu.archiveRoot; elide: Text.ElideMiddle; font.pixelSize: 11; color: HomeTheme.muted }
                IconButton { glyph: "folder"; hint: HomeTheme.t("打开结果目录"); onClicked: studio.remoteGpu.openArchive() }
                HomeButton { text: HomeTheme.t("更改"); quiet: true; enabled: !studio.busy && !studio.remoteGpu.busy; onClicked: archiveDialog.open() }
            }
        }
        HomeButton { objectName: "recoverRemoteImage"; visible: studio.remoteGpu.pendingCount>0; text: HomeTheme.t("回收未完成任务 · ")+studio.remoteGpu.pendingCount; enabled: !studio.busy && !studio.remoteGpu.busy && studio.remoteGpu.ready; onClicked: {studio.resumeRemoteImage();root.resumeRequested()} }
    }
}
