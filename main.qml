import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import UsdBrowser 1.0

ApplicationWindow {
    id: root
    width: 1100
    height: 700
    visible: true
    title: doc.isOpen ? ("USD 浏览器 — " + doc.filePath) : "USD 浏览器"
    color: "black"

    // ── 数据模型 ──────────────────────────────────────────────
    UsdDocument { id: doc }
    ListModel    { id: attrModel }

    property string selectedPrimPath: ""

    function loadAttrs(path) {
        root.selectedPrimPath = path
        attrModel.clear()
        let attrs = doc.getAttributes(path)
        for (let i = 0; i < attrs.length; i++) attrModel.append(attrs[i])
        let info = doc.getPrimInfo(path)
        primTypeLabel.text = info.typeName || ""
        statusText.text = path + "  (" + attrs.length + " 个属性)"
    }

    // ── 工具栏 ───────────────────────────────────────────────
    header: Rectangle {
        height: 44
        color: "#1f1f1f"
        RowLayout {
            anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
            spacing: 4

            Repeater {
                model: [
                    { label: "打开",   enabled: true,        action: function(){ fileOpenOverlay.show(false) } },
                    { label: "测试文件", enabled: true,      action: function(){ doc.open("/home/cnf2025581067/source/repos/test_qmlmcp_server/test_scene.usda") } },
                    { label: "保存",   enabled: doc.isOpen,  action: function(){ doc.save(); statusText.text = "已保存" } },
                    { label: "另存为", enabled: doc.isOpen,  action: function(){ fileOpenOverlay.show(true) } },
                    { label: "关闭",   enabled: doc.isOpen,  action: function(){ doc.close(); attrModel.clear(); root.selectedPrimPath = "" } }
                ]
                delegate: Button {
                    text: modelData.label
                    enabled: modelData.enabled
                    flat: true
                    contentItem: Text { text: parent.text; color: parent.enabled ? "#cccccc" : "#555555"; font.pixelSize: 13 }
                    background: Rectangle { color: parent.hovered ? "#333333" : "transparent"; radius: 4 }
                    onClicked: modelData.action()
                }
            }
            Item { Layout.fillWidth: true }
        }
    }

    // ── 主体三栏 ─────────────────────────────────────────────
    SplitView {
        id:split_view
        anchors.fill: parent
        orientation: Qt.Horizontal

        handle: Rectangle {
            id:handle_conttrol
            property var control :split_view
            implicitWidth: control.orientation === Qt.Horizontal ? 6 : control.width
            implicitHeight: control.orientation === Qt.Horizontal ? control.height : 6
            color: "transparent"

            Rectangle {
                color: "transparent"
                width: control.orientation === Qt.Horizontal ? thickness : length
                height: control.orientation === Qt.Horizontal ? length : thickness
                radius: thickness
                x: (parent.width - width) / 2
                y: (parent.height - height) / 2

                property int length: handle_conttrol.SplitHandle.pressed ? 3 : 8
                readonly property int thickness: handle_conttrol.SplitHandle.pressed ? 3 : 1

                Behavior on length {
                    NumberAnimation {
                        duration: 100
                    }
                }
            }
        }

        // 左栏：Prim 树
        Rectangle {
            SplitView.preferredWidth: 220
            SplitView.minimumWidth:   150
            color: "#1a1a1a"

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
                    selectionModel: ItemSelectionModel {id:sel}
                    model: doc.primTreeModel;
                    property int hoveredRow: -1

                    ScrollBar.vertical: ScrollBar {}
                    columnWidthProvider: function(col) { return width }
                    rowHeightProvider:  function(row){return 30}

                    maximumFlickVelocity: 0
                    flickDeceleration: 100000
                    boundsBehavior: Flickable.StopAtBounds

                    delegate: Item {
                        id: primDelegate
                        readonly property real indentation: 15
                        readonly property real padding: 15
                        // Assigned to by TreeView:
                        required property TreeView treeView
                        required property bool isTreeNode
                        required property bool expanded
                        required property bool hasChildren
                        required property int depth
                        required property int row
                        required property int column
                        required property bool current
                        required property bool selected

                        //required property string name
                        //required property string path
                        //required property string typeName
                        //required property bool   isActive

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
                            onTapped: {
                                treeView.selectionModel.setCurrentIndex( treeView.index(row, column), ItemSelectionModel.ClearAndSelect | ItemSelectionModel.Rows )
                                loadAttrs(model.path)
                            }
                            onDoubleTapped: {
                                let index = treeView.index(row, column)
                                treeView.selectionModel.setCurrentIndex(index, ItemSelectionModel.ClearAndSelect | ItemSelectionModel.Rows)
                                loadAttrs(model.path)
                            }
                        }

                        // Rotate indicator when expanded by the user
                        // (requires TreeView to have a selectionModel)
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
                            color:
                            {
                                if (primDelegate.current)
                                    return "#007acc"
                                else if (primTree.hoveredRow === row)
                                    return "#70007acc"

                                return   row % 2 === 0 ? "#232323" : "#1e1e1e"
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
                                onSingleTapped: {
                                    let index = treeView.index(row, column)
                                    treeView.selectionModel.setCurrentIndex(index, ItemSelectionModel.ClearAndSelect | ItemSelectionModel.Rows)
                                    treeView.toggleExpanded(row)
                                    loadAttrs(model.path)
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
                        let ok = doc.removePrim(primCtxMenu.primPath)
                        if (ok) { attrModel.clear(); root.selectedPrimPath = "" }
                        statusText.text = ok ? "已删除" : "删除失败"
                    }
                }
            }
        }

        // 中栏：3D 视口
        Rectangle {
            SplitView.fillWidth: true
            color: "#111111"

            UsdViewport {
                id: viewport
                anchors.fill: parent
                document: doc
            }

            // 视口提示标签
            Label {
                visible: !doc.isOpen
                anchors.centerIn: parent
                text: "打开 USD 文件后在此显示 3D 场景\n拖拽旋转 · alt+拖拽平移 · 滚轮缩放"
                color: "#555555"; font.pixelSize: 14
                horizontalAlignment: Text.AlignHCenter
            }

            // 右上角控制提示
            Label {
                visible: doc.isOpen
                anchors { top: parent.top; right: parent.right; margins: 8 }
                text: "拖拽旋转 · alt+拖拽平移 · 滚轮缩放"
                color: "#666666"; font.pixelSize: 11
            }
        }

        // 右栏：属性编辑器
        Rectangle {
            SplitView.preferredWidth: 320
            SplitView.minimumWidth:   200
            color: "#1e1e1e"

            ColumnLayout {
                anchors.fill: parent; spacing: 0

                Rectangle {
                    Layout.fillWidth: true; height: 28; color: "#252525"
                    RowLayout {
                        anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                        Label {
                            text: root.selectedPrimPath !== "" ? root.selectedPrimPath : "未选中 Prim"
                            color: "#cccccc"; font.bold: true; font.pixelSize: 11
                            elide: Text.ElideLeft; Layout.fillWidth: true
                        }
                        Label { id: primTypeLabel; color: "#888888"; font.pixelSize: 11 }
                    }
                }

                ListView {
                    id: attrList
                    Layout.fillWidth: true; Layout.fillHeight: true
                    model: attrModel;
                    clip: true
                    ScrollBar.vertical: ScrollBar {}


                    maximumFlickVelocity: 0
                    flickDeceleration: 100000
                    boundsBehavior: Flickable.StopAtBounds

                    header: Rectangle {
                        width: attrList.width; height: 22; color: "#2d2d2d"
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            spacing: 0
                            Label { text: "属性名"; color: "#888888"; font.pixelSize: 10; Layout.preferredWidth: 150 }
                            Label { text: "类型";   color: "#888888"; font.pixelSize: 10; Layout.preferredWidth: 80 }
                            Label { text: "值";     color: "#888888"; font.pixelSize: 10; Layout.fillWidth: true }
                        }
                    }

                    delegate: Rectangle {
                        width: attrList.width; height: 32
                        color: index % 2 === 0 ? "#232323" : "#1e1e1e"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            anchors.rightMargin: 6
                            spacing: 0

                            Label {
                                text: model.name; font.pixelSize: 11
                                color: model.isCustom ? "#9cdcfe" : "#c8c8c8"
                                Layout.preferredWidth: 150; elide: Text.ElideRight
                            }
                            Label {
                                text: model.typeName; font.pixelSize: 10; color: "#777777"
                                Layout.preferredWidth: 80; elide: Text.ElideRight
                            }
                            TextField {
                                id: valField
                                Layout.fillWidth: true
                                text: model.value; font.pixelSize: 11; color: "#dcdcaa"
                                // 捕获 model 角色，避免 signal handler 里 model 上下文失效
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
                                    console.log(`root.selectedPrimPath, _name, text:  ${root.selectedPrimPath}, ${_name}, ${text}`)
                                    let ok = doc.setAttribute(root.selectedPrimPath, _name, text)
                                    if (ok) {
                                        statusText.text = "已更新: " + _name + " = " + text
                                        Qt.callLater(function(){ loadAttrs(root.selectedPrimPath) })
                                    } else {
                                        statusText.text = "不支持直接编辑: " + _typeName
                                        text = _value
                                    }
                                    _busy = false
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ── 状态栏 ──────────────────────────────────────────────
    footer: Rectangle {
        height: 22; color: "#007acc"
        RowLayout {
            anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
            Label { id: statusText; text: "就绪"; color: "white"; font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
            Label {
                text: doc.isOpen ? (doc.primPaths.length + " 个 Prim") : ""
                color: "white"; font.pixelSize: 11
            }
        }
    }

    // ══════════════════════════════════════════════════════════
    //  自定义文件对话框（替代系统 FileDialog）
    // ══════════════════════════════════════════════════════════
    Rectangle {
        id: fileOpenOverlay
        anchors.fill: parent; color: "#99000000"; visible: false; z: 200

        property bool isSaveAs: false
        function show(saveAs) { isSaveAs = saveAs; pathField.text = doc.filePath; visible = true; pathField.forceActiveFocus() }

        MouseArea { anchors.fill: parent; onClicked: fileOpenOverlay.visible = false }

        Rectangle {
            anchors.centerIn: parent; width: 520; height: 140
            color: "#2d2d2d"; radius: 8
            layer.enabled: true
            layer.effect: null   // 去掉 QtQuick.Effects 依赖

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 12

                Label {
                    text: fileOpenOverlay.isSaveAs ? "另存为" : "打开 USD 文件"
                    color: "#ffffff"; font.pixelSize: 14; font.bold: true
                }

                TextField {
                    id: pathField
                    Layout.fillWidth: true
                    placeholderText: "/path/to/scene.usda"
                    color: "#cccccc"
                    background: Rectangle { color: "#1e1e1e"; radius: 4; border.color: "#555555" }
                    onAccepted: confirmBtn.clicked()
                }

                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    Button {
                        text: "取消"; flat: true
                        contentItem: Text { text: parent.text; color: "#aaaaaa"; font.pixelSize: 12 }
                        background: Rectangle { color: parent.hovered ? "#333333" : "transparent"; radius: 4 }
                        onClicked: fileOpenOverlay.visible = false
                    }
                    Button {
                        id: confirmBtn
                        text: fileOpenOverlay.isSaveAs ? "保存" : "打开"
                        contentItem: Text { text: parent.text; color: "#ffffff"; font.pixelSize: 12 }
                        background: Rectangle { color: parent.hovered ? "#005fa3" : "#0078d4"; radius: 4 }
                        onClicked: {
                            let path = pathField.text.trim()
                            if (path === "") return
                            let ok = fileOpenOverlay.isSaveAs
                                ? doc.saveAs(path)
                                : doc.open(path)
                            statusText.text = ok
                                ? (fileOpenOverlay.isSaveAs ? "已保存: " + path : "已打开: " + path)
                                : "失败: " + doc.errorString
                            fileOpenOverlay.visible = false
                        }
                    }
                }
            }
        }
    }

    // ══════════════════════════════════════════════════════════
    //  自定义"添加 Prim"对话框
    // ══════════════════════════════════════════════════════════
    Rectangle {
        id: addPrimOverlay
        anchors.fill: parent; color: "#99000000"; visible: false; z: 200

        property string parentPath: ""

        MouseArea { anchors.fill: parent; onClicked: addPrimOverlay.visible = false }

        Rectangle {
            anchors.centerIn: parent; width: 360; height: 200
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
                            let ok = doc.addPrim(addPrimOverlay.parentPath, name, newPrimType.currentText)
                            statusText.text = ok ? "已添加: " + addPrimOverlay.parentPath + "/" + name : "添加失败"
                            addPrimOverlay.visible = false
                        }
                    }
                }
            }
        }
    }
}
