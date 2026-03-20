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
    property bool _isFiltering: false

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

    function _applyFilter(text) {
        let q = text.trim()
        _isFiltering = q.length > 0
        document.setPrimTreeFilter(q)
        if (_isFiltering) {
            // Expand all visible nodes so filtered results are shown
            Qt.callLater(function() { primTree.expandRecursively() })
        }
    }

    function _clearFilter() {
        searchField.text = ""
        _isFiltering = false
        document.setPrimTreeFilter("")
        // Wait for TreeView to finish rebuilding after filter removal,
        // then expand+select+scroll to the previously selected prim
        if (selectedPrimPaths.length > 0) {
            scrollTimer.restart()
        }
    }

    Timer {
        id: scrollTimer
        interval: 50; repeat: false
        onTriggered: panel._syncTreeSelection(panel.selectedPrimPaths)
    }

    // ── Right-click context menu ──
    PrimContextMenu {
        id: primCtxMenu
        onAddAttributeRequested: primAddAttrDialog.openDialog()
        onEditApiSchemaRequested: primEditSchemaDialog.openDialog()
        onCreatePrimRequested: function(name, typeName) {
            let parentPath = panel.selectedPrimPaths.length > 0 ? panel.selectedPrimPaths[0] : "/"
            let ok = panel.document.addPrim(parentPath, name, typeName)
            panel.statusMessage(ok ? "已创建: " + parentPath + "/" + name : "创建失败")
        }
        onDeletePrimRequested: {
            let paths = panel.selectedPrimPaths.slice ? panel.selectedPrimPaths.slice() : []
            for (let i = paths.length - 1; i >= 0; --i)
                panel.document.removePrim(paths[i])
            panel._internalChange = true
            panel.selectedPrimPaths = []
            panel.selectionChanged([])
            panel._internalChange = false
        }
    }

    AddAttributeDialog {
        id: primAddAttrDialog
        document: panel.document
        selectedPrimPaths: panel.selectedPrimPaths
        onAttributeAdded: {
            // Notify parent to refresh attribute panel
        }
    }

    EditSchemaDialog {
        id: primEditSchemaDialog
        document: panel.document
        selectedPrimPaths: panel.selectedPrimPaths
        onSchemaChanged: {
            // Notify parent to refresh attribute panel
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true; height: 28; color: "#252525"
            Label { anchors.centerIn: parent; text: "Prim 列表"; color: "#aaaaaa"; font.bold: true; font.family: AppStyle.fontFamily; font.pixelSize: 11 }
        }

        // Column header with resize handles
        Rectangle {
            id: colHeader
            Layout.fillWidth: true; height: 22; color: "#2d2d2d"

            Label {
                anchors.left: parent.left; anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: "名称"; color: "#888888"; font.family: AppStyle.fontFamily; font.pixelSize: 10
            }

            // Eye column header
            Item {
                id: eyeHeaderCol
                width: panel._eyeColWidth; height: parent.height
                anchors.right: typeHeaderCol.left
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    text: "可视"; color: "#888888"; font.family: AppStyle.fontFamily; font.pixelSize: 10
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
                    text: "类型"; color: "#888888"; font.family: AppStyle.fontFamily; font.pixelSize: 10
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

        // ── Search bar ──
        Item {
            Layout.fillWidth: true; height: 24
            Layout.leftMargin: 2; Layout.rightMargin: 2

            TextField {
                id: searchField
                anchors.fill: parent
                placeholderText: "搜索 prim..."
                placeholderTextColor: "#555555"
                color: "#cccccc"; font.family: AppStyle.fontFamily; font.pixelSize: 11
                leftPadding: 4; rightPadding: 20; topPadding: 0; bottomPadding: 0
                background: Rectangle { color: "#1e1e1e"; radius: 3; border.color: searchField.activeFocus ? "#007acc" : "#3a3a3a" }
                onTextChanged: panel._applyFilter(text)
                Keys.onEscapePressed: panel._clearFilter()
            }

            Label {
                visible: searchField.text.length > 0
                anchors.right: parent.right; anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                text: "\u2715"; color: clearArea.containsMouse ? "#cccccc" : "#888888"; font.family: AppStyle.fontFamily; font.pixelSize: 11

                MouseArea {
                    id: clearArea
                    anchors.fill: parent; anchors.margins: -4
                    cursorShape: Qt.PointingHandCursor; hoverEnabled: true
                    onClicked: panel._clearFilter()
                }
            }
        }

        TreeView {
            id: primTree
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            selectionModel: ItemSelectionModel { id: sel }
            model: panel.document.filteredPrimTreeModel
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

                // Right-click context menu
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    onClicked: function(mouse) {
                        let paths = panel.selectedPrimPaths.slice ? panel.selectedPrimPaths.slice() : []
                        if (paths.indexOf(model.path) < 0) {
                            // Not in selection — select this prim only
                            let index = treeView.index(row, column)
                            treeView.selectionModel.setCurrentIndex(index, ItemSelectionModel.ClearAndSelect | ItemSelectionModel.Rows)
                            panel._internalChange = true
                            panel.selectedPrimPaths = [model.path]
                            panel.selectionChanged([model.path])
                            panel._internalChange = false
                        }
                        primCtxMenu.popup()
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
                    color: "#aaaaaa"; font.bold: true; font.family: AppStyle.fontFamily; font.pixelSize: 11

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
                    color: "#aaaaaa"; font.bold: true; font.family: AppStyle.fontFamily; font.pixelSize: 11
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
                        font.family: AppStyle.fontFamily; font.pixelSize: 10
                    }
                }
            }
        }
    }


    Popup {
        id: addPrimOverlay
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        property string parentPath: ""

        width: Math.min(360, (parent ? parent.width : 400) - 40)
        padding: 20

        Overlay.modal: Rectangle { color: "#99000000" }
        background: Rectangle { color: AppStyle.bgWidget; radius: 8 }

        contentItem: ColumnLayout {
            spacing: 10

            Label { text: "添加 Prim"; color: AppStyle.textWhite; font.family: AppStyle.fontFamily; font.pixelSize: 14; font.bold: true }
            Label { text: "父: " + addPrimOverlay.parentPath; color: AppStyle.textMuted; font.family: AppStyle.fontFamily; font.pixelSize: AppStyle.fontSizeSmall; elide: Text.ElideRight }

            RowLayout {
                Layout.fillWidth: true
                Label { text: "名称:"; color: AppStyle.textPrimary; font.family: AppStyle.fontFamily; font.pixelSize: AppStyle.fontSize; Layout.preferredWidth: 50 }
                TextField {
                    id: newPrimName; Layout.fillWidth: true; placeholderText: "MyPrim"
                    implicitHeight: AppStyle.controlHeight
                    color: AppStyle.textPrimary; font.family: AppStyle.fontFamily; font.pixelSize: AppStyle.fontSize
                    background: Rectangle { color: AppStyle.bgBase; radius: 4; border.color: AppStyle.border }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: "类型:"; color: AppStyle.textPrimary; font.family: AppStyle.fontFamily; font.pixelSize: AppStyle.fontSize; Layout.preferredWidth: 50 }
                ComboBox {
                    id: newPrimType; Layout.fillWidth: true; font.family: AppStyle.fontFamily; font.pixelSize: AppStyle.fontSize
                    implicitHeight: AppStyle.controlHeight
                    model: ["Xform","Sphere","Cube","Cylinder","Cone","Mesh","Camera",""]
                    contentItem: Text {
                        leftPadding: 6
                        text: newPrimType.displayText; font.family: AppStyle.fontFamily; font.pixelSize: AppStyle.fontSize
                        color: AppStyle.textBright; verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle { color: AppStyle.bgInput; radius: 3; border.color: newPrimType.activeFocus ? AppStyle.borderFocus : AppStyle.border }
                    indicator: Text { anchors.right: parent.right; anchors.rightMargin: 6; anchors.verticalCenter: parent.verticalCenter; text: "\u25BE"; color: AppStyle.textMuted; font.family: AppStyle.fontFamily; font.pixelSize: AppStyle.fontSizeTiny }
                    popup: Popup {
                        y: newPrimType.height
                        width: newPrimType.width; implicitHeight: contentItem.implicitHeight + 2; padding: 1
                        background: Rectangle { color: AppStyle.bgWidget; border.color: AppStyle.border; radius: 3 }
                        contentItem: ListView {
                            clip: true; implicitHeight: Math.min(contentHeight, 200)
                            model: newPrimType.delegateModel
                            ScrollBar.vertical: ScrollBar {}
                        }
                    }
                    delegate: ItemDelegate {
                        width: newPrimType.width; height: 24
                        contentItem: Text { text: modelData; color: highlighted ? AppStyle.textWhite : AppStyle.textPrimary; font.family: AppStyle.fontFamily; font.pixelSize: AppStyle.fontSizeSmall; verticalAlignment: Text.AlignVCenter }
                        highlighted: newPrimType.highlightedIndex === index
                        background: Rectangle { color: highlighted ? AppStyle.accent : "transparent" }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    text: "取消"; flat: true
                    contentItem: Text { text: parent.text; color: AppStyle.textSecondary; font.family: AppStyle.fontFamily; font.pixelSize: AppStyle.fontSize }
                    background: Rectangle { color: parent.hovered ? AppStyle.bgMid : "transparent"; radius: 4 }
                    onClicked: addPrimOverlay.close()
                }
                Button {
                    text: "添加"
                    contentItem: Text { text: parent.text; color: AppStyle.textWhite; font.family: AppStyle.fontFamily; font.pixelSize: AppStyle.fontSize }
                    background: Rectangle { color: parent.hovered ? AppStyle.accentDark : AppStyle.accent; radius: 4 }
                    onClicked: {
                        let name = newPrimName.text.trim()
                        if (name === "") return
                        let ok = panel.document.addPrim(addPrimOverlay.parentPath, name, newPrimType.currentText)
                        panel.statusMessage(ok ? "已添加: " + addPrimOverlay.parentPath + "/" + name : "添加失败")
                        addPrimOverlay.close()
                    }
                }
            }
        }
    }
}
