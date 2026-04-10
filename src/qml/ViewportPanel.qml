import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import UsdBrowser 1.0

Rectangle {
    id: panel
    color: "#111111"

    required property var document
    property var selectedPrimPaths: []
    property alias viewportItem: viewport
    signal selectionChanged(var paths)
    signal attributeRefreshNeeded()

    property bool _internalChange: false

    function selectPrimPaths(paths) {
        _internalChange = true
        selectedPrimPaths = paths
        viewport.selectPrimPaths(paths)
        _internalChange = false
    }

    onSelectedPrimPathsChanged: {
        if (_internalChange) return
        _internalChange = true
        viewport.selectPrimPaths(selectedPrimPaths)
        _internalChange = false
    }

    UsdViewport {
        id: viewport
        anchors.fill: parent
        document: panel.document

        onGizmoDragUpdated: {
            panel.attributeRefreshNeeded()
        }

        onGizmoDragFinished: function(primPath) {
            panel.attributeRefreshNeeded()
        }

        onPrimClicked: function(primPath, ctrlHeld) {
            panel.forceActiveFocus()
            let paths
            if (ctrlHeld) {
                paths = panel.selectedPrimPaths.slice ? panel.selectedPrimPaths.slice() : []
                if (primPath !== "") {
                    let pi = paths.indexOf(primPath)
                    if (pi >= 0)
                        paths.splice(pi, 1)
                    else
                        paths.push(primPath)
                }
            } else {
                paths = primPath !== "" ? [primPath] : []
            }
            panel._internalChange = true
            panel.selectedPrimPaths = paths
            viewport.selectPrimPaths(paths)
            panel.selectionChanged(paths)
            panel._internalChange = false
        }

        onSelectedPrimPathsChanged: {
            if (panel._internalChange) return
            panel.selectionChanged(viewport.selectedPrimPaths)
        }
    }

    Label {
        visible: !panel.document.isOpen
        anchors.centerIn: parent
        text: "打开 USD 文件后在此显示 3D 场景\n拖拽旋转 · alt+拖拽平移 · 滚轮缩放"
        color: "#555555"; font.family: AppStyle.fontFamily; font.pixelSize: 14
        horizontalAlignment: Text.AlignHCenter
    }

    Label {
        visible: panel.document.isOpen
        anchors { top: parent.top; right: parent.right; margins: 8 }
        text: "拖拽旋转 · alt+拖拽平移 · 滚轮缩放"
        color: "#666666"; font.family: AppStyle.fontFamily; font.pixelSize: 11
    }

    // ── Viewport toolbar: Gizmo | Display | Collision ──
    Rectangle {
        visible: panel.document.isOpen
        anchors { top: parent.top; left: parent.left; margins: 8 }
        width: toolbarRow.width + 8
        height: toolbarRow.height + 4
        radius: 4
        color: "#cc1e1e1e"

        Row {
            id: toolbarRow
            anchors.centerIn: parent
            spacing: 0

            // ── Section 1: Gizmo mode ──
            Row {
                spacing: 1
                Repeater {
                    model: [
                        { label: "移动", mode: 1 },
                        { label: "旋转", mode: 2 },
                        { label: "缩放", mode: 3 }
                    ]
                    delegate: Rectangle {
                        width: 48; height: 24; radius: 3
                        color: viewport.gizmoMode === modelData.mode
                            ? AppStyle.accent
                            : toolArea.containsMouse ? "#40ffffff" : "transparent"
                        Text {
                            anchors.centerIn: parent; text: modelData.label
                            font.family: AppStyle.fontFamily; font.pixelSize: AppStyle.fontSizeSmall
                            color: viewport.gizmoMode === modelData.mode ? AppStyle.textWhite : AppStyle.textSecondary
                        }
                        MouseArea {
                            id: toolArea; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: viewport.gizmoMode = (viewport.gizmoMode === modelData.mode) ? 0 : modelData.mode
                        }
                    }
                }
            }

            // Divider
            Rectangle { width: 1; height: 16; color: AppStyle.border; anchors.verticalCenter: parent.verticalCenter
                        Layout.leftMargin: 4; Layout.rightMargin: 4 }
            Item { width: 6; height: 1 }

            // ── Section 2: Grid, Shadow & Snap ──
            Row {
                spacing: 1
                Repeater {
                    model: [
                        { label: "网格", prop: "showGrid" },
                        { label: "阴影", prop: "showShadow" },
                        { label: "吸附", prop: "snapEnabled" }
                    ]
                    delegate: Rectangle {
                        width: 48; height: 24; radius: 3
                        property bool active: modelData.prop === "showGrid" ? viewport.showGrid
                                            : modelData.prop === "showShadow" ? viewport.showShadow
                                            : viewport.snapEnabled
                        color: active ? AppStyle.accent
                             : dispArea.containsMouse ? "#40ffffff" : "transparent"
                        Text {
                            anchors.centerIn: parent; text: modelData.label
                            font.family: AppStyle.fontFamily; font.pixelSize: AppStyle.fontSizeSmall
                            color: parent.active ? AppStyle.textWhite : AppStyle.textSecondary
                        }
                        MouseArea {
                            id: dispArea; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (modelData.prop === "showGrid") viewport.showGrid = !viewport.showGrid
                                else if (modelData.prop === "showShadow") viewport.showShadow = !viewport.showShadow
                                else viewport.snapEnabled = !viewport.snapEnabled
                            }
                        }
                    }
                }
            }

            // Divider
            Item { width: 6; height: 1 }
            Rectangle { width: 1; height: 16; color: AppStyle.border; anchors.verticalCenter: parent.verticalCenter }
            Item { width: 6; height: 1 }

            // ── Section 3: Collision display ──
            Row {
                spacing: 4
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "碰撞"; color: AppStyle.textMuted
                    font.family: AppStyle.fontFamily; font.pixelSize: AppStyle.fontSizeSmall
                }
                ComboBox {
                    id: colCombo
                    width: 72; implicitHeight: 22
                    font.family: AppStyle.fontFamily; font.pixelSize: AppStyle.fontSizeSmall
                    model: ["隐藏", "选中", "全部"]
                    currentIndex: viewport.collisionDisplayMode
                    onCurrentIndexChanged: viewport.collisionDisplayMode = currentIndex
                    contentItem: Text {
                        leftPadding: 6; text: colCombo.displayText
                        font.family: AppStyle.fontFamily; font.pixelSize: AppStyle.fontSizeSmall
                        color: AppStyle.textBright; verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: AppStyle.bgInput; radius: 3
                        border.color: colCombo.activeFocus ? AppStyle.borderFocus : AppStyle.border
                    }
                    indicator: Text {
                        anchors.right: parent.right; anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        text: "\u25BE"; color: AppStyle.textMuted
                        font.family: AppStyle.fontFamily; font.pixelSize: AppStyle.fontSizeTiny
                    }
                    popup: Popup {
                        y: colCombo.height
                        width: colCombo.width; implicitHeight: contentItem.implicitHeight + 2; padding: 1
                        background: Rectangle { color: AppStyle.bgWidget; border.color: AppStyle.border; radius: 3 }
                        contentItem: ListView {
                            clip: true; implicitHeight: contentHeight
                            model: colCombo.delegateModel
                        }
                    }
                    delegate: ItemDelegate {
                        width: colCombo.width; height: 24
                        contentItem: Text {
                            text: modelData; verticalAlignment: Text.AlignVCenter
                            font.family: AppStyle.fontFamily; font.pixelSize: AppStyle.fontSizeSmall
                            color: highlighted ? AppStyle.textWhite : AppStyle.textPrimary
                        }
                        highlighted: colCombo.highlightedIndex === index
                        background: Rectangle { color: highlighted ? AppStyle.accent : "transparent" }
                    }
                }
            }
        }
    }

    Text {
        x: viewport.orientLabelX.x - width / 2
        y: viewport.orientLabelX.y - height / 2
        text: "X"; color: "#ff3333"
        font.family: AppStyle.fontFamily; font.pixelSize: 11; font.bold: true
        visible: panel.document.isOpen
    }
    Text {
        x: viewport.orientLabelY.x - width / 2
        y: viewport.orientLabelY.y - height / 2
        text: "Y"; color: "#33ff33"
        font.family: AppStyle.fontFamily; font.pixelSize: 11; font.bold: true
        visible: panel.document.isOpen
    }
    Text {
        x: viewport.orientLabelZ.x - width / 2
        y: viewport.orientLabelZ.y - height / 2
        text: "Z"; color: "#5599ff"
        font.family: AppStyle.fontFamily; font.pixelSize: 11; font.bold: true
        visible: panel.document.isOpen
    }

    Rectangle {
        x: 50 - width / 2
        y: parent.height - 26
        color: "#cc1e1e1e"
        radius: 3
        width: cmLabel.width + 8
        height: cmLabel.height + 4
        visible: panel.document.isOpen
        Text {
            id: cmLabel
            anchors.centerIn: parent
            text: viewport.stageUnitLabel
            color: "#888888"
            font.family: AppStyle.fontFamily; font.pixelSize: 16
        }
    }

    // ── Right-click context menu ──
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onClicked: function(mouse) {
            if (panel.document.isOpen)
                viewportCtxMenu.popup()
        }
    }

    PrimContextMenu {
        id: viewportCtxMenu
        onAddAttributeRequested: addAttrDialog.openDialog()
        onEditApiSchemaRequested: editSchemaDialog.openDialog()
        onCreatePrimRequested: function(name, typeName) {
            let parentPath = panel.selectedPrimPaths.length > 0 ? panel.selectedPrimPaths[0] : "/"
            panel.document.addPrim(parentPath, name, typeName)
        }
        onDeletePrimRequested: {
            let paths = panel.selectedPrimPaths.slice ? panel.selectedPrimPaths.slice() : []
            for (let i = paths.length - 1; i >= 0; --i)
                panel.document.removePrim(paths[i])
            panel.selectedPrimPaths = []
            panel.selectionChanged([])
        }
    }

    AddAttributeDialog {
        id: addAttrDialog
        document: panel.document
        selectedPrimPaths: panel.selectedPrimPaths
        onAttributeAdded: panel.attributeRefreshNeeded()
    }

    EditSchemaDialog {
        id: editSchemaDialog
        document: panel.document
        selectedPrimPaths: panel.selectedPrimPaths
        onSchemaChanged: panel.attributeRefreshNeeded()
    }
}
