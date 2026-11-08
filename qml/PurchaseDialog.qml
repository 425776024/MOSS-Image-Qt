import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

HomeDialog {
    id: root
    objectName: "purchaseDialog"
    property var purchases: studio.purchases
    property bool accessPrompt: false
    title: HomeTheme.t(purchases.lifetimeUnlocked ? "已永久解锁" : purchases.trialStarted ? (purchases.accessAllowed ? "免费试用中" : "试用已结束") : "免费试用 3 天")
    width: Math.min(440,Overlay.overlay ? Overlay.overlay.width-40 : 440)
    closePolicy: purchases.accessAllowed ? Popup.CloseOnEscape : Popup.NoAutoClose
    defaultAction: purchases.canStartTrial ? trialButton : purchases.lifetimeUnlocked ? closeButton : buyButton
    onOpened: accessPrompt=!purchases.accessAllowed
    Component.onCompleted: if(purchases.storeEdition && !purchases.accessAllowed)open()
    Connections {
        target: root.purchases
        function onUnlockRequested(){root.open()}
        function onChanged(){
            if(!root.purchases.storeEdition)return
            if(root.accessPrompt && root.purchases.accessAllowed && root.opened)root.close()
            else if(!root.purchases.accessAllowed && !root.visible)root.open()
        }
    }
    ColumnLayout {
        width: parent.width; spacing: 14
        Label {
            Layout.fillWidth: true; color: HomeTheme.text; wrapMode: Text.Wrap
            text: purchases.lifetimeUnlocked ? HomeTheme.t("全部创作功能已永久解锁。") : HomeTheme.t("完整体验图像、语音、音效和数字人创作。")
        }
        Label {
            Layout.fillWidth: true; color: HomeTheme.secondary; wrapMode: Text.Wrap
            visible: !purchases.lifetimeUnlocked
            text: purchases.trialStarted && purchases.accessAllowed
                ? HomeTheme.t("试用剩余 %1 天。到期后需买断继续使用。").arg(purchases.trialDaysRemaining)
                : HomeTheme.t("试用从 Apple 确认后开始，持续 72 小时。到期后需买断才能继续使用全部创作功能。")
        }
        Label {
            Layout.fillWidth: true; color: HomeTheme.secondary; wrapMode: Text.Wrap
            text: purchases.price.length>0 ? HomeTheme.t("买断价格：%1。一次购买，永久使用，不自动扣款。").arg(purchases.price) : HomeTheme.t("正在获取买断价格…")
            visible: !purchases.lifetimeUnlocked
        }
        Label {
            objectName: "purchaseStatus"; Layout.fillWidth: true; color: HomeTheme.secondary; wrapMode: Text.Wrap
            visible: text.length>0
            text: !purchases.ready ? HomeTheme.t("正在检查购买状态…") : purchases.busy ? HomeTheme.t("正在处理购买…") : HomeTheme.t(purchases.message)
            Accessible.role: Accessible.StaticText
        }
        HomeButton {
            id: trialButton; objectName: "startFreeTrial"; Layout.fillWidth: true; primary: true
            visible: !purchases.lifetimeUnlocked && !purchases.trialStarted
            enabled: purchases.canStartTrial; text: HomeTheme.t("开始 3 天免费试用")
            onClicked: purchases.startTrial()
        }
        HomeButton {
            id: buyButton; objectName: "buyLifetime"; Layout.fillWidth: true
            primary: purchases.trialStarted; visible: !purchases.lifetimeUnlocked; enabled: purchases.canPurchase
            text: purchases.price.length>0 ? HomeTheme.t("永久买断")+" · "+purchases.price : HomeTheme.t("获取价格中…")
            onClicked: purchases.purchase()
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            HomeButton { objectName: "restorePurchases"; text: HomeTheme.t("恢复购买"); quiet: true; enabled: !purchases.busy; onClicked: purchases.restore() }
            HomeButton { text: HomeTheme.t("重试"); quiet: true; visible: !purchases.canPurchase && !purchases.lifetimeUnlocked && !purchases.busy; onClicked: purchases.refresh() }
            Item { Layout.fillWidth: true }
            HomeButton { id: closeButton; text: HomeTheme.t(purchases.accessAllowed ? "完成" : "退出"); quiet: true; onClicked: {if(purchases.accessAllowed)root.close();else Qt.quit()} }
        }
    }
}
