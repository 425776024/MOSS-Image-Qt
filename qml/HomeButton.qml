import QtQuick

// Shared native control port; retain existing resource-management sizing.
AppButton {
    implicitHeight: 32
    implicitWidth: Math.max(text ? 64 : 32, contentItem.implicitWidth + leftPadding + rightPadding)
}
