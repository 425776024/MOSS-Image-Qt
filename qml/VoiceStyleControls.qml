import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property var options
    signal optionEdited(string key, string value)
    spacing: 6
    readonly property var emotions: ["中性","高兴","悲伤","愤怒","恐惧","厌恶","惊讶"]
    readonly property var dialects: ["standard","chuanyu","cantonese"]
    RowLayout {
        Layout.fillWidth: true; spacing: 10
        ColumnLayout {
            Layout.fillWidth: true; spacing: 4
            Text { text: HomeTheme.t("情绪"); color: HomeTheme.secondary; font.pixelSize: 11 }
            HomeCombo {
                objectName: "voiceEmotion"; Layout.fillWidth: true
                model: root.emotions.map(value=>HomeTheme.t(value))
                currentIndex: Math.max(0,root.emotions.indexOf(root.options.emotion || "中性"))
                enabled: !studio.busy && (!root.options.dialect || root.options.dialect === "standard")
                Accessible.name: HomeTheme.t("情绪")
                onActivated: root.optionEdited("emotion",root.emotions[currentIndex])
            }
        }
        ColumnLayout {
            Layout.fillWidth: true; spacing: 4
            Text { text: HomeTheme.t("方言"); color: HomeTheme.secondary; font.pixelSize: 11 }
            HomeCombo {
                objectName: "voiceDialect"; Layout.fillWidth: true; enabled: !studio.busy
                model: [HomeTheme.t("普通话"),HomeTheme.t("川渝话"),HomeTheme.t("粤语")]
                currentIndex: Math.max(0,root.dialects.indexOf(root.options.dialect || "standard"))
                Accessible.name: HomeTheme.t("方言")
                onActivated: root.optionEdited("dialect",root.dialects[currentIndex])
            }
        }
    }
    Text {
        visible: !!root.options.dialect && root.options.dialect !== "standard"
        Layout.fillWidth: true; text: HomeTheme.t("方言使用独立朗读模式，不能叠加情绪。")
        wrapMode: Text.Wrap; color: HomeTheme.muted; font.pixelSize: 11
    }
}
