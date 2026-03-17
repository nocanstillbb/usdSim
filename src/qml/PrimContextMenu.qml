import QtQuick
import QtQuick.Controls

Menu {
    id: root

    signal addAttributeRequested()
    signal editApiSchemaRequested()
    signal createPrimRequested(string name, string typeName)
    signal deletePrimRequested()

    topPadding: 2; bottomPadding: 2

    // ── Reusable styled submenu ──
    component StyledMenu: Menu {
        topPadding: 2; bottomPadding: 2
        delegate: MenuItem {
            id: _smi
            implicitWidth: AppStyle.menuItemWidth
            implicitHeight: AppStyle.menuItemHeight
            arrow: Text {
                x: parent.width - width - 8
                anchors.verticalCenter: parent.verticalCenter
                text: "\u203A"
                color: _smi.highlighted ? AppStyle.textWhite : AppStyle.textSecondary
                font.pixelSize: 16
                visible: _smi.subMenu
            }
            contentItem: Text {
                text: _smi.text
                font.pixelSize: AppStyle.fontSize
                color: _smi.highlighted ? AppStyle.textWhite : AppStyle.textPrimary
                verticalAlignment: Text.AlignVCenter
                leftPadding: 8; rightPadding: 8
            }
            background: Rectangle {
                implicitWidth: AppStyle.menuItemWidth
                implicitHeight: AppStyle.menuItemHeight
                color: _smi.highlighted ? AppStyle.accent : "transparent"
            }
        }
        background: Rectangle {
            implicitWidth: AppStyle.menuItemWidth
            implicitHeight: 10
            color: AppStyle.bgWidget; border.color: AppStyle.border; radius: 4
        }
    }

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

    // ── Add submenu ──
    StyledMenu {
        title: "Add"
        Action { text: "Attribute"; onTriggered: root.addAttributeRequested() }
        Action { text: "Edit API Schema"; onTriggered: root.editApiSchemaRequested() }
    }

    // ── Create submenu ──
    StyledMenu {
        title: "Create"

        StyledMenu {
            title: "Mesh"
            Action { text: "Cone"; onTriggered: root.createPrimRequested("Cone", "Cone") }
            Action { text: "Cube"; onTriggered: root.createPrimRequested("Cube", "Cube") }
            Action { text: "Cylinder"; onTriggered: root.createPrimRequested("Cylinder", "Cylinder") }
            Action { text: "Disk"; onTriggered: root.createPrimRequested("Disk", "Mesh") }
            Action { text: "Plane"; onTriggered: root.createPrimRequested("Plane", "Plane") }
            Action { text: "Sphere"; onTriggered: root.createPrimRequested("Sphere", "Sphere") }
            Action { text: "Torus"; onTriggered: root.createPrimRequested("Torus", "Mesh") }
        }

        StyledMenu {
            title: "Shape"
            Action { text: "Capsule"; onTriggered: root.createPrimRequested("Capsule", "Capsule") }
            Action { text: "Cone"; onTriggered: root.createPrimRequested("Cone", "Cone") }
            Action { text: "Cube"; onTriggered: root.createPrimRequested("Cube", "Cube") }
            Action { text: "Cylinder"; onTriggered: root.createPrimRequested("Cylinder", "Cylinder") }
            Action { text: "Plane"; onTriggered: root.createPrimRequested("Plane", "Plane") }
            Action { text: "Sphere"; onTriggered: root.createPrimRequested("Sphere", "Sphere") }
        }

        StyledMenu {
            title: "Light"
            Action { text: "Distant Light"; onTriggered: root.createPrimRequested("DistantLight", "DistantLight") }
            Action { text: "Sphere Light"; onTriggered: root.createPrimRequested("SphereLight", "SphereLight") }
            Action { text: "Rect Light"; onTriggered: root.createPrimRequested("RectLight", "RectLight") }
            Action { text: "Disk Light"; onTriggered: root.createPrimRequested("DiskLight", "DiskLight") }
            Action { text: "Dome Light"; onTriggered: root.createPrimRequested("DomeLight", "DomeLight") }
            Action { text: "Cylinder Light"; onTriggered: root.createPrimRequested("CylinderLight", "CylinderLight") }
        }

        Action { text: "Scope"; onTriggered: root.createPrimRequested("Scope", "Scope") }
        Action { text: "Xform"; onTriggered: root.createPrimRequested("Xform", "Xform") }

        MenuSeparator {
            contentItem: Rectangle {
                implicitWidth: AppStyle.menuItemWidth
                implicitHeight: 1
                color: AppStyle.border
            }
        }

        StyledMenu {
            title: "Physics"
            Action { text: "Ground Plane"; onTriggered: root.createPrimRequested("GroundPlane", "Plane") }
        }
    }

    MenuSeparator {
        contentItem: Rectangle {
            implicitWidth: AppStyle.menuItemWidth
            implicitHeight: 1
            color: AppStyle.border
        }
    }

    Action { text: "Delete"; onTriggered: root.deletePrimRequested() }
}
