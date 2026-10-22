import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property bool embedded: false
    color: HomeTheme.sidebar
    ColumnLayout {
        anchors.fill: parent; anchors.margins: root.embedded ? 0 : 22; spacing: 20
        HomeHeader { visible: !root.embedded; Layout.fillWidth: true; title: HomeTheme.t("联系我们") }
        ScrollView {
            id: view
            implicitHeight: 0
            Layout.fillWidth: true; Layout.fillHeight: true
            contentWidth: availableWidth
            contentHeight: Math.max(availableHeight, contactCard.implicitHeight)
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            HomeSection {
                id: contactCard
                objectName: "contactCard"
                width: Math.min(520,view.availableWidth)
                x: (view.availableWidth-width)/2
                y: Math.max(0,(view.availableHeight-height)/2)
                padding: 20
                RowLayout { Layout.fillWidth: true; spacing: 18
                    Rectangle { Layout.preferredWidth: 176; Layout.preferredHeight: 176; color: "white"; radius: 8
                        Image { anchors.fill: parent; anchors.margins: 8; source: "qrc:/home/assets/home/wechat_contact_qr.png"; fillMode: Image.PreserveAspectFit; Accessible.name: HomeTheme.t("微信") }
                    }
                    ColumnLayout { Layout.fillWidth: true; spacing: 10
                        Text { Layout.fillWidth: true; text: HomeTheme.t("扫码添加微信"); color: HomeTheme.text; font.pixelSize: 17; font.weight: Font.DemiBold; wrapMode: Text.Wrap }
                        Text { Layout.fillWidth: true; text: HomeTheme.t("欢迎咨询使用问题或反馈建议。"); color: HomeTheme.secondary; font.pixelSize: 12; wrapMode: Text.Wrap }
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: HomeTheme.line }
                RowLayout { Layout.fillWidth: true; spacing: 18
                    Rectangle { Layout.preferredWidth: 176; Layout.preferredHeight: 176; color: "white"; radius: 8
                        Image {
                            objectName: "officialAccountQr"
                            anchors.fill: parent; anchors.margins: 8
                            source: "qrc:/home/assets/home/wechat_official_account_qr.png"
                            // Show the original QR with its quiet zone; the account name is beside it.
                            sourceClipRect: Qt.rect(166, 104, 718, 718)
                            fillMode: Image.PreserveAspectFit
                            Accessible.role: Accessible.Graphic
                            Accessible.name: HomeTheme.t("新身数字人公众号二维码")
                        }
                    }
                    ColumnLayout { Layout.fillWidth: true; spacing: 10
                        Text { Layout.fillWidth: true; text: "新身数字人"; color: HomeTheme.text; font.pixelSize: 17; font.weight: Font.DemiBold; wrapMode: Text.Wrap }
                        Text { Layout.fillWidth: true; text: HomeTheme.t("微信扫码关注公众号"); color: HomeTheme.secondary; font.pixelSize: 12; wrapMode: Text.Wrap }
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: HomeTheme.line }
                RowLayout { Layout.fillWidth: true; spacing: 8
                    HomeButton { text: HomeTheme.t("B站主页"); iconName: "play"; quiet: true; onClicked: Qt.openUrlExternally("https://space.bilibili.com/98643795") }
                    HomeButton { text: HomeTheme.t("使用帮助"); iconName: "help"; quiet: true; onClicked: Qt.openUrlExternally("https://ffclip.com") }
                    Item { Layout.fillWidth: true }
                }
            }
        }
    }
}
