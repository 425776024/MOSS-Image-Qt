import QtQuick
import QtQuick.Layouts

ColumnLayout {
    property string title: ""
    property string description: ""
    property string iconName: "folder"
    spacing: 10
    Rectangle { Layout.alignment: Qt.AlignHCenter; width: 56; height: 56; radius: 15; color: HomeTheme.selection
        StudioIcon { anchors.centerIn: parent; width: 27; height: 27; name: parent.parent.iconName; ink: HomeTheme.accent }
    }
    Text { Layout.alignment: Qt.AlignHCenter; text: HomeTheme.t(parent.title); font.pixelSize: 15; font.weight: Font.Medium; color: HomeTheme.text }
    Text { visible: !!parent.description; Layout.fillWidth: true; Layout.maximumWidth: 340; Layout.alignment: Qt.AlignHCenter; text: HomeTheme.t(parent.description); horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; font.pixelSize: 12; color: HomeTheme.secondary }
}
