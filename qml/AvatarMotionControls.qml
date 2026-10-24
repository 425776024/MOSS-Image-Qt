import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: root
    objectName: "avatarMotionControls"
    required property var options
    property bool expanded: false
    signal optionEdited(string key, var value)
    readonly property var emotionIds: [-1, 4, 3, 5, 0, 2, 1, 6, 7]
    readonly property var emotionNames: ["默认", "中性", "高兴", "悲伤", "愤怒", "恐惧", "厌恶", "惊讶", "轻蔑"]
    readonly property var weights: options.use_d_keys || ({exp: 1, pitch: 1, yaw: 1, roll: 1, t: 1})
    readonly property string strengthSummary: {
        const values = Object.values(weights).map(value => Math.round(value * 100))
        const low = Math.min.apply(null, values), high = Math.max.apply(null, values)
        return low === high ? high + "%" : low + "–" + high + "%"
    }
    spacing: 8
    RowLayout {
        Layout.fillWidth: true; spacing: 10
        Text { text: HomeTheme.t("人物表情"); color: HomeTheme.secondary; font.pixelSize: 12; Layout.fillWidth: true }
        HomeCombo {
            objectName: "avatarEmotion"; Layout.preferredWidth: 150
            model: root.emotionNames.map(name => HomeTheme.t(name))
            currentIndex: Math.max(0, root.emotionIds.indexOf(root.options.emo === undefined ? -1 : root.options.emo))
            Accessible.name: HomeTheme.t("人物表情")
            onActivated: root.optionEdited("emo", root.emotionIds[currentIndex])
        }
    }
    Button {
        id: disclosure
        objectName: "avatarMotionDisclosure"; Layout.fillWidth: true
        implicitHeight: 36; leftPadding: 10; rightPadding: 10
        opacity: enabled ? 1 : 0.55
        hoverEnabled: true; checkable: true; checked: root.expanded
        Accessible.name: HomeTheme.t("动作幅度")
        Accessible.description: root.strengthSummary
        contentItem: RowLayout {
            spacing: 8
            StudioIcon { name: "chevron"; rotation: root.expanded ? 0 : -90; Layout.preferredWidth: 12; Layout.preferredHeight: 12; ink: HomeTheme.text }
            Text { text: HomeTheme.t("动作幅度"); Layout.fillWidth: true; color: HomeTheme.text; font.pixelSize: 13; font.weight: Font.Medium }
            Text { text: root.strengthSummary; color: HomeTheme.secondary; font.pixelSize: 12 }
        }
        background: Rectangle {
            radius: 6; color: disclosure.down || disclosure.hovered ? HomeTheme.hover : HomeTheme.dialogInset
            border.width: disclosure.visualFocus ? 1 : 0
            border.color: HomeTheme.accent
        }
        onClicked: root.expanded = !root.expanded
    }
    ColumnLayout {
        visible: root.expanded; Layout.fillWidth: true; Layout.leftMargin: 30; spacing: 5
        Repeater {
            model: [{key: "exp", name: "口型与表情"}, {key: "pitch", name: "点头"},
                    {key: "yaw", name: "转头"}, {key: "roll", name: "歪头"}, {key: "t", name: "位移"}]
            RowLayout {
                id: row
                required property var modelData
                Layout.fillWidth: true; spacing: 8
                Text { text: HomeTheme.t(row.modelData.name); Layout.preferredWidth: HomeTheme.english ? 104 : 72; font.pixelSize: 12; color: HomeTheme.secondary }
                AppSlider {
                    objectName: "avatarGain_" + row.modelData.key
                    Layout.fillWidth: true; Layout.minimumWidth: 48
                    from: 0; to: 200; stepSize: 5
                    value: Math.round(100 * root.weights[row.modelData.key])
                    Accessible.name: HomeTheme.t(row.modelData.name)
                    onMoved: {
                        const next = Object.assign({}, root.weights)
                        next[row.modelData.key] = value / 100
                        root.optionEdited("use_d_keys", next)
                    }
                }
                Text { text: Math.round(100 * root.weights[row.modelData.key]) + "%"; Layout.preferredWidth: 38; horizontalAlignment: Text.AlignRight; font.pixelSize: 12; color: HomeTheme.text }
            }
        }
        HomeButton {
            objectName: "avatarMotionReset"; Layout.alignment: Qt.AlignRight
            text: HomeTheme.t("重置幅度"); quiet: true; compact: true
            onClicked: root.optionEdited("use_d_keys", {exp: 1, pitch: 1, yaw: 1, roll: 1, t: 1})
        }
    }
}
