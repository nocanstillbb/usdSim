import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: panel
    color: "#1a1a1a"

    required property var document

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true; height: 28; color: "#252525"
            RowLayout {
                anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                Label { text: "历史记录"; color: "#aaaaaa"; font.bold: true; font.pixelSize: 11 }
                Item { Layout.fillWidth: true }
                Label {
                    visible: panel.document.undoStack !== null
                    text: {
                        let stack = panel.document.undoStack
                        if (!stack) return ""
                        return stack.index + " / " + stack.count
                    }
                    color: "#888888"; font.pixelSize: 10
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8; Layout.rightMargin: 8; Layout.topMargin: 4
            spacing: 4

            Button {
                text: "撤销"
                enabled: panel.document.undoStack ? panel.document.undoStack.canUndo : false
                flat: true
                contentItem: Text { text: parent.text; color: parent.enabled ? "#cccccc" : "#555555"; font.family: AppStyle.fontFamily; font.pixelSize: 11 }
                background: Rectangle { color: parent.hovered && parent.enabled ? "#333333" : "transparent"; radius: 4 }
                onClicked: panel.document.undo()
            }

            Button {
                text: "重做"
                enabled: panel.document.undoStack ? panel.document.undoStack.canRedo : false
                flat: true
                contentItem: Text { text: parent.text; color: parent.enabled ? "#cccccc" : "#555555"; font.family: AppStyle.fontFamily; font.pixelSize: 11 }
                background: Rectangle { color: parent.hovered && parent.enabled ? "#333333" : "transparent"; radius: 4 }
                onClicked: panel.document.redo()
            }

            Item { Layout.fillWidth: true }
        }

        ListView {
            id: historyList
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            model: historyModel
            ScrollBar.vertical: ScrollBar {}

            maximumFlickVelocity: 0
            flickDeceleration: 100000
            boundsBehavior: Flickable.StopAtBounds

            delegate: Rectangle {
                width: historyList.width; height: 22
                color: model.active ? (index % 2 === 0 ? "#232323" : "#1e1e1e") : "#1a1a1a"

                Label {
                    anchors { verticalCenter: parent.verticalCenter; left: parent.left; right: parent.right; leftMargin: 8; rightMargin: 8 }
                    text: (model.active ? "● " : "○ ") + model.label
                    color: model.active ? "#cccccc" : "#555555"
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }
            }
        }
    }

    ListModel { id: historyModel }

    Connections {
        target: panel.document.undoStack
        function onIndexChanged() {
            _rebuildHistory()
        }
    }

    Component.onCompleted: _rebuildHistory()

    function _rebuildHistory() {
        historyModel.clear()
        let stack = panel.document.undoStack
        if (!stack) return
        for (let i = 0; i < stack.count; i++) {
            historyModel.append({
                label: stack.commandText(i),
                active: i < stack.index
            })
        }
        if (historyList.count > 0)
            historyList.positionViewAtEnd()
    }
}
