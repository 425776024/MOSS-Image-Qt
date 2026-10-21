import QtQuick
import QtQuick.Effects

Rectangle {
    id: root
    property Item backdropSource
    property real tintOpacity: StudioTheme.dark ? 0.74 : 0.94
    property real frostAmount: 0.85
    color: Qt.alpha(StudioTheme.panel, tintOpacity)
    radius: 13
    border.color: StudioTheme.glassLine

    // Capture only the canvas under this panel, never the floating controls.
    ShaderEffectSource {
        id: backdrop
        sourceItem: root.visible ? root.backdropSource : null
        sourceRect: {
            const source = root.backdropSource
            if (!source) return Qt.rect(0, 0, 1, 1)
            // Register the geometry dependencies used by mapToItem().
            const geometry = [root.x, root.y, root.width, root.height,
                              source.x, source.y, source.width, source.height]
            const point = root.mapToItem(source, 0, 0)
            return Qt.rect(point.x, point.y, geometry[2], geometry[3])
        }
        textureSize: Qt.size(Math.max(1, Math.ceil(root.width)), Math.max(1, Math.ceil(root.height)))
        live: root.visible
        hideSource: false
        visible: false
    }
    Rectangle {
        id: roundedMask
        width: root.width; height: root.height; radius: root.radius
        color: "white"; visible: false
        layer.enabled: true
    }
    MultiEffect {
        objectName: "frostedBackdrop"
        anchors.fill: parent; z: -1
        visible: root.visible && root.backdropSource !== null
        source: backdrop
        autoPaddingEnabled: false
        blurEnabled: true; blurMax: 24; blur: root.frostAmount
        saturation: -0.15
        maskEnabled: true; maskSource: roundedMask
    }
}
