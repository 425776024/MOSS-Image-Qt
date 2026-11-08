import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: root
    objectName: "projectTabs"
    required property string currentId
    readonly property var pages: studio.creationPages
    readonly property var current: pages.find(p=>p.id===currentId) || ({})
    readonly property var others: pages.filter(p=>p.id!==currentId)
    readonly property var ordered: current.id ? [current].concat(others) : others
    signal renameRequested(string id)
    spacing: 4
    implicitHeight: 32

    function pageIndex(id) { return studio.creationPages.findIndex(p=>p.id===id) }
    function open(id) { root.forceActiveFocus(); studio.openConversation(id) }
    function remove(id) {
        if(studio.busy)return
        const index=pageIndex(id)
        if(index>=0){root.forceActiveFocus();studio.deletePage(index)}
    }
    function showActions(id,item) {
        actions.targetId=id
        const point=item.mapToItem(root,0,item.height)
        actions.popup(point.x,point.y+4)
    }

    component ProjectTab: AbstractButton {
        id: card
        required property var project
        readonly property bool selected: project.id===root.currentId
        readonly property string title: HomeTheme.projectName(project)
        readonly property bool showDelete: hovered || activeFocus || deleteButton.hovered || deleteButton.activeFocus
        objectName: "projectTab-"+(project.id || "")
        implicitWidth: Math.max(88,Math.min(180,Math.ceil(nameMetrics.advanceWidth)+58)); implicitHeight: 32
        TextMetrics { id: nameMetrics; text: card.title; font: VideoCutTheme.compactButtonFont }
        hoverEnabled: true; focusPolicy: Qt.StrongFocus; activeFocusOnTab: true
        Accessible.name: title; Accessible.selected: selected
        onClicked: if(!selected)root.open(project.id)
        onDoubleClicked: if(selected && !studio.busy)root.renameRequested(project.id)
        Keys.onPressed: event=>{
            if(event.key===Qt.Key_F2 && !studio.busy){root.renameRequested(project.id);event.accepted=true}
            else if(event.key===Qt.Key_Menu || (event.key===Qt.Key_F10 && event.modifiers & Qt.ShiftModifier)){
                root.showActions(project.id,card);event.accepted=true
            }
        }
        TapHandler { acceptedButtons: Qt.RightButton; onTapped: root.showActions(card.project.id,card) }
        ToolTip.visible: hovered && !deleteButton.hovered
        ToolTip.text: selected ? title+"\n"+HomeTheme.t("双击重命名") : title
        ToolTip.delay: 700
        background: Rectangle {
            radius: 5
            color: card.selected ? Qt.alpha(HomeTheme.text,HomeTheme.dark ? .10 : .06) : card.hovered ? HomeTheme.hover : "transparent"
            border.color: card.visualFocus ? HomeTheme.accent : "transparent"
        }
        contentItem: RowLayout {
            spacing: 6
            Rectangle {
                Layout.leftMargin: 6; Layout.preferredWidth: 20; Layout.preferredHeight: 20
                radius: 3; clip: true; color: HomeTheme.preview
                Image {
                    id: thumbnail; objectName: "projectThumbnail"
                    anchors.fill: parent; source: card.project.visual || card.project.image || ""
                    fillMode: Image.PreserveAspectCrop; sourceSize: Qt.size(80,80)
                    asynchronous: true; retainWhileLoading: true
                }
                StudioIcon { anchors.centerIn: parent; width: 16; height: 16; name: "document"; ink: HomeTheme.secondary; visible: thumbnail.status!==Image.Ready }
            }
            Text {
                Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.rightMargin: 26
                text: card.title; color: HomeTheme.text; font.pixelSize: 12; font.weight: Font.Medium
                elide: Text.ElideRight
            }
        }
        IconButton {
            id: deleteButton; objectName: "deleteProject-"+(card.project.id || "")
            anchors.right: parent.right; anchors.rightMargin: 2; anchors.top: parent.top; anchors.topMargin: 2
            width: 24; height: 28; iconSize: 14; glyph: "trash"
            opacity: card.showDelete ? 1 : 0; enabled: !studio.busy
            focusPolicy: Qt.StrongFocus; activeFocusOnTab: true
            iconColor: hovered ? HomeTheme.danger : HomeTheme.secondary
            hint: HomeTheme.t("删除作品")+"："+card.title
            onClicked: root.remove(card.project.id)
        }
    }

    ProjectTab {
        objectName: "currentProjectTab"
        project: root.current
        Layout.preferredWidth: implicitWidth; Layout.preferredHeight: 32
    }
    ListView {
        id: list; objectName: "otherProjectTabs"
        Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredHeight: 32
        orientation: ListView.Horizontal; spacing: 4; clip: true
        model: root.visible ? root.others : []; boundsBehavior: Flickable.StopAtBounds
        onModelChanged: positionViewAtBeginning()
        delegate: ProjectTab {
            required property var modelData
            project: modelData; width: implicitWidth; height: 32
        }
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded; height: 3 }
        WheelHandler {
            target: null
            onWheel: event=>{
                const delta=event.pixelDelta.x || event.pixelDelta.y || (event.angleDelta.x || event.angleDelta.y)/3
                list.contentX=Math.max(0,Math.min(Math.max(0,list.contentWidth-list.width),list.contentX-delta))
                event.accepted=true
            }
        }
    }
    IconButton {
        objectName: "allProjectsTrigger"; visible: root.pages.length>1
        glyph: "chevron"; hint: HomeTheme.t("全部作品")
        onClicked: allProjects.popup(width-allProjects.width,height+4)
        HomeMenu {
            id: allProjects; objectName: "allProjectsMenu"
            Instantiator {
                model: root.visible ? root.ordered : []
                delegate: AppMenuItem {
                    required property var modelData
                    height: 28
                    text: HomeTheme.projectName(modelData); checkable: true; checked: modelData.id===root.currentId
                    onTriggered: {const id=modelData.id;allProjects.close();root.open(id)}
                }
                onObjectAdded: (index,object)=>allProjects.insertItem(index,object)
                onObjectRemoved: (index,object)=>allProjects.removeItem(object)
            }
        }
    }
    IconButton {
        objectName: "homeCreateProject"; glyph: "compose"; hint: HomeTheme.t("新建作品")
        onClicked: {root.forceActiveFocus();studio.newConversation()}
    }
    IconButton {
        objectName: "undoProjectDeletion"; visible: studio.canRestorePage; enabled: !studio.busy
        glyph: "undo"; hint: HomeTheme.t("撤销删除"); onClicked: studio.restoreDeletedPage()
    }
    HomeMenu {
        id: actions; objectName: "projectTabMenu"; property string targetId: ""
        AppMenuItem { text: HomeTheme.t("重命名"); enabled: !studio.busy; onTriggered: root.renameRequested(actions.targetId) }
        AppMenuItem { text: HomeTheme.t("复制作品"); enabled: !studio.busy; onTriggered: {const index=root.pageIndex(actions.targetId);if(index>=0)studio.duplicatePage(index)} }
        AppMenuItem { text: HomeTheme.t("删除作品"); destructive: true; enabled: !studio.busy; onTriggered: root.remove(actions.targetId) }
    }
}
