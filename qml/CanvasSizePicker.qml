import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AbstractButton {
    id: root
    required property var document
    readonly property var presets: [
        {name:"方形",w:512,h:512},{name:"方形",w:1024,h:1024},{name:"方形",w:1536,h:1536},
        {name:"方形",w:2048,h:2048},{name:"竖版 3:4",w:768,h:1024},{name:"横版 4:3",w:1024,h:768},
        {name:"海报 4:5",w:1024,h:1280},{name:"竖屏 9:16",w:720,h:1280},{name:"宽屏 16:9",w:1280,h:720}
    ]
    readonly property var current: presets.find(p=>p.w===document.canvasWidth && p.h===document.canvasHeight)
    implicitWidth: 200; implicitHeight: 28; padding: 8; hoverEnabled: true
    Accessible.name: "画布尺寸"; Accessible.description: document.canvasWidth+" × "+document.canvasHeight
    onClicked: choices.open()
    background: Rectangle { radius: 6; color: root.hovered ? HomeTheme.hover : HomeTheme.field; border.color: root.visualFocus ? HomeTheme.accent : HomeTheme.outline }
    contentItem: RowLayout {
        spacing: 8
        Text { Layout.fillWidth: true; text: (root.current ? root.current.name : "自定义")+" · "+root.document.canvasWidth+" × "+root.document.canvasHeight; color: HomeTheme.text; font.pixelSize: 12; elide: Text.ElideRight }
        StudioIcon { Layout.preferredWidth: 12; Layout.preferredHeight: 12; name: "chevron"; ink: HomeTheme.secondary }
    }
    HomeDialog {
        id: choices; objectName: "canvasSizeChoices"; title: "画布尺寸"; width: Math.min(520,root.Window.window ? root.Window.window.width-40 : 520)
        ColumnLayout { width: parent.width; spacing: 12
            GridLayout { Layout.fillWidth: true; columns: 3; columnSpacing: 8; rowSpacing: 8
                Repeater { model: root.presets
                    AbstractButton {
                        id: option; required property var modelData
                        readonly property bool chosen: root.document.canvasWidth===modelData.w && root.document.canvasHeight===modelData.h
                        Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredWidth: 1; implicitHeight: 86; padding: 8; hoverEnabled: true
                        Accessible.name: modelData.name+" · "+modelData.w+" × "+modelData.h; Accessible.checked: chosen; Accessible.role: Accessible.RadioButton
                        onClicked: {root.document.setCanvasSize(modelData.w,modelData.h);choices.close()}
                        background: Rectangle { radius: 8; color: option.chosen ? HomeTheme.selection : option.hovered ? HomeTheme.hover : HomeTheme.field; border.color: option.chosen || option.visualFocus ? HomeTheme.accent : HomeTheme.outline }
                        contentItem: ColumnLayout { spacing: 4
                            Item { Layout.fillWidth: true; Layout.preferredHeight: 30
                                Rectangle { anchors.centerIn: parent; width: option.modelData.w>=option.modelData.h ? 30 : 30*option.modelData.w/option.modelData.h; height: option.modelData.h>=option.modelData.w ? 30 : 30*option.modelData.h/option.modelData.w; radius: 2; color: HomeTheme.preview; border.color: option.chosen ? HomeTheme.accent : HomeTheme.secondary }
                            }
                            Text { Layout.alignment: Qt.AlignHCenter; text: option.modelData.name; color: HomeTheme.text; font.pixelSize: 11 }
                            Text { Layout.alignment: Qt.AlignHCenter; text: option.modelData.w+" × "+option.modelData.h; color: HomeTheme.secondary; font.pixelSize: 10 }
                        }
                    }
                }
            }
            RowLayout { Layout.fillWidth: true
                HomeButton { text: "自定义尺寸…"; quiet: true; onClicked: {choices.close();customSize.open()} }
                Item { Layout.fillWidth: true }
                HomeButton { text: "取消"; quiet: true; onClicked: choices.close() }
            }
        }
    }
    HomeDialog {
        id: customSize; objectName: "canvasCustomSize"; title: "自定义画布"; width: Math.min(360,root.Window.window ? root.Window.window.width-40 : 360)
        readonly property string sizeError: {
            const w=Number(customWidth.text),h=Number(customHeight.text)
            if(!Number.isInteger(w)||!Number.isInteger(h)||w<256||h<256||w>4096||h>4096)return "宽高须在 256–4096 px 之间"
            if(w%16||h%16)return "宽高须为 16 的倍数"
            if(w*h>4194304)return "总像素不能超过 4,194,304"
            return ""
        }
        onOpened: {customWidth.text=String(root.document.canvasWidth);customHeight.text=String(root.document.canvasHeight);customWidth.forceActiveFocus();customWidth.selectAll()}
        ColumnLayout { width: parent.width; spacing: 12
            RowLayout { Layout.fillWidth: true; spacing: 8
                HomeField { id: customWidth; Layout.fillWidth: true; Accessible.name: "自定义画布宽度"; validator: IntValidator { bottom: 1; top: 99999 } }
                Text { text: "×"; color: HomeTheme.secondary }
                HomeField { id: customHeight; Layout.fillWidth: true; Accessible.name: "自定义画布高度"; validator: IntValidator { bottom: 1; top: 99999 } }
                Text { text: "px"; color: HomeTheme.muted; font.pixelSize: 12 }
            }
            Text { Layout.fillWidth: true; text: customSize.sizeError || "尺寸改变后，布局会按比例缩放。"; color: customSize.sizeError ? HomeTheme.danger : HomeTheme.secondary; font.pixelSize: 11; wrapMode: Text.Wrap }
            RowLayout { Layout.alignment: Qt.AlignRight
                HomeButton { text: "取消"; quiet: true; onClicked: customSize.close() }
                HomeButton { text: "应用"; primary: true; enabled: !customSize.sizeError; onClicked: {root.document.setCanvasSize(Number(customWidth.text),Number(customHeight.text));customSize.close()} }
            }
        }
    }
}
