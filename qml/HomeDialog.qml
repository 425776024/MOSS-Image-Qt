import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Dialog {
    id: root
    property string dialogIcon: ""
    property string subtitle: ""
    property color tone: HomeTheme.accent
    property bool showCloseButton: false
    property var defaultAction: null
    modal: true
    focus: true
    padding: 20
    topPadding: 16
    width: Math.min(440,Overlay.overlay ? Overlay.overlay.width-40 : 440)
    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    closePolicy: Popup.CloseOnEscape
    font.family: ".AppleSystemUIFont"
    font.pixelSize: 13
    Overlay.modal: Rectangle { color: "#33000000" }
    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: root.opened && root.activeFocus && root.defaultAction !== null && root.defaultAction.enabled && root.defaultAction.visible
            && !(root.contentItem.Window.window && root.contentItem.Window.window.activeFocusItem instanceof TextEdit)
            && !(root.contentItem.Window.window && root.contentItem.Window.window.activeFocusItem && root.contentItem.Window.window.activeFocusItem.inputMethodComposing === true)
        onActivated: root.defaultAction.clicked()
    }
    Shortcut { sequence: "Meta+."; enabled: root.opened && root.activeFocus && root.closePolicy !== Popup.NoAutoClose; onActivated: root.reject() }
    background: Rectangle {
        id: sheet
        radius: 12
        color: HomeTheme.dialogSurface
        border.color: HomeTheme.dialogBorder
        border.width: VideoCutTheme.hairline
        Repeater {
            model: 16
            Rectangle {
                required property int index
                readonly property real spread: index+1
                z: -1; x: -spread; y: 5-spread
                width: sheet.width+spread*2; height: sheet.height+spread*2
                radius: sheet.radius+spread; color: "#02000000"
            }
        }
    }
    header: Item {
        implicitHeight: titleRow.implicitHeight+20
        RowLayout {
            id: titleRow
            anchors { left: parent.left; right: parent.right; top: parent.top; leftMargin: 20; rightMargin: 20; topMargin: 20 }
            spacing: 8
            StudioIcon {
                visible: root.dialogIcon.length>0
                Layout.preferredWidth: 22; Layout.preferredHeight: 22
                name: root.dialogIcon; ink: root.tone
            }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 4
                Label { Layout.fillWidth: true; text: root.title; textFormat: Text.PlainText; color: HomeTheme.text; font.pixelSize: 13; font.weight: Font.DemiBold; wrapMode: Text.Wrap }
                Label { visible: root.subtitle.length>0; Layout.fillWidth: true; text: root.subtitle; textFormat: Text.PlainText; color: HomeTheme.secondary; font.pixelSize: 11; wrapMode: Text.Wrap }
            }
            IconButton { visible: root.showCloseButton && root.closePolicy !== Popup.NoAutoClose; glyph: "close"; hint: HomeTheme.t("关闭"); onClicked: root.reject() }
        }
    }
    footer: DialogButtonBox {
        visible: count>0
        standardButtons: root.standardButtons
        spacing: 8
        padding: 0; leftPadding: 20; rightPadding: 20; topPadding: 0; bottomPadding: 16
        background: Item {}
        delegate: HomeButton {}
    }
}
