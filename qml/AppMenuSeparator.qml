import QtQuick
import QtQuick.Controls

MenuSeparator {
    id: separator
    leftPadding: 8; rightPadding: 8
    topPadding: 6; bottomPadding: 6
    contentItem: Rectangle { implicitWidth: 200; implicitHeight: 1; color: separator.palette.mid }
}
