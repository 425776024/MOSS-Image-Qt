// Adapted from VideoCut-Qt app/qml/controls/AppSlider.qml, at the user's request.
// Standalone Moss palette and icon bridge; native availability stays with each caller.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

// Retains Slider's user-only moved signal and standard range semantics. The
// caller owns authoritative value bindings and any Begin/Commit/Cancel flow.
Slider {
    id: slider
    readonly property bool appearanceEnabled: slider.enabled
    property bool hasDisplayValue: true
    // Attribute editors opt into a filled value rail. Transport and mixer
    // sliders retain their existing specialised track presentation.
    property bool integrated: false
    property bool showProgress: true
    property real trackThickness: 2
    property real handleWidth: 6
    property real handleHeight: 12
    property real handleRadius: 2
    property color thumbColor: VideoCutTheme.text
    property color accentColor: VideoCutTheme.accent
    property color disabledAccentColor: VideoCutTheme.muted
    property Gradient trackGradient: null
    // Presentation only: signed ranges expose their neutral value without
    // changing Slider's range, movement signals, snapping or SDK transactions.
    property bool showNeutral: Math.min(from, to) < 0 && Math.max(from, to) > 0
    property real neutralValue: 0
    readonly property bool neutralVisible: showNeutral && hasDisplayValue && from !== to
        && Number.isFinite(from) && Number.isFinite(to) && Math.abs(neutralValue) < Infinity
        && neutralValue >= Math.min(from, to) && neutralValue <= Math.max(from, to)
    readonly property real neutralPosition: from === to ? 0 : Math.max(0, Math.min(1, (neutralValue - from) / (to - from)))
    readonly property real neutralVisualPosition: verticalTrack || mirrored ? 1 - neutralPosition : neutralPosition
    readonly property real progressOrigin: neutralVisible ? neutralVisualPosition : verticalTrack || mirrored ? 1 : 0
    readonly property real progressStart: Math.min(progressOrigin, visualPosition)
    readonly property real progressExtent: Math.abs(visualPosition - progressOrigin)
    readonly property bool verticalTrack: orientation === Qt.Vertical
    hoverEnabled: true
    implicitWidth: verticalTrack ? VideoCutTheme.controlHeight : 120
    implicitHeight: verticalTrack ? 120 : VideoCutTheme.controlHeight
    padding: 2.5
    leftPadding: padding
    rightPadding: padding
    topPadding: padding
    bottomPadding: padding
    background: Rectangle {
        x: slider.verticalTrack ? (slider.width - width) / 2 : slider.leftPadding
        y: slider.verticalTrack ? slider.topPadding : (slider.height - height) / 2
        width: slider.verticalTrack ? slider.trackThickness : slider.availableWidth
        height: slider.verticalTrack ? slider.availableHeight
            : slider.integrated ? Math.min(slider.height, VideoCutTheme.inputHeight) : slider.trackThickness
        radius: slider.integrated ? 4 : slider.trackThickness / 2
        color: slider.integrated ? VideoCutTheme.sliderField : VideoCutTheme.line
        gradient: slider.trackGradient
        opacity: slider.appearanceEnabled || !slider.trackGradient ? 1 : 0.45
        Rectangle {
            visible: slider.hasDisplayValue && slider.showProgress
            readonly property real inset: slider.integrated && !slider.verticalTrack ? 2 : 0
            width: slider.verticalTrack ? parent.width : Math.max(0, parent.width - 2 * inset) * slider.progressExtent
            height: slider.verticalTrack ? parent.height * slider.progressExtent : parent.height - 2 * inset
            x: slider.verticalTrack ? 0 : inset + (parent.width - 2 * inset) * slider.progressStart
            y: slider.verticalTrack ? parent.height * slider.progressStart : inset
            radius: slider.integrated ? 3 : slider.trackThickness / 2
            color: slider.integrated && !slider.trackGradient
                ? VideoCutTheme.parameterProgress.alpha(VideoCutTheme.parameterProgress.a * (slider.appearanceEnabled ? slider.pressed || slider.hovered ? 1.4 : 1 : 0.5))
                : !slider.appearanceEnabled ? slider.disabledAccentColor.alpha(slider.integrated ? 0.12 : 1)
                : slider.integrated ? slider.accentColor.alpha(slider.pressed || slider.hovered ? 0.26 : 0.18) : slider.accentColor
        }
        Rectangle {
            objectName: "sliderNeutralMark"
            visible: slider.neutralVisible
            width: slider.verticalTrack ? 8 : VideoCutTheme.hairline
            height: slider.verticalTrack ? VideoCutTheme.hairline : slider.integrated ? 12 : 8
            x: slider.verticalTrack ? (parent.width - width) / 2 : slider.neutralVisualPosition * parent.width - width / 2
            y: slider.verticalTrack ? slider.neutralVisualPosition * parent.height - height / 2 : (parent.height - height) / 2
            color: slider.integrated ? VideoCutTheme.line : slider.appearanceEnabled ? VideoCutTheme.secondary : VideoCutTheme.muted
        }
    }
    handle: Rectangle {
        visible: slider.hasDisplayValue
        x: slider.verticalTrack ? (slider.width - width) / 2
            : slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
        y: slider.verticalTrack ? slider.topPadding + slider.visualPosition * (slider.availableHeight - height)
            : (slider.height - height) / 2
        width: slider.verticalTrack ? slider.handleHeight : slider.integrated ? slider.visualFocus || slider.pressed ? 4 : 3 : slider.handleWidth
        height: slider.verticalTrack ? slider.handleWidth : slider.integrated ? slider.visualFocus || slider.pressed ? 18 : slider.hovered ? 16 : 12 : slider.handleHeight
        radius: slider.integrated ? 1.5 : slider.handleRadius
        color: !slider.appearanceEnabled ? slider.disabledAccentColor
            : slider.integrated && !slider.trackGradient
                ? slider.visualFocus || slider.pressed ? slider.accentColor : VideoCutTheme.secondary
            : slider.pressed || slider.hovered ? slider.thumbColor.lighter(1.12) : slider.thumbColor
        // Keep keyboard focus visible on the thumb without outlining the row.
        scale: !slider.integrated && slider.enabled && slider.visualFocus ? 1.25 : 1
    }
    HoverHandler {
        cursorShape: slider.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }
}
