import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AbstractButton {
    id: root
    property string title: ""
    property string detail: ""
    property string iconName: ""
    property bool selected: false
    property string appearance: ""
    property color previewColor: "#1c1c1e"
    property color previewAccent: HomeTheme.accent
    readonly property bool preview: appearance !== ""
    implicitWidth: 148; implicitHeight: preview ? 104 : detail ? 68 : 44
    padding: 10; hoverEnabled: true
    Accessible.role: Accessible.RadioButton; Accessible.name: title; Accessible.description: detail; Accessible.checked: selected
    Accessible.onPressAction: root.clicked()
    background: Rectangle {
        radius: 8; color: root.selected ? HomeTheme.selection : root.hovered ? HomeTheme.hover : HomeTheme.field
        border.color: root.selected || root.visualFocus ? HomeTheme.accent : HomeTheme.outline
        border.width: root.visualFocus ? 2 : 1
    }
    contentItem: ColumnLayout {
        spacing: 8
        Rectangle {
            visible: root.preview; Layout.fillWidth: true; Layout.fillHeight: true; radius: 5; clip: true
            color: root.appearance === "light" || root.appearance === "system" ? "#fafafa" : root.previewColor
            border.color: Qt.alpha(HomeTheme.text,.12)
            Rectangle { width: parent.width*.28; height: parent.height; color: root.appearance === "light" || root.appearance === "system" ? "#e6e7e9" : Qt.lighter(root.previewColor,1.25) }
            Rectangle { visible: root.appearance === "system"; anchors.right: parent.right; width: parent.width*.5; height: parent.height; color: root.previewColor }
            Row { x: 6; y: 6; spacing: 3; Repeater { model: ["#ff6058","#ffbd2e","#28c840"]; Rectangle { required property string modelData; width: 4; height: 4; radius: 2; color: modelData } } }
            Rectangle { x: 5; y: 19; width: parent.width*.21; height: 5; radius: 2; color: Qt.alpha(root.previewAccent,.5) }
            Rectangle { x: parent.width*.36; y: 19; width: parent.width*.42; height: 4; radius: 2; color: root.appearance === "light" ? "#c8cbd1" : "#69717f" }
            Rectangle { x: parent.width*.36; y: 28; width: parent.width*.27; height: 4; radius: 2; color: root.previewAccent }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            StudioIcon { visible: !root.preview && !!root.iconName; name: root.iconName; ink: root.selected ? HomeTheme.accent : HomeTheme.secondary; Layout.preferredWidth: 20; Layout.preferredHeight: 20 }
            ColumnLayout {
                Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 3
                Text { Layout.fillWidth: true; text: root.title; color: HomeTheme.text; font.pixelSize: 12; font.weight: Font.Medium; elide: Text.ElideRight }
                Text { visible: !!root.detail; Layout.fillWidth: true; text: root.detail; color: HomeTheme.secondary; font.pixelSize: 10; elide: Text.ElideRight }
            }
            StudioIcon { visible: root.selected; name: "check"; ink: HomeTheme.accent; Layout.preferredWidth: 14; Layout.preferredHeight: 14 }
        }
    }
    ToolTip.visible: hovered && !!detail; ToolTip.text: detail; ToolTip.delay: 650
}
