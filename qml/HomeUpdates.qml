import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    objectName: "homeUpdates"
    color: HomeTheme.sidebar
    readonly property bool busy: ["checking","downloading","verifying","installing"].includes(appUpdate.state)
    readonly property bool working: ["downloading","verifying","installing"].includes(appUpdate.state)
    component UpdateButton: HomeButton { focusPolicy: Qt.StrongFocus; activeFocusOnTab: true }
    function t(zh,en) { return HomeTheme.english ? en : zh }
    function status() {
        switch(appUpdate.state) {
        case "checking": return t("正在检查更新…","Checking for updates…")
        case "available": return appUpdate.appleStore ? t("App Store 有新版本可用","An App Store update is available") : t("有新版本可用","An update is available")
        case "current": return appUpdate.appleStore ? t("已安装版本不低于 App Store 正式版","Your installed version is at least the current App Store release") : t("当前版本已是最新","You're up to date")
        case "unconfirmed": return t("可查看当前发布版本","View the current published release")
        case "notPublished": return appUpdate.appleStore ? t("App Store 暂无已上架版本","No version is published on the App Store yet") : t("暂无可用安装包","No installation package is available")
        case "downloading": return t("正在下载更新…","Downloading update…")
        case "verifying": return t("正在验证更新…","Verifying update…")
        case "installing": return t("正在安装，即将重启…","Installing. Restarting shortly…")
        case "verificationFailed": return t("更新验证失败，请重新下载","Verification failed. Download the update again.")
        case "diskFull": return t("空间不足，请清理后重试","Not enough space. Free up space and try again.")
        case "busy": return t("请等待创作或资源下载完成后重试","Wait for creation or resource downloads to finish.")
        case "error": return t("无法检查或下载更新，请重试","Couldn't check or download updates. Try again.")
        case "rateLimited": return t("GitHub 暂时无法查询，请稍后重试或查看下载","GitHub is temporarily unavailable. Try later or view downloads.")
        default: return t("检查最新版本","Check for the latest version")
        }
    }
    onVisibleChanged: if(visible && !busy)appUpdate.check()
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 22; spacing: 20
        HomeHeader {
            Layout.fillWidth: true; title: root.t("更新","Updates")
            UpdateButton { objectName: "updateCheck"; text: root.t("检查更新","Check for Updates"); iconName: "refresh"; enabled: !root.busy; onClicked: appUpdate.check() }
        }
        ScrollView {
            id: scroll; Layout.fillWidth: true; Layout.fillHeight: true; contentWidth: availableWidth; clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                width: Math.min(720,scroll.availableWidth); spacing: 16
                HomeSection {
                    Layout.fillWidth: true
                    Text { text: workspace.appName; font.pixelSize: 17; font.weight: Font.DemiBold; color: HomeTheme.text }
                    Text { text: root.t("当前版本 ","Installed version ")+appUpdate.currentVersion+" · "+(appUpdate.appleStore ? "App Store / TestFlight" : root.t("社区版","Community")); color: HomeTheme.secondary; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Text { objectName: "updateStatus"; text: root.status(); color: HomeTheme.text; font.pixelSize: 13; wrapMode: Text.Wrap; Layout.fillWidth: true; Accessible.role: Accessible.StaticText }
                    Text { visible: !!appUpdate.latest.version; text: (appUpdate.appleStore ? root.t("App Store 最新版本 ","Latest App Store version ") : root.t("最新版本 ","Latest version "))+(appUpdate.latest.version || "")+(appUpdate.latest.commit ? " ("+appUpdate.latest.commit.slice(0,7)+")" : "")+(appUpdate.latest.publishedAt ? " · "+Qt.formatDateTime(new Date(appUpdate.latest.publishedAt),"yyyy-MM-dd hh:mm") : ""); color: HomeTheme.secondary; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    ProgressBar { objectName: "updateProgress"; visible: root.working; Layout.fillWidth: true; from: 0; to: 1; value: appUpdate.progress; indeterminate: appUpdate.state!=="downloading" }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 8
                        Item { Layout.fillWidth: true }
                        UpdateButton { visible: appUpdate.state==="downloading"; text: root.t("取消","Cancel"); onClicked: appUpdate.cancel() }
                        UpdateButton {
                            objectName: "updateInstall"; visible: !appUpdate.appleStore && appUpdate.state==="available" && appUpdate.canInstall
                            primary: true; text: root.t("更新并重启","Update and Restart"); enabled: !studio.busy && !studio.models.busy
                            onClicked: appUpdate.install()
                        }
                        UpdateButton {
                            objectName: "updateOpenSource"; visible: !root.working && (appUpdate.appleStore || !appUpdate.canInstall || appUpdate.state!=="available")
                            text: appUpdate.appleStore ? root.t("打开 App Store","Open App Store") : root.t("查看下载","View Downloads")
                            onClicked: appUpdate.openStore(false)
                        }
                    }
                }
                HomeSection {
                    visible: appUpdate.appleStore; Layout.fillWidth: true; title: "TestFlight"
                    Text { Layout.fillWidth: true; text: root.t("测试版的可用版本与自动更新由 TestFlight 管理。","TestFlight manages available beta builds and automatic updates."); color: HomeTheme.secondary; font.pixelSize: 13; wrapMode: Text.Wrap }
                    RowLayout { Layout.fillWidth: true
                        Item { Layout.fillWidth: true }
                        UpdateButton { objectName: "updateTestFlight"; text: root.t("打开 TestFlight","Open TestFlight"); onClicked: appUpdate.openStore(true) }
                    }
                }
                Text {
                    visible: !appUpdate.appleStore; Layout.fillWidth: true
                    text: root.t("更新成功后自动清理安装包与旧版本，保留作品、模型和设置。","After a successful update, the installer and previous version are removed. Projects, models and settings are kept.")
                    color: HomeTheme.muted; font.pixelSize: 12; wrapMode: Text.Wrap
                }
                HomeSection {
                    visible: !!appUpdate.latest.notes; Layout.fillWidth: true; title: root.t("更新说明","What's New")
                    Text {
                        Layout.fillWidth: true; text: appUpdate.latest.notes || ""; color: HomeTheme.secondary; font.pixelSize: 13; wrapMode: Text.Wrap; textFormat: Text.MarkdownText
                        onLinkActivated: link => { if(/^https:\/\/github\.com\/425776024\/MOSS-Image-Qt\//.test(link))Qt.openUrlExternally(link) }
                    }
                }
            }
        }
    }
}
