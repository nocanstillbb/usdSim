import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property var document
    property var selectedPrimPaths: []
    signal attributeAdded()

    width: Math.min(360, (parent ? parent.width : 400) - 40)
    padding: 20

    Overlay.modal: Rectangle { color: "#99000000" }
    background: Rectangle { color: AppStyle.bgWidget; radius: 8 }

    function openDialog() {
        newAttrName.text = "new_attr"
        newAttrType.currentIndex = 0
        newAttrCustom.checked = true
        newAttrVariability.currentIndex = 0
        open()
    }

    contentItem: ColumnLayout {
        spacing: 10

        Label {
            text: "Add Attribute"
            font.pixelSize: 16; font.bold: true; color: AppStyle.textWhite
        }
        Label {
            text: "Prim: " + (root.selectedPrimPaths.length > 0 ? root.selectedPrimPaths[0] : "")
            color: AppStyle.textSecondary; font.pixelSize: AppStyle.fontSizeSmall
            elide: Text.ElideMiddle
            Layout.fillWidth: true
        }

        RowLayout {
            spacing: 8
            Label { text: "Name:"; color: AppStyle.textPrimary; font.pixelSize: AppStyle.fontSize; Layout.preferredWidth: 80 }
            TextField {
                id: newAttrName
                text: "new_attr"
                Layout.fillWidth: true; implicitHeight: AppStyle.controlHeight
                color: AppStyle.textWhite; font.pixelSize: AppStyle.fontSize
                background: Rectangle { color: AppStyle.bgInput; radius: 3; border.color: newAttrName.activeFocus ? AppStyle.borderFocus : AppStyle.border }
            }
        }

        RowLayout {
            spacing: 8
            Label { text: "Type:"; color: AppStyle.textPrimary; font.pixelSize: AppStyle.fontSize; Layout.preferredWidth: 80 }
            ComboBox {
                id: newAttrType
                Layout.fillWidth: true
                implicitHeight: AppStyle.controlHeight
                font.pixelSize: AppStyle.fontSize
                model: ["bool","int","uint","int64","float","double","string","token","half",
                        "float2","float3","float4","double2","double3","double4",
                        "point3f","point3d","vector3f","vector3d","normal3f","normal3d",
                        "color3f","color3d","color4f","color4d","quatf","quatd",
                        "matrix2d","matrix3d","matrix4d","texCoord2f","texCoord3f","asset"]
                contentItem: Text {
                    leftPadding: 6
                    text: newAttrType.displayText; font.pixelSize: AppStyle.fontSize
                    color: AppStyle.textBright; verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle { color: AppStyle.bgInput; radius: 3; border.color: newAttrType.activeFocus ? AppStyle.borderFocus : AppStyle.border }
                indicator: Text { anchors.right: parent.right; anchors.rightMargin: 6; anchors.verticalCenter: parent.verticalCenter; text: "\u25BE"; color: AppStyle.textMuted; font.pixelSize: AppStyle.fontSizeTiny }
                popup: Popup {
                    y: newAttrType.height
                    width: newAttrType.width; implicitHeight: contentItem.implicitHeight + 2; padding: 1
                    background: Rectangle { color: AppStyle.bgWidget; border.color: AppStyle.border; radius: 3 }
                    contentItem: ListView {
                        clip: true; implicitHeight: Math.min(contentHeight, 200)
                        model: newAttrType.delegateModel
                        ScrollBar.vertical: ScrollBar {}
                    }
                }
                delegate: ItemDelegate {
                    width: newAttrType.width; height: 24
                    contentItem: Text { text: modelData; color: highlighted ? AppStyle.textWhite : AppStyle.textPrimary; font.pixelSize: AppStyle.fontSizeSmall; verticalAlignment: Text.AlignVCenter }
                    highlighted: newAttrType.highlightedIndex === index
                    background: Rectangle { color: highlighted ? AppStyle.accent : "transparent" }
                }
            }
        }

        RowLayout {
            spacing: 8
            Label { text: "Custom:"; color: AppStyle.textPrimary; font.pixelSize: AppStyle.fontSize; Layout.preferredWidth: 80 }
            CheckBox {
                id: newAttrCustom; checked: true
                implicitWidth: 32; implicitHeight: AppStyle.controlHeight
                indicator: Rectangle {
                    implicitWidth: 16; implicitHeight: 16
                    x: newAttrCustom.leftPadding; y: parent.height / 2 - height / 2
                    radius: 3; color: newAttrCustom.checked ? AppStyle.accent : AppStyle.bgInput
                    border.color: newAttrCustom.checked ? AppStyle.accent : AppStyle.textDim
                    Text { anchors.centerIn: parent; text: newAttrCustom.checked ? "\u2713" : ""; color: AppStyle.textWhite; font.pixelSize: AppStyle.fontSizeSmall }
                }
                contentItem: Item { implicitWidth: 0; implicitHeight: AppStyle.controlHeight }
            }
            Item { Layout.fillWidth: true }
        }

        RowLayout {
            spacing: 8
            Label { text: "Variability:"; color: AppStyle.textPrimary; font.pixelSize: AppStyle.fontSize; Layout.preferredWidth: 80 }
            ComboBox {
                id: newAttrVariability
                Layout.fillWidth: true
                implicitHeight: AppStyle.controlHeight
                font.pixelSize: AppStyle.fontSize
                model: ["Varying", "Uniform"]
                contentItem: Text {
                    leftPadding: 6
                    text: newAttrVariability.displayText; font.pixelSize: AppStyle.fontSize
                    color: AppStyle.textBright; verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle { color: AppStyle.bgInput; radius: 3; border.color: newAttrVariability.activeFocus ? AppStyle.borderFocus : AppStyle.border }
                indicator: Text { anchors.right: parent.right; anchors.rightMargin: 6; anchors.verticalCenter: parent.verticalCenter; text: "\u25BE"; color: AppStyle.textMuted; font.pixelSize: AppStyle.fontSizeTiny }
                popup: Popup {
                    y: newAttrVariability.height
                    width: newAttrVariability.width; implicitHeight: contentItem.implicitHeight + 2; padding: 1
                    background: Rectangle { color: AppStyle.bgWidget; border.color: AppStyle.border; radius: 3 }
                    contentItem: ListView {
                        clip: true; implicitHeight: Math.min(contentHeight, 200)
                        model: newAttrVariability.delegateModel
                        ScrollBar.vertical: ScrollBar {}
                    }
                }
                delegate: ItemDelegate {
                    width: newAttrVariability.width; height: 24
                    contentItem: Text { text: modelData; color: highlighted ? AppStyle.textWhite : AppStyle.textPrimary; font.pixelSize: AppStyle.fontSizeSmall; verticalAlignment: Text.AlignVCenter }
                    highlighted: newAttrVariability.highlightedIndex === index
                    background: Rectangle { color: highlighted ? AppStyle.accent : "transparent" }
                }
            }
        }

        RowLayout {
            spacing: 8
            Layout.alignment: Qt.AlignRight
            Rectangle {
                width: cancelBtnText.width + 20; height: AppStyle.controlHeight; radius: 3
                color: cancelBtnArea.containsMouse ? AppStyle.bgHover : AppStyle.bgInput
                Text { id: cancelBtnText; anchors.centerIn: parent; text: "Cancel"; color: AppStyle.textPrimary; font.pixelSize: AppStyle.fontSize }
                MouseArea { id: cancelBtnArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.close() }
            }
            Rectangle {
                width: addBtnText.width + 20; height: AppStyle.controlHeight; radius: 3
                color: addBtnArea.containsMouse ? AppStyle.accentHover : AppStyle.accent
                Text { id: addBtnText; anchors.centerIn: parent; text: "Add"; color: AppStyle.textWhite; font.pixelSize: AppStyle.fontSize }
                MouseArea { id: addBtnArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: {
                    let ok = root.document.addAttribute(
                        root.selectedPrimPaths,
                        newAttrName.text,
                        newAttrType.currentText,
                        newAttrCustom.checked,
                        newAttrVariability.currentText)
                    if (ok) {
                        root.document.loadAttributes(root.selectedPrimPaths)
                        root.attributeAdded()
                    }
                    root.close()
                }
            }
        }
        }
    }
}
