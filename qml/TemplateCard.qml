import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AbstractButton {
    id: control
    property string kind: "poster"
    implicitHeight: 104
    implicitWidth: 82
    hoverEnabled: true
    Accessible.name: text + "示例，应用后可以撤销"
    ToolTip.visible: hovered
    ToolTip.delay: 600
    ToolTip.text: Accessible.name
    background: Rectangle {
        color: control.hovered ? StudioTheme.hover : StudioTheme.elevated
        border.color: control.visualFocus ? StudioTheme.accent : "transparent"
        radius: 7
    }
    contentItem: ColumnLayout {
        spacing: 6
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true
            Layout.margins: 5; Layout.bottomMargin: 0
            radius: 4; clip: true
            color: control.kind === "poster" ? "#D9D9B7" : control.kind === "note" ? "#E3D4BC" : "#C3D1CC"
            Rectangle {
                visible: control.kind === "poster"
                width: parent.width * 0.85; height: width; radius: width / 2
                x: parent.width * 0.4; y: parent.height * 0.3; color: "#6C7E52"
            }
            Column {
                visible: control.kind === "poster"; x: 8; y: 8; spacing: 2
                Text { text: "春日"; font.pixelSize: 18; font.weight: Font.Bold; color: "#314431" }
                Text { text: "SPRING / 01"; font.pixelSize: 6; font.letterSpacing: 1; color: "#314431" }
            }
            Column {
                visible: control.kind === "note"; anchors.centerIn: parent; rotation: -8; spacing: 4
                Text { text: "慢慢来"; font.pixelSize: 15; font.family: "Kaiti SC"; color: "#735941" }
                Rectangle { width: 42; height: 1; color: "#AA957A" }
                Text { text: "把日子写成诗"; font.pixelSize: 7; color: "#735941" }
            }
            Row {
                visible: control.kind === "objects"; anchors.centerIn: parent; spacing: 6
                Rectangle { width: 22; height: 34; radius: 7; color: "#5B7F82"; anchors.bottom: parent.bottom }
                Rectangle { width: 19; height: 19; radius: 3; color: "#B58B64"; anchors.bottom: parent.bottom }
            }
        }
        Text {
            Layout.alignment: Qt.AlignHCenter; Layout.bottomMargin: 8
            text: control.text; color: StudioTheme.secondary; font.pixelSize: 10
        }
    }
}
