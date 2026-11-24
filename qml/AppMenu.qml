import QtQuick
import QtQuick.Controls

Menu {
    id: menu
    popupType: Popup.Item
    padding: 6
    margins: 8
    overlap: 4
    transformOrigin: Item.TopLeft
    delegate: AppMenuItem {}
    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: messenger.reduceMotion ? 0 : 180; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: messenger.reduceMotion ? 0 : 220; easing.type: Easing.OutCubic }
        }
    }
    exit: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; to: 0; duration: messenger.reduceMotion ? 0 : 140; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; to: 0.98; duration: messenger.reduceMotion ? 0 : 140; easing.type: Easing.OutCubic }
        }
    }
    background: MenuSurface {
        color: Qt.rgba(menu.palette.window.r, menu.palette.window.g, menu.palette.window.b, 0.96)
        borderColor: messenger.darkTheme ? "#587f9ebb" : "#c9ffffff"
    }
}
