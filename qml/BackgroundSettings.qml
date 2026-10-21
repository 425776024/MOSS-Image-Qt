import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Rectangle {
    id: root
    color: StudioTheme.panel
    readonly property var element: studio.timeline.tracks.find(t => t.key === "background") || ({})
    FileDialog { id: imageFile; title: "选择背景图"; nameFilters: ["图片 (*.png *.jpg *.jpeg *.webp)"]; onAccepted: studio.importTrackImage("background",selectedFile) }
    ColorDialog { id: colorPicker; title: "背景颜色"; selectedColor: root.element.color; onAccepted: studio.timeline.setValue("background","color",selectedColor.toString()) }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 12; spacing: 12; enabled: !studio.busy
        RowLayout {
            Layout.fillWidth: true
            Text { text: "画布底色"; color: StudioTheme.secondary; font.pixelSize: 12; Layout.fillWidth: true }
            StudioButton {
                objectName: "toggleBackground"; text: root.element.enabled ? "已显示" : "已隐藏"
                quiet: true; active: !root.element.enabled
                hint: root.element.enabled ? "隐藏背景" : "显示背景"
                onClicked: studio.timeline.setValue("background", "enabled", !root.element.enabled)
            }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 6
            StudioButton { Layout.fillWidth: true; text: "纯色"; active: root.element.kind === "color"; onClicked: studio.timeline.setSource("background","","color") }
            StudioButton { Layout.fillWidth: true; text: "背景图"; active: root.element.kind === "image"; onClicked: imageFile.open() }
        }
        Rectangle {
            Layout.fillWidth: true; height: 116; color: root.element.color; radius: 8; border.color: StudioTheme.line; clip: true
            Image { anchors.fill: parent; visible: root.element.kind === "image"; source: visible ? root.element.source : ""; fillMode: Image.PreserveAspectCrop; asynchronous: true }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: colorPicker.open() }
        }
        RowLayout {
            Layout.fillWidth: true
            Text { text: "颜色"; color: StudioTheme.secondary; font.pixelSize: 12 }
            StudioField { Layout.fillWidth: true; text: root.element.color; Accessible.name: "背景颜色"; onEditingFinished: { studio.timeline.setValue("background","color",text); text = Qt.binding(() => root.element.color) } }
            StudioButton { iconName: "brush"; hint: "选择背景颜色"; onClicked: colorPicker.open() }
        }
        RowLayout {
            spacing: 8
            Repeater {
                model: ["#202427","#f5f1e8","#ffffff","#111111","#dfe9d4","#c8d7e9"]
                delegate: AbstractButton {
                    required property string modelData
                    implicitWidth: 34; implicitHeight: 34; hoverEnabled: true
                    Accessible.name: "背景颜色 " + modelData
                    background: Rectangle {
                        radius: 6; color: modelData
                        border.width: 2; border.color: parent.visualFocus || root.element.color === modelData ? StudioTheme.accent : StudioTheme.line
                    }
                    onClicked: studio.timeline.setValue("background","color",modelData)
                    ToolTip.visible: hovered; ToolTip.text: modelData; ToolTip.delay: 500
                }
            }
        }
        StudioButton { visible: root.element.kind === "image"; Layout.fillWidth: true; text: "更换背景图"; iconName: "folder"; onClicked: imageFile.open() }
        Text { Layout.fillWidth: true; text: "背景位于所有对象下方，覆盖整段视频。"; color: StudioTheme.muted; font.pixelSize: 11; lineHeight: 1.5; wrapMode: Text.Wrap }
        Item { Layout.fillHeight: true }
    }
}
