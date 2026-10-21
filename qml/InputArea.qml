import QtQuick
import QtQuick.Controls
Rectangle {
    id: root
    property string value: ""
    property string placeholder: ""
    property string label: ""
    property bool readOnly: false
    property bool editing: editor.activeFocus
    property alias editor: editor
    property alias font: editor.font
    property bool syncing: false
    signal edited(string text)
    implicitHeight: 140
    radius: 4; color: StudioTheme.field
    border.color: editor.activeFocus ? StudioTheme.accent : StudioTheme.line
    onValueChanged: sync()
    Component.onCompleted: sync()
    function sync() {
        if (editor.text === value) return
        syncing = true
        editor.text = value
        syncing = false
    }
    ScrollView {
        anchors.fill: parent; anchors.margins: 3; clip: true
        TextArea {
            id: editor
            objectName: root.objectName + "Text"
            Accessible.name: root.label
            readOnly: root.readOnly
            selectByMouse: true
            wrapMode: TextArea.Wrap
            font.pixelSize: 12
            padding: 5
            color: enabled ? StudioTheme.text : StudioTheme.faint
            selectionColor: StudioTheme.selection
            selectedTextColor: StudioTheme.text
            placeholderText: root.placeholder
            placeholderTextColor: StudioTheme.muted
            background: null
            onTextChanged: if (!root.syncing && activeFocus) root.edited(text)
        }
    }
}
