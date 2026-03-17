import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: panel
    color: "#1e1e1e"

    required property var document
    property var selectedPrimPaths: []
    property string selectedPrimPath: ""
    signal statusMessage(string msg)

    property string _primTypeText: ""
    property real _nameColWidth: 150
    property real _typeColWidth: 80

    onSelectedPrimPathsChanged: _loadAttributes()

    function refreshValues() {
        if (selectedPrimPaths.length > 0)
            document.refreshAttributes(selectedPrimPaths)
    }

    function reload() {
        _loadAttributes()
    }

    function _loadAttributes() {
        let paths = selectedPrimPaths
        if (paths.length === 0) {
            document.clearAttributes()
            _primTypeText = ""
            statusMessage("就绪")
            return
        }
        document.loadAttributes(paths)
        let count = document.attrModel.rowCount()
        if (paths.length === 1) {
            let info = document.getPrimInfo(paths[0])
            _primTypeText = info.typeName || ""
            statusMessage(paths[0] + "  (" + count + " 个属性)")
        } else {
            _primTypeText = paths.length + " 个 Prim"
            statusMessage("已选中 " + paths.length + " 个 Prim  (" + count + " 个共有属性)")
        }
    }

    // Handle attribute editing from delegate (avoids id scope issues)
    function _handleEdit(name, typeName, oldValue, newText) {
        let paths = selectedPrimPaths
        let ok = false
        if (paths.length > 1) {
            ok = document.setAttributeMulti(paths, name, newText)
        } else {
            ok = document.setAttribute(selectedPrimPath, name, newText)
        }
        if (ok) {
            statusMessage("已更新: " + name + " = " + newText)
            Qt.callLater(_loadAttributes)
        } else {
            statusMessage("不支持直接编辑: " + typeName)
        }
        return ok
    }

    ColumnLayout {
        anchors.fill: parent; spacing: 0

        Rectangle {
            Layout.fillWidth: true; height: 28; color: "#252525"
            TapHandler { onTapped: panel.forceActiveFocus() }
            RowLayout {
                anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                Label {
                    text: panel.selectedPrimPaths.length > 1
                        ? (panel.selectedPrimPaths.length + " 个 Prim 已选中")
                        : (panel.selectedPrimPath !== "" ? panel.selectedPrimPath : "未选中 Prim")
                    color: "#cccccc"; font.bold: true; font.pixelSize: 11
                    elide: Text.ElideLeft; Layout.fillWidth: true
                }
                Label { text: panel._primTypeText; color: "#888888"; font.pixelSize: 11 }
            }
        }

        // Column header with resize handles
        Rectangle {
            id: colHeader
            Layout.fillWidth: true; height: 22; color: "#2d2d2d"
            TapHandler { onTapped: panel.forceActiveFocus() }

            // Name column header
            Item {
                id: nameHeaderCol
                anchors.left: parent.left; anchors.right: typeHeaderCol.left
                height: parent.height
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left; anchors.leftMargin: 6
                    text: "属性名"; color: "#888888"; font.pixelSize: 10
                }
            }

            // Type column header
            Item {
                id: typeHeaderCol
                width: panel._typeColWidth; height: parent.height
                anchors.right: valueHeaderCol.left
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left; anchors.leftMargin: 4
                    text: "类型"; color: "#888888"; font.pixelSize: 10
                }
                // Left resize handle (between name and type)
                MouseArea {
                    width: 6; height: parent.height
                    anchors.left: parent.left; anchors.leftMargin: -3
                    cursorShape: Qt.SplitHCursor
                    property real startX: 0
                    property real startW: 0
                    onPressed: function(mouse) {
                        startX = mapToItem(colHeader, mouse.x, 0).x
                        startW = panel._nameColWidth
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed) return
                        let currX = mapToItem(colHeader, mouse.x, 0).x
                        let delta = currX - startX
                        panel._nameColWidth = Math.max(60, startW + delta)
                    }
                }
            }

            // Value column header
            Item {
                id: valueHeaderCol
                anchors.right: parent.right
                width: parent.width - panel._nameColWidth - panel._typeColWidth
                height: parent.height
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left; anchors.leftMargin: 4
                    text: "值"; color: "#888888"; font.pixelSize: 10
                }
                // Left resize handle (between type and value)
                MouseArea {
                    width: 6; height: parent.height
                    anchors.left: parent.left; anchors.leftMargin: -3
                    cursorShape: Qt.SplitHCursor
                    property real startX: 0
                    property real startW: 0
                    onPressed: function(mouse) {
                        startX = mapToItem(colHeader, mouse.x, 0).x
                        startW = panel._typeColWidth
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed) return
                        let currX = mapToItem(colHeader, mouse.x, 0).x
                        let delta = currX - startX
                        panel._typeColWidth = Math.max(40, startW + delta)
                    }
                }
            }
        }

        // Search bar
        Rectangle {
            Layout.fillWidth: true; height: 32; color: "#252525"
            TextField {
                id: searchField
                anchors.fill: parent
                anchors.leftMargin: 6; anchors.rightMargin: 6
                anchors.topMargin: 3; anchors.bottomMargin: 3
                placeholderText: "搜索属性..."
                placeholderTextColor: "#555555"
                font.pixelSize: 11; color: "#cccccc"
                background: Rectangle { color: "#1e1e1e"; radius: 3; border.color: searchField.activeFocus ? "#0078d4" : "#333333" }
            }
        }

        ListView {
            id: attrList
            Layout.fillWidth: true; Layout.fillHeight: true
            model: panel.document.attrModel
            clip: true
            ScrollBar.vertical: ScrollBar {}

            maximumFlickVelocity: 0
            flickDeceleration: 100000
            boundsBehavior: Flickable.StopAtBounds

            property int selectedIndex: -1

            delegate: Rectangle {
                id: attrDelegate
                readonly property bool _matches: {
                    let q = searchField.text.toLowerCase()
                    return q === "" || model.name.toLowerCase().indexOf(q) >= 0
                        || model.typeName.toLowerCase().indexOf(q) >= 0
                        || model.value.toLowerCase().indexOf(q) >= 0
                }
                width: attrList.width; height: _matches ? 32 : 0
                visible: _matches
                color: {
                    if (attrList.selectedIndex === index)
                        return "#007acc"
                    else if (hoverHandler.hovered)
                        return "#70007acc"
                    return index % 2 === 0 ? "#232323" : "#1e1e1e"
                }

                HoverHandler { id: hoverHandler }
                TapHandler {
                    onTapped: { attrList.selectedIndex = index; attrList.forceActiveFocus() }
                }

                // Right-click context menu
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    onClicked: function(mouse) {
                        attrList.selectedIndex = index
                        attrCtxMenu._targetAttrName = model.name
                        attrCtxMenu._targetIsCustom = model.isCustom
                        attrCtxMenu.popup()
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    spacing: 0

                    Label {
                        text: model.name; font.pixelSize: 11
                        color: model.isCustom ? "#9cdcfe" : "#c8c8c8"
                        Layout.preferredWidth: panel._nameColWidth - 6
                        elide: Text.ElideRight; clip: true
                    }
                    Label {
                        text: model.typeName; font.pixelSize: 10; color: "#777777"
                        Layout.preferredWidth: panel._typeColWidth
                        elide: Text.ElideRight; clip: true
                    }
                    TextField {
                        id: valField
                        Layout.fillWidth: true
                        readOnly: model.readOnly
                        text: model.value; font.pixelSize: 11
                        color: model.readOnly ? "#666666" : (model.value === "mixed" ? "#888888" : "#dcdcaa")
                        font.italic: model.value === "mixed"
                        property string _name:     model.name     || ""
                        property string _typeName: model.typeName || ""
                        property string _value:    model.value    || ""
                        background: Rectangle {
                            color: valField.activeFocus ? "#2d2d2d" : "transparent"
                            border.color: valField.activeFocus ? "#0078d4" : "transparent"
                            radius: 2
                        }
                        property bool _busy: false
                        onEditingFinished: {
                            if (_busy || text === _value) return
                            _busy = true
                            let ok = panel._handleEdit(_name, _typeName, _value, text)
                            if (!ok) text = _value
                            _busy = false
                        }
                    }
                }
            }
        }
    }

    // ── Attribute context menu ──
    Menu {
        id: attrCtxMenu
        property string _targetAttrName: ""
        property bool _targetIsCustom: false
        topPadding: 2; bottomPadding: 2

        delegate: MenuItem {
            id: attrCtxDelegate
            implicitWidth: 160
            implicitHeight: 28
            contentItem: Text {
                text: attrCtxDelegate.text
                font.pixelSize: 12
                color: attrCtxDelegate.highlighted ? "#ffffff" : "#cccccc"
                verticalAlignment: Text.AlignVCenter
                leftPadding: 8; rightPadding: 8
            }
            background: Rectangle {
                implicitWidth: 160; implicitHeight: 28
                color: attrCtxDelegate.highlighted ? "#0078d4" : "transparent"
            }
        }

        background: Rectangle {
            implicitWidth: 160; implicitHeight: 10
            color: "#2d2d2d"; border.color: "#555555"; radius: 4
        }

        Action {
            text: "删除属性"
            enabled: attrCtxMenu._targetIsCustom
            onTriggered: {
                let ok = panel.document.removeAttribute(panel.selectedPrimPaths, attrCtxMenu._targetAttrName)
                if (ok) {
                    panel.document.loadAttributes(panel.selectedPrimPaths)
                    panel.statusMessage("已删除: " + attrCtxMenu._targetAttrName)
                }
            }
        }
    }
}
