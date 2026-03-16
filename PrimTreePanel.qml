import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

Rectangle {
    id: panel
    color: "#1a1a1a"

    required property var document
    property var selectedPrimPaths: []
    signal selectionChanged(var paths)
    signal statusMessage(string msg)

    property bool _internalChange: false
    property real _eyeColWidth: 36
    property real _typeColWidth: 70

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

        // Column header with resize handles
        Rectangle {
            id: colHeader
            Layout.fillWidth: true; height: 22; color: "#2d2d2d"

            Label {
                anchors.left: parent.left; anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: "名称"; color: "#888888"; font.pixelSize: 10
            }

            // Eye column header
            Item {
                id: eyeHeaderCol
                width: panel._eyeColWidth; height: parent.height
                anchors.right: typeHeaderCol.left
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    text: "可视"; color: "#888888"; font.pixelSize: 10
                }
                // Left resize handle
                MouseArea {
                    id: eyeResizeHandle
                    width: 6; height: parent.height
                    anchors.left: parent.left; anchors.leftMargin: -3
                    cursorShape: Qt.SplitHCursor
                    property real startX: 0
                    property real startEyeW: 0
                    property real startTypeW: 0
                    onPressed: function(mouse) {
                        startX = mapToItem(colHeader, mouse.x, 0).x
                        startEyeW = panel._eyeColWidth
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed) return
                        let currX = mapToItem(colHeader, mouse.x, 0).x
                        let delta = startX - currX
                        let newW = Math.max(24, startEyeW + delta)
                        panel._eyeColWidth = newW
                    }
                }
            }

            // Type column header
            Item {
                id: typeHeaderCol
                width: panel._typeColWidth; height: parent.height
                anchors.right: parent.right
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left; anchors.leftMargin: 4
                    text: "类型"; color: "#888888"; font.pixelSize: 10
                }
                // Left resize handle
                MouseArea {
                    id: typeResizeHandle
                    width: 6; height: parent.height
                    anchors.left: parent.left; anchors.leftMargin: -3
                    cursorShape: Qt.SplitHCursor
                    property real startX: 0
                    property real startTypeW: 0
                    onPressed: function(mouse) {
                        startX = mapToItem(colHeader, mouse.x, 0).x
                        startTypeW = panel._typeColWidth
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed) return
                        let currX = mapToItem(colHeader, mouse.x, 0).x
                        let delta = startX - currX
                        let newW = Math.max(30, startTypeW + delta)
                        panel._typeColWidth = newW
                    }
                }
            }
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
                readonly property real eyeColumnWidth: panel._eyeColWidth
                readonly property real typeColumnWidth: panel._typeColWidth
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

                // Column 1: Tree indicator + Name
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
                    width: parent.width - x - eyeColumnWidth - typeColumnWidth
                    clip: true
                    elide: Text.ElideRight
                    color: "#aaaaaa"; font.bold: true; font.pixelSize: 11
                    text: model.name
                }

                // Column 2: isActive eye icon
                Item {
                    id: eyeColumn
                    width: eyeColumnWidth
                    height: parent.height
                    anchors.right: typeColumn.left

                    Image {
                        id: eyeImg
                        source: model.isActive ? "icons/eye-solid.svg" : "icons/eye-slash-solid.svg"
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        width: 14; height: 14
                        sourceSize: Qt.size(14, 14)
                        fillMode: Image.PreserveAspectFit
                        visible: false
                    }
                    MultiEffect {
                        anchors.fill: eyeImg
                        source: eyeImg
                        colorization: 1.0
                        colorizationColor: model.isActive ? "#aaaaaa" : "#555555"
                    }

                    TapHandler {
                        onTapped: {
                            let path = model.path
                            let newVisible = !model.isActive
                            panel.document.setPrimVisibility(path, newVisible)
                        }
                    }
                }

                // Column 3: typeName
                Item {
                    id: typeColumn
                    width: typeColumnWidth
                    height: parent.height
                    anchors.right: parent.right

                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.rightMargin: 4
                        horizontalAlignment: Text.AlignLeft
                        clip: true
                        text: model.typeName
                        color: "#888888"
                        font.pixelSize: 10
                    }
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
                Label { text: "父: " + addPrimOverlay.parentPath; color: "#888888"; font.pixelSize: 11; elide: Text.ElideRight }

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
