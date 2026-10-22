import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    property url source
    readonly property bool ready: portrait.status === Image.Ready
    readonly property bool loading: portrait.status === Image.Loading
    readonly property size imageSize: Qt.size(portrait.sourceSize.width,portrait.sourceSize.height)
    implicitHeight: 196
    radius: 10
    color: HomeTheme.dialogInset
    border.color: HomeTheme.outline
    Image {
        id: portrait; objectName: "avatarImportPreviewImage"
        anchors.fill: parent; anchors.margins: 8
        source: root.source; fillMode: Image.PreserveAspectFit
        autoTransform: true; asynchronous: true
    }
    BusyIndicator { anchors.centerIn: parent; running: root.loading; visible: running; width: 28; height: 28 }
    ColumnLayout {
        anchors.centerIn: parent; width: parent.width-32; spacing: 8
        visible: !root.ready && !root.loading
        StudioIcon { Layout.alignment: Qt.AlignHCenter; name: "image"; ink: HomeTheme.muted; width: 24; height: 24 }
        Label { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: HomeTheme.t(root.source.toString() ? "无法读取图片，请重新选择" : "选择图片后在这里预览"); wrapMode: Text.Wrap; color: HomeTheme.secondary; font.pixelSize: 12 }
    }
}
