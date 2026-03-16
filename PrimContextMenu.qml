import QtQuick
import QtQuick.Controls

Menu {
    id: root

    signal addAttributeRequested()
    signal editApiSchemaRequested()

    topPadding: 2; bottomPadding: 2

    delegate: MenuItem {
        id: menuDelegate
        implicitWidth: AppStyle.menuItemWidth
        implicitHeight: AppStyle.menuItemHeight
        arrow: Text {
            x: parent.width - width - 8
            anchors.verticalCenter: parent.verticalCenter
            text: "\u203A"
            color: menuDelegate.highlighted ? AppStyle.textWhite : AppStyle.textSecondary
            font.pixelSize: 16
            visible: menuDelegate.subMenu
        }
        contentItem: Text {
            text: menuDelegate.text
            font.pixelSize: AppStyle.fontSize
            color: menuDelegate.highlighted ? AppStyle.textWhite : AppStyle.textPrimary
            verticalAlignment: Text.AlignVCenter
            leftPadding: 8; rightPadding: 8
        }
        background: Rectangle {
            implicitWidth: AppStyle.menuItemWidth
            implicitHeight: AppStyle.menuItemHeight
            color: menuDelegate.highlighted ? AppStyle.accent : "transparent"
        }
    }

    background: Rectangle {
        implicitWidth: AppStyle.menuItemWidth
        implicitHeight: 10
        color: AppStyle.bgWidget; border.color: AppStyle.border; radius: 4
    }

    Menu {
        id: addSubMenu
        title: "Add"

        delegate: MenuItem {
            id: addSubDelegate
            implicitWidth: AppStyle.menuItemWidth
            implicitHeight: AppStyle.menuItemHeight
            contentItem: Text {
                text: addSubDelegate.text
                font.pixelSize: AppStyle.fontSize
                color: addSubDelegate.highlighted ? AppStyle.textWhite : AppStyle.textPrimary
                verticalAlignment: Text.AlignVCenter
                leftPadding: 8; rightPadding: 8
            }
            background: Rectangle {
                implicitWidth: AppStyle.menuItemWidth
                implicitHeight: AppStyle.menuItemHeight
                color: addSubDelegate.highlighted ? AppStyle.accent : "transparent"
            }
        }

        background: Rectangle {
            implicitWidth: AppStyle.menuItemWidth
            implicitHeight: 10
            color: AppStyle.bgWidget; border.color: AppStyle.border; radius: 4
        }

        Action {
            text: "Attribute"
            onTriggered: root.addAttributeRequested()
        }
        Action {
            text: "Edit API Schema"
            onTriggered: root.editApiSchemaRequested()
        }
    }

    Menu {
        id: createSubMenu
        title: "Create"

        delegate: MenuItem {
            id: createSubDelegate
            implicitWidth: AppStyle.menuItemWidth
            implicitHeight: AppStyle.menuItemHeight
            contentItem: Text {
                text: createSubDelegate.text
                font.pixelSize: AppStyle.fontSize
                color: createSubDelegate.highlighted ? AppStyle.textWhite : AppStyle.textPrimary
                verticalAlignment: Text.AlignVCenter
                leftPadding: 8; rightPadding: 8
            }
            background: Rectangle {
                implicitWidth: AppStyle.menuItemWidth
                implicitHeight: AppStyle.menuItemHeight
                color: createSubDelegate.highlighted ? AppStyle.accent : "transparent"
            }
        }

        background: Rectangle {
            implicitWidth: AppStyle.menuItemWidth
            implicitHeight: 10
            color: AppStyle.bgWidget; border.color: AppStyle.border; radius: 4
        }

        Action {
            text: "(placeholder)"
            enabled: false
        }
    }
}
