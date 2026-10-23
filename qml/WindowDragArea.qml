import QtQuick

MouseArea {
    id: root
    readonly property var window: Window.window
    property point pressGlobal
    property point pressWindow
    acceptedButtons: Qt.LeftButton
    enabled: !!window && window.visibility!==Window.FullScreen
    function moveWindow(mouse) {
        if(Qt.platform.os!=="osx" || !window)return
        const point=mapToGlobal(mouse.x,mouse.y)
        window.x=pressWindow.x+point.x-pressGlobal.x
        window.y=pressWindow.y+point.y-pressGlobal.y
    }
    onPressed: mouse=>{
        pressGlobal=mapToGlobal(mouse.x,mouse.y);pressWindow=Qt.point(window.x,window.y)
        if(Qt.platform.os!=="osx")window.startSystemMove()
    }
    onPositionChanged: mouse=>{if(pressed)moveWindow(mouse)}
    onReleased: mouse=>moveWindow(mouse)
    onDoubleClicked: {
        if(!window)return
        if(window.visibility===Window.Maximized)window.showNormal()
        else window.showMaximized()
    }
}
