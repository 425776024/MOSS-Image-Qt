import QtQuick
import QtQuick.Controls
Item {
    id: root
    property var document
    property bool drawMode: false
    property string drawType: "object"
    property string imageSource: ""
    property bool showRegions: true
    property real canvasPadding: 32
    property bool compact: false
    property real scaleFactor: Math.min((width-canvasPadding*2)/document.canvasWidth,(height-canvasPadding*2)/document.canvasHeight)
    property alias paper: paper
    Rectangle {
        anchors.fill: parent
        color: StudioTheme.background
    }
    Rectangle {
        id: paper
        objectName: "layoutPaper"
        anchors.centerIn: parent
        width: document.canvasWidth*root.scaleFactor
        height: document.canvasHeight*root.scaleFactor
        color: root.imageSource !== "" || document.layout ? "#FFFFFF" : StudioTheme.canvas
        border.color: StudioTheme.line
        Image { anchors.fill: parent; source: root.imageSource; fillMode: Image.PreserveAspectFit; asynchronous: true }
        Column {
            anchors.centerIn: parent; width: parent.width-12; spacing: root.compact ? 6 : 12
            visible: root.imageSource==="" && (!document.layout || document.regions.length===0)
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: document.layout ? "▧" : "✦"; font.pixelSize: 40; color: StudioTheme.muted }
            Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; text: document.layout ? HomeTheme.t("在画布上画出你的构想") : HomeTheme.t("让想象成为画面"); color: StudioTheme.secondary; font.pixelSize: root.compact ? 13 : 17 }
            Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; text: document.layout ? HomeTheme.t("选择画框工具，拖动添加对象或文字区域") : HomeTheme.t("填写下方描述，开始生成"); color: StudioTheme.muted; font.pixelSize: root.compact ? 10 : 12 }
        }
        MouseArea {
            id: drawing
            anchors.fill: parent
            enabled: document.layout
            cursorShape: root.drawMode ? Qt.CrossCursor : Qt.ArrowCursor
            property real startX: 0
            property real startY: 0
            property real endX: 0
            property real endY: 0
            onPressed: mouse => {root.forceActiveFocus();
                if(!root.drawMode){document.selected=-1;return;}
                startX=mouse.x;startY=mouse.y;endX=mouse.x;endY=mouse.y;
            }
            onPositionChanged: mouse => {endX=Math.max(0,Math.min(width,mouse.x));endY=Math.max(0,Math.min(height,mouse.y));}
            onReleased: mouse => {
                if(root.drawMode){
                    const x=Math.min(startX,endX)/root.scaleFactor,y=Math.min(startY,endY)/root.scaleFactor;
                    document.addRegion(x,y,Math.abs(endX-startX)/root.scaleFactor,Math.abs(endY-startY)/root.scaleFactor,root.drawType);
                    root.drawMode=false;
                }
            }
        }
        Repeater {
            model: document.layout && root.showRegions ? document.regions : []
            delegate: Rectangle {
                id: region
                required property var modelData
                required property int index
                property bool selected: document.selected===index
                z: selected ? 1 : 0
                x: modelData.x*root.scaleFactor
                y: modelData.y*root.scaleFactor
                width: modelData.width*root.scaleFactor
                height: modelData.height*root.scaleFactor
                color: Qt.rgba(border.color.r,border.color.g,border.color.b,root.imageSource==="" ? .055 : .025)
                border.width: selected ? 2 : 1.2
                border.color: modelData.color
                Rectangle {
                    x: 0; y: 0; height: 21; width: Math.min(parent.width,tag.implicitWidth+14)
                    color: region.modelData.color
                    Text {id:tag;anchors.fill:parent;anchors.leftMargin:6;verticalAlignment:Text.AlignVCenter;
                        text:region.modelData.name;font.pixelSize:11;color:"white";elide:Text.ElideRight}
                }
                Text {
                    anchors.fill: parent; anchors.topMargin: 25; anchors.bottomMargin: 4; anchors.leftMargin: 6; anchors.rightMargin: 6
                    visible: parent.height > 36
                    text: region.modelData.type==="text" ? region.modelData.text : ""
                    color: region.modelData.color; font.pixelSize: 18; minimumPixelSize: 7; fontSizeMode: Text.Fit
                    wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; clip: true
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: !root.drawMode
                    cursorShape: Qt.SizeAllCursor
                    property real originX
                    property real originY
                    property real pointerX
                    property real pointerY
                    onPressed: mouse => {root.forceActiveFocus();
                        document.selected=region.index;
                        const p=mapToItem(paper,mouse.x,mouse.y);
                        pointerX=p.x;pointerY=p.y;originX=region.x;originY=region.y;
                    }
                    onPositionChanged: mouse => {
                        if(pressed){
                            const p=mapToItem(paper,mouse.x,mouse.y);
                            region.x=Math.max(0,Math.min(paper.width-region.width,originX+p.x-pointerX));
                            region.y=Math.max(0,Math.min(paper.height-region.height,originY+p.y-pointerY));
                        }
                    }
                    onReleased: document.moveRegion(region.index,region.x/root.scaleFactor,region.y/root.scaleFactor,region.width/root.scaleFactor,region.height/root.scaleFactor)
                }
                Rectangle {
                    visible: region.selected
                    width:10;height:10;x:parent.width-5;y:parent.height-5;color:"white";border.color:region.modelData.color;border.width:2
                    MouseArea {
                        anchors.fill:parent;anchors.margins:-5;cursorShape:Qt.SizeFDiagCursor
                        enabled: !root.drawMode
                        property real px;property real py;property real initialW;property real initialH
                        onPressed: mouse => {root.forceActiveFocus();const p=mapToItem(paper,mouse.x,mouse.y);px=p.x;py=p.y;initialW=region.width;initialH=region.height;}
                        onPositionChanged: mouse => {if(pressed){const p=mapToItem(paper,mouse.x,mouse.y);region.width=Math.max(12,Math.min(paper.width-region.x,initialW+p.x-px));region.height=Math.max(12,Math.min(paper.height-region.y,initialH+p.y-py));}}
                        onReleased: document.moveRegion(region.index,region.x/root.scaleFactor,region.y/root.scaleFactor,region.width/root.scaleFactor,region.height/root.scaleFactor)
                    }
                }
            }
        }
        Rectangle {
            z: 2
            visible: drawing.pressed && root.drawMode
            x:Math.min(drawing.startX,drawing.endX);y:Math.min(drawing.startY,drawing.endY)
            width:Math.abs(drawing.endX-drawing.startX);height:Math.abs(drawing.endY-drawing.startY)
            color:"#185064EB";border.color:StudioTheme.accent;border.width:2
        }
    }
    Text {
        visible: !root.compact
        anchors.bottom:parent.bottom;anchors.bottomMargin:root.compact ? 2 : 9;anchors.horizontalCenter:parent.horizontalCenter
        text:document.canvasWidth+" × "+document.canvasHeight+"  ·  "+Math.round(root.scaleFactor*100)+"%"
        font.pixelSize:11;color:StudioTheme.muted
    }
    Rectangle {
        visible: root.compact; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 4
        width: zoomLabel.implicitWidth+10; height: 18; radius: 4; color: Qt.alpha(StudioTheme.background,.90)
        Text { id: zoomLabel; anchors.centerIn: parent; text: Math.round(root.scaleFactor*100)+"%"; font.pixelSize: 10; color: StudioTheme.muted }
    }
}
