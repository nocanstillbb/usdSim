import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: panel
    color: "#1a1a1a"

    required property var document
    property var selectedPrimPaths: []
    signal selectionChanged(var paths)
    signal statusMessage(string msg)

    property bool _internalChange: false

    onSelectedPrimPathsChanged: {
        if (_internalChange) return
        _syncTreeSelection(selectedPrimPaths)
    }

    function _syncTreeSelection(paths) {
        sel.clearSelection()
        sel.clearCurrentIndex()
        for (let i = 0; i < paths.length; i++) {
            let idx = document.findPrimModelIndex(paths[i])
            if (idx.valid) {
                primTree.expandToIndex(idx)
                if (i === 0)
                    sel.setCurrentIndex(idx, ItemSelectionModel.Select | ItemSelectionModel.Rows)
                else
                    sel.select(idx, ItemSelectionModel.Select | ItemSelectionModel.Rows)
            }
        }
        if (paths.length > 0) {
            Qt.callLater(function() {
                let firstIdx = document.findPrimModelIndex(paths[0])
                if (firstIdx.valid) {
                    let row = primTree.rowAtIndex(firstIdx)
                    if (row >= 0)
                        primTree.positionViewAtRow(row, Qt.AlignVCenter)
                }
            })
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true; height: 28; color: "#252525"
            Label { anchors.centerIn: parent; text: "Prim 列表"; color: "#aaaaaa"; font.bold: true; font.pixelSize: 11 }
        }

        TreeView {
            id: primTree
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            selectionModel: ItemSelectionModel { id: sel }
            model: panel.document.primTreeModel
            property int hoveredRow: -1

            ScrollBar.vertical: ScrollBar {}
            columnWidthProvider: function(col) { return width }
            rowHeightProvider: function(row) { return 30 }

            maximumFlickVelocity: 0
            flickDeceleration: 100000
            boundsBehavior: Flickable.StopAtBounds

            delegate: Item {
                id: primDelegate
                readonly property real indentation: 15
                readonly property real padding: 15
                required property TreeView treeView
                required property bool isTreeNode
                required property bool expanded
                required property bool hasChildren
                required property int depth
                required property int row
                required property int column
                required property bool current
                required property bool selected

                HoverHandler {
                    id: hoverHandler
                    onHoveredChanged: {
                        if (hovered)
                            primTree.hoveredRow = row
                        else if (primTree.hoveredRow === row)
                            primTree.hoveredRow = -1
                    }
                }
                TapHandler {
                    acceptedModifiers: Qt.ControlModifier
                    onTapped: function(eventPoint, button) {
                        let paths = panel.selectedPrimPaths.slice ? panel.selectedPrimPaths.slice() : []
                        let pi = paths.indexOf(model.path)
                        if (pi >= 0)
                            paths.splice(pi, 1)
                        else
                            paths.push(model.path)
                        sel.clearSelection()
                        sel.clearCurrentIndex()
                        for (let i = 0; i < paths.length; i++) {
                            let idx = panel.document.findPrimModelIndex(paths[i])
                            if (idx.valid) {
                                if (i === 0)
                                    sel.setCurrentIndex(idx, ItemSelectionModel.Select | ItemSelectionModel.Rows)
                                else
                                    sel.select(idx, ItemSelectionModel.Select | ItemSelectionModel.Rows)
                            }
                        }
                        panel._internalChange = true
                        panel.selectedPrimPaths = paths
                        panel.selectionChanged(paths)
                        panel._internalChange = false
                    }
                }
                TapHandler {
                    acceptedModifiers: Qt.NoModifier
                    onTapped: function(eventPoint, button) {
                        let index = treeView.index(row, column)
                        treeView.selectionModel.setCurrentIndex(index, ItemSelectionModel.ClearAndSelect | ItemSelectionModel.Rows)
                        panel._internalChange = true
                        panel.selectedPrimPaths = [model.path]
                        panel.selectionChanged([model.path])
                        panel._internalChange = false
                    }
                    onDoubleTapped: {
                        let index = treeView.index(row, column)
                        treeView.selectionModel.setCurrentIndex(index, ItemSelectionModel.ClearAndSelect | ItemSelectionModel.Rows)
                        panel._internalChange = true
                        panel.selectedPrimPaths = [model.path]
                        panel.selectionChanged([model.path])
                        panel._internalChange = false
                    }
                }

                property Animation indicatorAnimation: NumberAnimation {
                    target: indicator
                    property: "rotation"
                    from: expanded ? 0 : 90
                    to: expanded ? 90 : 0
                    duration: 100
                    easing.type: Easing.OutQuart
                }
                TableView.onPooled: { indicatorAnimation.complete(); if (primTree.hoveredRow === row) primTree.hoveredRow = -1 }
                TableView.onReused: if (current) indicatorAnimation.start()
                onExpandedChanged: indicator.rotation = expanded ? 90 : 0

                Rectangle {
                    id: background
                    anchors.fill: parent
                    color: {
                        if (primDelegate.selected || primDelegate.current)
                            return "#007acc"
                        else if (primTree.hoveredRow === row)
                            return "#70007acc"
                        return row % 2 === 0 ? "#232323" : "#1e1e1e"
                    }
                }

                Label {
                    id: indicator
                    x: padding + (depth * indentation)
                    anchors.verticalCenter: parent.verticalCenter
                    visible: isTreeNode && hasChildren
                    text: "▶"
                    color: "#aaaaaa"; font.bold: true; font.pixelSize: 11

                    TapHandler {
                        onSingleTapped: function(eventPoint, button) {
                            let index = treeView.index(row, column)
                            treeView.selectionModel.setCurrentIndex(index, ItemSelectionModel.ClearAndSelect | ItemSelectionModel.Rows)
                            treeView.toggleExpanded(row)
                            panel._internalChange = true
                            panel.selectedPrimPaths = [model.path]
                            panel.selectionChanged([model.path])
                            panel._internalChange = false
                        }
                    }
                }

                Label {
                    id: label
                    x: padding + (isTreeNode ? (depth + 1) * indentation : 0)
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - padding - x
                    clip: true
                    color: "#aaaaaa"; font.bold: true; font.pixelSize: 11
                    text: `${model.name} [${model.typeName}]`
                }
            }
        }
    }

    Menu {
        id: primCtxMenu
        property string primPath: ""
        MenuItem {
            text: "添加子 Prim"
            onTriggered: { addPrimOverlay.parentPath = primCtxMenu.primPath; addPrimOverlay.visible = true }
        }
        MenuItem {
            text: "删除此 Prim"
            onTriggered: {
                let ok = panel.document.removePrim(primCtxMenu.primPath)
                if (ok) {
                    panel._internalChange = true
                    panel.selectedPrimPaths = []
                    panel.selectionChanged([])
                    panel._internalChange = false
                }
                panel.statusMessage(ok ? "已删除" : "删除失败")
            }
        }
    }

    Rectangle {
        id: addPrimOverlay
        anchors.fill: parent; color: "#99000000"; visible: false; z: 200

        property string parentPath: ""

        MouseArea { anchors.fill: parent; onClicked: addPrimOverlay.visible = false }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(360, parent.width - 20)
            height: 200
            color: "#2d2d2d"; radius: 8

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 10

                Label { text: "添加 Prim"; color: "#ffffff"; font.pixelSize: 14; font.bold: true }
                Label { text: "父: " + addPrimOverlay.parentPath; color: "#888888"; font.pixelSize: 11; elide: Text.ElideLeft }

                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "名称:"; color: "#cccccc"; font.pixelSize: 12; Layout.preferredWidth: 50 }
                    TextField {
                        id: newPrimName; Layout.fillWidth: true; placeholderText: "MyPrim"
                        color: "#cccccc"
                        background: Rectangle { color: "#1e1e1e"; radius: 4; border.color: "#555555" }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "类型:"; color: "#cccccc"; font.pixelSize: 12; Layout.preferredWidth: 50 }
                    ComboBox {
                        id: newPrimType; Layout.fillWidth: true
                        model: ["Xform","Sphere","Cube","Cylinder","Cone","Mesh","Camera",""]
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    Button {
                        text: "取消"; flat: true
                        contentItem: Text { text: parent.text; color: "#aaaaaa"; font.pixelSize: 12 }
                        background: Rectangle { color: parent.hovered ? "#333333" : "transparent"; radius: 4 }
                        onClicked: addPrimOverlay.visible = false
                    }
                    Button {
                        text: "添加"
                        contentItem: Text { text: parent.text; color: "#ffffff"; font.pixelSize: 12 }
                        background: Rectangle { color: parent.hovered ? "#005fa3" : "#0078d4"; radius: 4 }
                        onClicked: {
                            let name = newPrimName.text.trim()
                            if (name === "") return
                            let ok = panel.document.addPrim(addPrimOverlay.parentPath, name, newPrimType.currentText)
                            panel.statusMessage(ok ? "已添加: " + addPrimOverlay.parentPath + "/" + name : "添加失败")
                            addPrimOverlay.visible = false
                        }
                    }
                }
            }
        }
    }
}
