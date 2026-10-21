import QtQuick

// Bridge the imported VideoCut controls to Moss's existing vector icon set.
StudioIcon {
    property string kind: "image"
    property color color: VideoCutTheme.text
    name: kind === "choice-chevron" ? "chevron" : kind === "keyframe-next" ? "arrow" : kind === "media" ? "image" : kind
    ink: color
}
