// Adapted from VideoCut-Qt app/qml/controls/AppSelect.qml, at the user's request.
// Standalone Moss palette and icon bridge; native availability stays with each caller.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

// Standard ComboBox model/textRole/valueRole/currentIndex/activated contract.
// A model or currentIndex update has no authored or preference side effect.
ComboBox {
    id: input
    // The control owns its popup. Consumers observe these lifecycle signals
    // without resolving a deferred popup through a separate Connections object.
    signal popupOpening()
    signal popupClosed()
    readonly property bool appearanceEnabled: input.enabled
    property real surfaceRadius: 4
    property bool outlined: false
    property color surfaceColor: VideoCutTheme.field
    property color surfaceBorderColor: VideoCutTheme.line
    property color disabledTextColor: VideoCutTheme.muted
    property string indicatorGlyph: "choice-chevron"
    property real indicatorSize: VideoCutTheme.smallIconSize
    property real indicatorMargin: 7
    property real maximumPopupHeight: 320
    property real minimumPopupWidth: 88
    property real maximumPopupWidth: 280
    property real measuredPopupWidth: minimumPopupWidth
    property real optionHeight: 28
    function measurePopup(): void {
        let longest = 0;
        for (let index = 0; index < count; ++index)
            longest = Math.max(longest, popupFontMetrics.advanceWidth(textAt(index)));
        // Text inset and the closed field's chevron; selection uses highlighting.
        measuredPopupWidth = Math.max(minimumPopupWidth, longest + 32);
    }
    FontMetrics { id: popupFontMetrics; font: input.font }
    Component.onCompleted: measurePopup()
    onModelChanged: measurePopup()
    onCountChanged: measurePopup()
    onFontChanged: measurePopup()
    // Long names and wide parent layouts must not create oversized menus.
    implicitWidth: Math.max(96, Math.min(maximumPopupWidth, Math.ceil(measuredPopupWidth)))
    implicitHeight: VideoCutTheme.inputHeight
    leftPadding: 10
    rightPadding: 20
    topPadding: 0
    bottomPadding: 0
    font: VideoCutTheme.parameterFont
    hoverEnabled: true
    ToolTip.visible: hovered && selectedLabel.truncated
    ToolTip.text: displayText
    ToolTip.delay: VideoCutTheme.tooltipDelay
    contentItem: Text {
        id: selectedLabel
        text: input.displayText
        font: input.font
        color: input.appearanceEnabled ? VideoCutTheme.text : input.disabledTextColor
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: AppInputSurface {
        focused: input.activeFocus
        outlined: input.outlined
        baseColor: input.surfaceColor
        strokeColor: input.surfaceBorderColor
        radius: input.surfaceRadius
        hovered: input.hovered
        available: input.enabled
    }
    indicator: Glyph {
        width: input.indicatorSize
        height: input.indicatorSize
        x: input.width - width - input.indicatorMargin
        y: (input.height - height) / 2
        kind: input.indicatorGlyph
        color: VideoCutTheme.muted
    }
    delegate: AppMenuItem {
        required property int index
        required property var model
        width: input.popup.availableWidth
        height: input.optionHeight
        // Reading roles here subscribes to dataChanged; textAt(index) is an
        // imperative query and cannot keep an open popup's labels current.
        text: {
            const data = model.modelData;
            let value = data;
            if (input.textRole.length) {
                value = model[input.textRole];
                if (value === undefined && data !== null && typeof data === "object")
                    value = data[input.textRole];
            }
            return value === undefined || value === null ? "" : "" + value;
        }
        checkable: true
        selectionActive: input.currentIndex === index
        checked: selectionActive
        highlighted: input.highlightedIndex === index
    }
    popup: Popup {
        id: choicePopup
        y: input.height + 4
        width: Math.min(input.maximumPopupWidth,
            Math.max(input.minimumPopupWidth, input.width, input.measuredPopupWidth),
            Overlay.overlay ? Math.max(0, Overlay.overlay.width - 12) : input.maximumPopupWidth)
        onAboutToShow: { input.measurePopup(); input.popupOpening(); }
        onClosed: input.popupClosed()
        padding: 4
        implicitHeight: Math.min(input.maximumPopupHeight,
            Overlay.overlay ? Math.max(0, Overlay.overlay.height - 16) : input.maximumPopupHeight,
            contentItem.implicitHeight + topPadding + bottomPadding)
        contentItem: ListView {
            id: choicePopupList
            clip: true
            implicitHeight: contentHeight
            model: input.popup.visible ? input.delegateModel : null
            currentIndex: input.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        background: Rectangle {
            radius: input.surfaceRadius
            color: VideoCutTheme.elevated
            border.color: VideoCutTheme.line
        }
    }
}
