import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    color: StudioTheme.panel
    property var doc: studio.layerDocument
    readonly property var selected: doc.selected >= 0 && doc.selected < doc.layers.length ? doc.layers[doc.selected] : null
    ScrollView {
        anchors.fill: parent; anchors.margins: 12; contentWidth: availableWidth; clip: true
        ColumnLayout {
            width: parent.width; spacing: 10; enabled: !studio.busy
            Repeater {
                model: root.doc.layers.slice().reverse()
                delegate: StudioButton {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true; implicitHeight: 34
                    text: modelData.name; iconName: "image"; active: root.doc.selected === root.doc.layers.length - index - 1
                    onClicked: root.doc.selected = root.doc.layers.length - index - 1
                }
            }
            Text { visible: !root.selected; Layout.fillWidth: true; text: root.doc.layers.length ? HomeTheme.t("选择画布对象以调整") : HomeTheme.t("导入图片或 SVG 后，可直接拖动和缩放。"); wrapMode: Text.Wrap; color: StudioTheme.muted; font.pixelSize: 12 }
            ColumnLayout {
                visible: !!root.selected; Layout.fillWidth: true; spacing: 10
                StudioField { Layout.fillWidth: true; Accessible.name: HomeTheme.t("对象名称"); text: root.selected ? root.selected.name : ""; onEditingFinished: root.doc.updateLayer(root.doc.selected,"name",text) }
                GridLayout {
                    Layout.fillWidth: true; columns: 2; rowSpacing: 8; columnSpacing: 8
                    Repeater {
                        model: [{key:"x",label:HomeTheme.t("X 位置")},{key:"y",label:HomeTheme.t("Y 位置")},{key:"width",label:HomeTheme.t("宽度")},{key:"height",label:HomeTheme.t("高度")}]
                        delegate: ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true; spacing: 4
                            Text { text: modelData.label; color: StudioTheme.muted; font.pixelSize: 11 }
                            StudioField {
                                Layout.fillWidth: true; Accessible.name: modelData.label
                                text: root.selected ? String(Math.round(root.selected[modelData.key])) : ""
                                validator: IntValidator { bottom: modelData.key === "width" || modelData.key === "height" ? 1 : -16384; top: 16384 }
                                onEditingFinished: {if(acceptableInput)root.doc.updateLayer(root.doc.selected,modelData.key,Number(text));text=Qt.binding(()=>root.selected?String(Math.round(root.selected[modelData.key])):"")}
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    StudioButton { Layout.fillWidth: true; text: HomeTheme.t("居中"); onClicked: root.doc.centerSelected() }
                    StudioButton { Layout.fillWidth: true; text: HomeTheme.t("适应画布"); onClicked: root.doc.fitSelected() }
                }
                RowLayout {
                    Layout.fillWidth: true
                    StudioButton { Layout.fillWidth: true; text: HomeTheme.t("上移"); enabled: root.doc.selected < root.doc.layers.length-1; onClicked: root.doc.moveSelected(1) }
                    StudioButton { Layout.fillWidth: true; text: HomeTheme.t("下移"); enabled: root.doc.selected > 0; onClicked: root.doc.moveSelected(-1) }
                    StudioButton { Layout.fillWidth: true; text: HomeTheme.t("复制"); onClicked: root.doc.duplicateSelected() }
                    StudioButton { Layout.fillWidth: true; text: HomeTheme.t("删除"); onClicked: root.doc.removeSelected() }
                }
            }
        }
    }
}
