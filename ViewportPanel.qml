import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import UsdBrowser 1.0

Rectangle {
    id: panel
    color: "#111111"

    required property var document
    property var selectedPrimPaths: []
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
        color: "#555555"; font.pixelSize: 14
        horizontalAlignment: Text.AlignHCenter
    }

    Label {
        visible: panel.document.isOpen
        anchors { top: parent.top; right: parent.right; margins: 8 }
        text: "拖拽旋转 · alt+拖拽平移 · 滚轮缩放"
        color: "#666666"; font.pixelSize: 11
    }

    Row {
        visible: panel.document.isOpen
        anchors { top: parent.top; left: parent.left; margins: 8 }
        spacing: 1

        Rectangle {
            width: childRow.width + 4
            height: childRow.height + 4
            radius: 4
            color: "#cc1e1e1e"

            Row {
                id: childRow
                anchors.centerIn: parent
                spacing: 1

                Repeater {
                    model: [
                        { label: "移动", mode: 1 },
                        { label: "旋转", mode: 2 },
                        { label: "缩放", mode: 3 }
                    ]
                    delegate: Rectangle {
                        width: 56; height: 24
                        radius: 3
                        color: viewport.gizmoMode === modelData.mode
                            ? "#0078d4"
                            : toolArea.containsMouse ? "#40ffffff" : "transparent"

                        Text {
                            anchors.centerIn: parent
                            text: modelData.label
                            font.pixelSize: 11
                            color: viewport.gizmoMode === modelData.mode ? "#ffffff" : "#aaaaaa"
                        }

                        MouseArea {
                            id: toolArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: viewport.gizmoMode = (viewport.gizmoMode === modelData.mode) ? 0 : modelData.mode
                        }
                    }
                }
            }
        }
    }

    Row {
        visible: panel.document.isOpen
        anchors { top: parent.top; left: parent.left; topMargin: 38; leftMargin: 8 }
        spacing: 1

        Rectangle {
            width: gridSnapRow.width + 4
            height: gridSnapRow.height + 4
            radius: 4
            color: "#cc1e1e1e"

            Row {
                id: gridSnapRow
                anchors.centerIn: parent
                spacing: 1

                Rectangle {
                    width: 56; height: 24
                    radius: 3
                    color: viewport.showGrid
                        ? "#0078d4"
                        : gridArea.containsMouse ? "#40ffffff" : "transparent"

                    Text {
                        anchors.centerIn: parent
                        text: "网格"
                        font.pixelSize: 11
                        color: viewport.showGrid ? "#ffffff" : "#aaaaaa"
                    }

                    MouseArea {
                        id: gridArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: viewport.showGrid = !viewport.showGrid
                    }
                }

                Rectangle {
                    width: 56; height: 24
                    radius: 3
                    color: viewport.snapEnabled
                        ? "#0078d4"
                        : snapArea.containsMouse ? "#40ffffff" : "transparent"

                    Text {
                        anchors.centerIn: parent
                        text: "吸附"
                        font.pixelSize: 11
                        color: viewport.snapEnabled ? "#ffffff" : "#aaaaaa"
                    }

                    MouseArea {
                        id: snapArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: viewport.snapEnabled = !viewport.snapEnabled
                    }
                }
            }
        }
    }

    Text {
        x: viewport.orientLabelX.x - width / 2
        y: viewport.orientLabelX.y - height / 2
        text: "X"; color: "#ff3333"
        font.pixelSize: 11; font.bold: true
        visible: panel.document.isOpen
    }
    Text {
        x: viewport.orientLabelY.x - width / 2
        y: viewport.orientLabelY.y - height / 2
        text: "Y"; color: "#33ff33"
        font.pixelSize: 11; font.bold: true
        visible: panel.document.isOpen
    }
    Text {
        x: viewport.orientLabelZ.x - width / 2
        y: viewport.orientLabelZ.y - height / 2
        text: "Z"; color: "#5599ff"
        font.pixelSize: 11; font.bold: true
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
            text: "cm"
            color: "#888888"
            font.pixelSize: 16
        }
    }
}
