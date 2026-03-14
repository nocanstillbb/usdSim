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

        ListView {
            id: attrList
            Layout.fillWidth: true; Layout.fillHeight: true
            model: panel.document.attrModel
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
                        text: model.value; font.pixelSize: 11
                        color: model.value === "mixed" ? "#888888" : "#dcdcaa"
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
}
