import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    color: HomeTheme.sidebar
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 22; spacing: 20
        HomeHeader { Layout.fillWidth: true; title: HomeTheme.t("联系我们") }
        HomeSection { Layout.preferredWidth: Math.min(600,parent.width); padding: 22
            RowLayout { Layout.fillWidth: true; spacing: 22
                Rectangle { width: 176; height: 176; color: "white"; radius: 8
                    Image { anchors.fill: parent; anchors.margins: 8; source: "qrc:/home/assets/home/wechat_contact_qr.png"; fillMode: Image.PreserveAspectFit; Accessible.name: HomeTheme.t("微信") }
                }
                ColumnLayout { Layout.fillWidth: true; spacing: 10
                    Text { text: HomeTheme.t("扫码添加微信"); color: HomeTheme.text; font.pixelSize: 17; font.weight: Font.DemiBold }
                    Text { Layout.fillWidth: true; text: HomeTheme.t("欢迎咨询使用问题或反馈建议。"); color: HomeTheme.secondary; font.pixelSize: 12; wrapMode: Text.Wrap }
                }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: HomeTheme.line }
            RowLayout { Layout.fillWidth: true; spacing: 8
                HomeButton { text: HomeTheme.t("B站主页"); iconName: "play"; quiet: true; onClicked: Qt.openUrlExternally("https://space.bilibili.com/98643795") }
                HomeButton { text: HomeTheme.t("使用帮助"); iconName: "help"; quiet: true; onClicked: Qt.openUrlExternally("https://ffclip.com") }
                Item { Layout.fillWidth: true }
            }
        }
        Item { Layout.fillHeight: true }
    }
}
