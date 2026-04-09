import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import UsdBrowser 1.0

ApplicationWindow {
    id: root
    width: 1400
    height: 800
    visible: true
    title: doc.isOpen ? ("USD 浏览器 — " + doc.filePath) : "USD 浏览器"
    color: "black"
    font.family: AppStyle.fontFamily

    // ── 数据模型 ──────────────────────────────────────────────
    UsdDocument {
        id: doc
        onStageModified: function(modifiedPrimPaths) {
            // Empty list means unknown — always refresh.
            // Otherwise only refresh if a selected prim was modified.
            if (modifiedPrimPaths.length === 0) {
                attrPanel.refreshValues()
                return
            }
            let sel = root.selectedPrimPaths
            for (let i = 0; i < sel.length; ++i) {
                if (modifiedPrimPaths.indexOf(sel[i]) >= 0) {
                    attrPanel.refreshValues()
                    return
                }
            }
        }
    }

    property string selectedPrimPath: ""
    property var selectedPrimPaths: []

    // ── Undo/Redo keyboard shortcuts ────────────────────────
    Shortcut { sequence: StandardKey.Undo;  onActivated: doc.undo() }
    Shortcut { sequence: StandardKey.Redo;  onActivated: doc.redo() }
    Shortcut { sequence: "Ctrl+Y";          onActivated: doc.redo() }
    Shortcut {
        sequence: StandardKey.Delete
        onActivated: {
            let paths = root.selectedPrimPaths.slice ? root.selectedPrimPaths.slice() : []
            for (let i = paths.length - 1; i >= 0; --i)
                doc.removePrim(paths[i])
            root.selectedPrimPaths = []
            root.selectedPrimPath = ""
            viewportPanel.selectPrimPaths([])
            primTreePanel.selectedPrimPaths = []
            attrPanel.selectedPrimPaths = []
            attrPanel.selectedPrimPath = ""
        }
    }

    // Refresh attribute panel after undo/redo
    Connections {
        target: doc.undoStack
        function onIndexChanged() {
            if (root.selectedPrimPaths.length > 0)
                attrPanel.refreshValues()
        }
    }

    // ── Selection coordination ──────────────────────────────
    Connections {
        target: primTreePanel
        function onSelectionChanged(paths) {
            root.selectedPrimPaths = paths
            root.selectedPrimPath = paths.length > 0 ? paths[paths.length - 1] : ""
            viewportPanel.selectPrimPaths(paths)
            attrPanel.selectedPrimPaths = paths
            attrPanel.selectedPrimPath = root.selectedPrimPath
        }
        function onStatusMessage(msg) { statusText.text = msg }
    }

    Connections {
        target: viewportPanel
        function onSelectionChanged(paths) {
            root.selectedPrimPaths = paths
            root.selectedPrimPath = paths.length > 0 ? paths[paths.length - 1] : ""
            primTreePanel.selectedPrimPaths = paths
            attrPanel.selectedPrimPaths = paths
            attrPanel.selectedPrimPath = root.selectedPrimPath
        }
        function onAttributeRefreshNeeded() {
            attrPanel.refreshValues()
        }
    }

    Connections {
        target: attrPanel
        function onStatusMessage(msg) { statusText.text = msg }
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
                    { label: "打开",   enabled: true,        action: function(){ root.openFileAction() } },
                    { label: "保存",   enabled: doc.isOpen,  action: function(){ viewportPanel.viewportItem.saveCameraToStage(); doc.save(); statusText.text = "已保存" } },
                    { label: "另存为", enabled: doc.isOpen,  action: function(){ root.saveAsAction() } },
                    { label: "关闭",   enabled: doc.isOpen,  action: function(){
                        doc.close(); doc.clearAttributes()
                        root.selectedPrimPath = ""; root.selectedPrimPaths = []
                        attrPanel.selectedPrimPaths = []; attrPanel.selectedPrimPath = ""
                    }}
                ]
                delegate: Button {
                    text: modelData.label
                    enabled: modelData.enabled
                    flat: true
                    contentItem: Text { text: parent.text; color: parent.enabled ? "#cccccc" : "#555555"; font.family: AppStyle.fontFamily; font.pixelSize: 13 }
                    background: Rectangle { color: parent.hovered ? "#333333" : "transparent"; radius: 4 }
                    onClicked: modelData.action()
                }
            }

            Rectangle { width: 1; height: 20; color: "#444444"; Layout.alignment: Qt.AlignVCenter }

            Button {
                text: "撤销"
                enabled: doc.undoStack ? doc.undoStack.canUndo : false
                flat: true
                contentItem: Text { text: parent.text; color: parent.enabled ? "#cccccc" : "#555555"; font.family: AppStyle.fontFamily; font.pixelSize: 13 }
                background: Rectangle { color: parent.hovered && parent.enabled ? "#333333" : "transparent"; radius: 4 }
                onClicked: doc.undo()
            }
            Button {
                text: "重做"
                enabled: doc.undoStack ? doc.undoStack.canRedo : false
                flat: true
                contentItem: Text { text: parent.text; color: parent.enabled ? "#cccccc" : "#555555"; font.family: AppStyle.fontFamily; font.pixelSize: 13 }
                background: Rectangle { color: parent.hovered && parent.enabled ? "#333333" : "transparent"; radius: 4 }
                onClicked: doc.redo()
            }

            Item { Layout.fillWidth: true }
        }
    }

    // ── 主体三栏 ─────────────────────────────────────────────
    SplitView {
        id: splitView
        anchors.fill: parent
        orientation: Qt.Horizontal
        // Click anywhere to clear search field focus
        TapHandler { onTapped: splitView.forceActiveFocus() }

        handle: Rectangle {
            id: handleControl
            property var control: splitView
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

                property int length: handleControl.SplitHandle.pressed ? 3 : 8
                readonly property int thickness: handleControl.SplitHandle.pressed ? 3 : 1

                Behavior on length {
                    NumberAnimation { duration: 100 }
                }
            }
        }

        // 左栏：Prim 树
        PrimTreePanel {
            id: primTreePanel
            SplitView.preferredWidth: 320
            SplitView.minimumWidth:   220
            document: doc
        }

        // 中栏：3D 视口
        ViewportPanel {
            id: viewportPanel
            SplitView.fillWidth: true
            document: doc
        }

        // 右栏：属性 + 历史 TabView
        Rectangle {
            SplitView.preferredWidth: 320
            SplitView.minimumWidth:   200
            color: "#1e1e1e"

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Tab 栏
                Rectangle {
                    id: tabBar
                    Layout.fillWidth: true; height: 28; color: "#252525"
                    Row {
                        id: tabRow
                        anchors.fill: parent
                        Repeater {
                            model: [
                                { label: "属性", idx: 0 },
                                { label: "历史记录", idx: 1 }
                            ]
                            delegate: Rectangle {
                                width: 80; height: 28
                                color: tabItemMouse.containsMouse ? "#2a2a2a" : "#252525"

                                Label {
                                    anchors.centerIn: parent
                                    text: modelData.label
                                    color: rightTabBar.currentIndex === modelData.idx ? "#cccccc" : "#888888"
                                    font.family: AppStyle.fontFamily; font.pixelSize: 11
                                }
                                MouseArea {
                                    id: tabItemMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: rightTabBar.currentIndex = modelData.idx
                                }
                            }
                        }
                    }
                    // 滑动下划线
                    Rectangle {
                        id: tabIndicator
                        width: 80; height: 2
                        color: "#0078d4"
                        y: parent.height - height
                        x: rightTabBar.currentIndex * 80
                        Behavior on x { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
                    }
                }

                // Tab 内容
                StackLayout {
                    id: rightTabBar
                    Layout.fillWidth: true; Layout.fillHeight: true

                    AttributePanel {
                        id: attrPanel
                        document: doc
                    }

                    HistoryPanel {
                        id: historyPanel
                        document: doc
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
            Label { id: statusText; text: "就绪"; color: "white"; font.family: AppStyle.fontFamily; font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
            Label {
                text: doc.isOpen ? (doc.primPaths.length + " 个 Prim") : ""
                color: "white"; font.family: AppStyle.fontFamily; font.pixelSize: 11
            }
        }
    }

    // ── 文件对话框（通过 C++ QFileDialog，始终使用原生对话框）────
    function openFileAction() {
        let path = doc.showOpenFileDialog()
        if (path !== "") {
            let ok = doc.open(path)
            statusText.text = ok ? "已打开: " + path : "失败: " + doc.errorString
        }
    }
    function saveAsAction() {
        viewportPanel.viewportItem.saveCameraToStage()
        let path = doc.showSaveFileDialog()
        if (path !== "") {
            let ok = doc.saveAs(path)
            statusText.text = ok ? "已保存: " + path : "失败"
        }
    }
}
