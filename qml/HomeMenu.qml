import QtQuick
import QtQuick.Controls

Menu {
    padding: 5
    margins: 8
    implicitWidth: 208
    delegate: AppMenuItem { }
    background: Rectangle {
        radius: 8
        color: HomeTheme.elevated
        border.color: HomeTheme.outline
    }
}
