import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root; objectName: "homeMcpPanel"; color: HomeTheme.sidebar
    property string selectedCall: ""
    property var detail: ({})
    property string notice: ""
    function t(zh,en) { return HomeTheme.english ? en : zh }
    function copy(text) { studio.copyText(text);notice=t("已复制","Copied");noticeTimer.restart() }
    function stateLabel(state) { return ({queued:t("排队中","Queued"),running:t("执行中","Running"),cancelling:t("正在取消","Cancelling"),completed:t("已完成","Completed"),failed:t("失败","Failed"),cancelled:t("已取消","Cancelled"),interrupted:t("已中断","Interrupted")})[state] || state || "" }
    function select(id) { selectedCall=id;detail=mcp.callDetails(id) }
    Timer { id: noticeTimer; interval: 1800; onTriggered: root.notice="" }
    Connections { target: mcp; function onCallChanged(id) { if(id===root.selectedCall || !id)root.detail=mcp.callDetails(root.selectedCall) } }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 22; spacing: 12
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true; spacing: 5
                Text { text: "MCP"; color: HomeTheme.text; font.pixelSize: 22; font.weight: Font.DemiBold }
                Text { Layout.fillWidth: true; text: mcp.running ? (mcp.lanEnabled ? root.t("已开启 · 局域网 · ","Running · Local network · ") : root.t("已开启 · 仅限本机 · ","Running · This computer only · "))+mcp.toolCount+root.t(" 个接口"," tools") : root.t("让其他软件调用创作与资源管理功能。","Connect other apps to creation and resource management."); color: HomeTheme.secondary; font.pixelSize: 12; wrapMode: Text.Wrap }
            }
            Text { text: root.notice; color: HomeTheme.secondary; font.pixelSize: 12 }
            HomeButton { objectName: "mcpToggle"; text: mcp.running ? root.t("关闭服务","Turn Off") : root.t("开启服务","Turn On"); primary: !mcp.running; focusPolicy: Qt.StrongFocus; activeFocusOnTab: true; onClicked: mcp.setEnabled(!mcp.running) }
        }
        Text { Layout.fillWidth: true; text: root.t("首次手动开启后，随应用启动；退出应用时自动关闭。","After you enable it, the service starts with the app and stops when the app quits."); color: HomeTheme.muted; font.pixelSize: 12; wrapMode: Text.Wrap }
        RowLayout {
            Layout.fillWidth: true; spacing: 12
            AppCheckBox { objectName: "mcpLanToggle"; text: root.t("允许局域网连接","Allow Local Network"); checked: mcp.lanEnabled; onClicked: mcp.setLanEnabled(checked) }
            HomeCombo { objectName: "mcpLanAddress"; visible: mcp.lanEnabled && mcp.lanAddresses.length>1; Layout.preferredWidth: 160; model: mcp.lanAddresses; currentIndex: mcp.lanAddresses.indexOf(mcp.lanAddress); onActivated: mcp.setLanAddress(currentText); Accessible.name: root.t("局域网地址","Local network address") }
            Item { Layout.fillWidth: true }
        }
        Text { Layout.fillWidth: true; visible: mcp.lanEnabled; text: mcp.lanAddresses.length ? root.t("仅用于可信局域网（HTTP 不加密）。另一台机器使用下方地址与访问凭证；连接失败时检查防火墙。","Use a trusted network (HTTP is unencrypted). Other computers need the address and credential below; check the firewall if connection fails.") : root.t("未发现局域网 IPv4 地址，请连接 Wi-Fi 或有线网络。","No local IPv4 address found. Connect to Wi-Fi or Ethernet."); color: HomeTheme.muted; font.pixelSize: 11; wrapMode: Text.Wrap }
        Text { visible: !!mcp.error; text: mcp.error; Layout.fillWidth: true; color: HomeTheme.danger; wrapMode: Text.Wrap; font.pixelSize: 12 }
        HomeSection {
            visible: mcp.running; Layout.fillWidth: true; padding: 12; contentSpacing: 8
            RowLayout {
                Layout.fillWidth: true; spacing: 10
                Text { text: "Streamable HTTP"; color: HomeTheme.secondary; font.pixelSize: 12 }
                TextField { objectName: "mcpUrl"; Layout.fillWidth: true; readOnly: true; selectByMouse: true; text: mcp.url; font.family: "Menlo"; font.pixelSize: 12; color: HomeTheme.text; padding: 5; background: null; Accessible.name: root.t("MCP 连接地址","MCP endpoint") }
                HomeButton { objectName: "mcpCopyUrl"; text: root.t("复制地址","Copy URL"); quiet: true; enabled: !mcp.lanEnabled || mcp.lanAddresses.length>0; onClicked: root.copy(mcp.url) }
            }
            Flow {
                Layout.fillWidth: true; spacing: 8
                HomeButton { objectName: "mcpCopyPrompt"; text: root.t("复制接入提示词","Copy Setup Prompt"); primary: true; enabled: !mcp.lanEnabled || mcp.lanAddresses.length>0; onClicked: root.copy(mcp.connectionText(HomeTheme.english)) }
                HomeButton { objectName: "mcpCopyConfig"; text: root.t("复制连接配置","Copy Configuration"); enabled: !mcp.lanEnabled || mcp.lanAddresses.length>0; onClicked: root.copy(mcp.configuration()) }
                HomeButton { text: root.t("复制接口说明","Copy Tool Reference"); quiet: true; onClicked: root.copy(mcp.toolReference()) }
            }
            Text { Layout.fillWidth: true; text: root.t("接入提示词与配置包含访问凭证。支持上传素材、图片预览和音视频下载；调用期间保持应用打开。","Setup includes the access credential. Supports uploads, image previews and media downloads; keep this app open."); color: HomeTheme.muted; font.pixelSize: 11; wrapMode: Text.Wrap }
        }
        RowLayout {
            Layout.fillWidth: true
            Text { text: root.t("实时调用","Live Activity"); color: HomeTheme.text; font.pixelSize: 13; font.weight: Font.Medium }
            Text { text: mcp.calls.count+root.t(" 条 · 本次运行最多保留 200 条"," calls · Latest 200 this session"); color: HomeTheme.muted; font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
            HomeButton { text: root.t("清空记录","Clear Activity"); quiet: true; compact: true; enabled: mcp.calls.count>0; onClicked: {root.selectedCall="";root.detail={};mcp.clearCalls()} }
        }
        SplitView {
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 160
            handle: Rectangle { implicitWidth: 1; color: HomeTheme.line }
            Rectangle {
                SplitView.preferredWidth: Math.min(300,root.width*.32); SplitView.minimumWidth: 200; color: HomeTheme.field
                ListView {
                    id: activity; objectName: "mcpActivityList"; anchors.fill: parent; anchors.margins: 5; clip: true; spacing: 3
                    model: mcp.calls; reuseItems: true; ScrollBar.vertical: ScrollBar { }
                    delegate: AbstractButton {
                        id: call; required property var entry; width: activity.width; height: 48; padding: 7; hoverEnabled: true; focusPolicy: Qt.StrongFocus; activeFocusOnTab: true
                        Accessible.name: entry.name+" · "+root.stateLabel(entry.state); Accessible.selected: root.selectedCall===entry.id
                        background: Rectangle { radius: 6; color: root.selectedCall===call.entry.id ? HomeTheme.selection : call.hovered ? HomeTheme.hover : "transparent"; border.color: call.visualFocus ? HomeTheme.accent : "transparent" }
                        contentItem: ColumnLayout {
                            spacing: 3
                            RowLayout { Layout.fillWidth: true; spacing: 8
                                Text { Layout.fillWidth: true; text: call.entry.name; color: HomeTheme.text; font.family: "Menlo"; font.pixelSize: 11; elide: Text.ElideRight }
                                Text { text: Qt.formatDateTime(new Date(call.entry.startedAt),"hh:mm:ss"); color: HomeTheme.muted; font.pixelSize: 10 }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: root.stateLabel(call.entry.state)+" · "+((call.entry.state==="running" || call.entry.state==="cancelling") && call.entry.stage ? call.entry.stage : ((call.entry.elapsedMs || 0)/1000).toFixed(2)+" s")
                                color: call.entry.state==="failed" ? HomeTheme.danger : HomeTheme.secondary; font.pixelSize: 11; elide: Text.ElideRight
                            }
                        }
                        onClicked: root.select(entry.id)
                    }
                    Text { visible: activity.count===0; anchors.centerIn: parent; width: parent.width-24; text: root.t("连接后，接口调用会显示在这里。","Tool calls appear here after a client connects."); wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter; color: HomeTheme.muted; font.pixelSize: 12 }
                }
            }
            Rectangle {
                SplitView.fillWidth: true; SplitView.minimumWidth: 280; color: HomeTheme.field
                ScrollView {
                    id: details; anchors.fill: parent; anchors.margins: 12; clip: true; contentWidth: availableWidth; ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    ColumnLayout {
                        width: details.availableWidth; spacing: 10
                        Text { Layout.fillWidth: true; text: root.detail.name || root.t("调用详情","Call Details"); font.pixelSize: 13; font.weight: Font.Medium; color: HomeTheme.text; wrapMode: Text.WrapAnywhere }
                        Text { Layout.fillWidth: true; text: root.detail.id ? root.stateLabel(root.detail.state)+" · "+((root.detail.elapsedMs || 0)/1000).toFixed(2)+" s" : root.t("选择一条调用，查看参数、进度与结果。","Select a call to inspect arguments, progress and results."); color: HomeTheme.secondary; font.pixelSize: 12; wrapMode: Text.Wrap }
                        Text { visible: !!root.detail.stage; Layout.fillWidth: true; text: root.detail.stage || ""; color: HomeTheme.secondary; font.pixelSize: 12; wrapMode: Text.Wrap }
                        ProgressBar { visible: root.detail.state==="running" || root.detail.state==="cancelling"; Layout.fillWidth: true; value: root.detail.progress || 0; indeterminate: !root.detail.progress; Accessible.name: root.t("当前阶段进度","Current stage progress") }
                        RowLayout { visible: !!root.detail.id; Layout.fillWidth: true
                            Text { Layout.fillWidth: true; text: root.t("参数","Arguments"); color: HomeTheme.secondary; font.pixelSize: 11 }
                            HomeButton { text: root.t("复制","Copy"); compact: true; quiet: true; onClicked: root.copy(root.detail.argumentsText || "{}") }
                        }
                        TextArea { visible: !!root.detail.id; Layout.fillWidth: true; text: root.detail.argumentsText || ""; textFormat: TextEdit.PlainText; readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere; font.family: "Menlo"; font.pixelSize: 11; color: HomeTheme.text; padding: 0; background: null }
                        RowLayout { visible: !!root.detail.id; Layout.fillWidth: true
                            Text { Layout.fillWidth: true; text: root.t("结果","Result"); color: HomeTheme.secondary; font.pixelSize: 11 }
                            HomeButton { text: root.t("复制","Copy"); compact: true; quiet: true; onClicked: root.copy(root.detail.resultText || "{}") }
                        }
                        TextArea { objectName: "mcpCallResult"; visible: !!root.detail.id; Layout.fillWidth: true; text: root.detail.resultText || ""; textFormat: TextEdit.PlainText; readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere; font.family: "Menlo"; font.pixelSize: 11; color: HomeTheme.text; padding: 0; background: null }
                    }
                }
            }
        }
        Text { Layout.fillWidth: true; text: root.t("关闭服务会停止接入，已提交的任务继续执行；退出应用会中断未完成任务。","Turning off disconnects clients; accepted jobs continue. Quitting interrupts unfinished jobs."); color: HomeTheme.muted; font.pixelSize: 11; wrapMode: Text.Wrap }
    }
}
