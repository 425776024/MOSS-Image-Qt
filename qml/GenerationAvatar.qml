import QtQuick
import QtQuick.Controls.Basic

Control {
    id: root
    property string task: ""
    property bool selected: false
    readonly property var styles: ({
        image: {icon: "image", color: "#2878cf"},
        edit: {icon: "brush", color: "#8956c5"},
        avatar: {icon: "person", color: "#c26a28"},
        voice: {icon: "microphone", color: "#0d8c82"},
        sfx: {icon: "audio", color: "#c55176"},
        pack: {icon: "layers", color: "#5264c4"}
    })
    readonly property var style: styles[task] || ({icon: "image", color: "#737b86"})
    readonly property color tint: HomeTheme.dark ? Qt.lighter(style.color,1.45) : style.color
    implicitWidth: 30; implicitHeight: 30
    visible: HomeTheme.taskLabel(task).length > 0
    hoverEnabled: true; padding: 6
    Accessible.role: Accessible.Graphic
    Accessible.name: HomeTheme.taskLabel(task)
    background: Rectangle {
        radius: width/2; color: Qt.alpha(root.tint,HomeTheme.dark ? .18 : .10)
        border.color: Qt.alpha(root.tint,root.selected ? .75 : .20)
    }
    contentItem: StudioIcon { name: root.style.icon; ink: root.tint }
    ToolTip.visible: hovered
    ToolTip.text: HomeTheme.taskLabel(task)
    ToolTip.delay: 400
}
