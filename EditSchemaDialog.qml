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
    signal schemaChanged()

    property var appliedSchemas: []
    property var availableSchemas: []
    property var filteredSchemas: []

    width: Math.min(450, (parent ? parent.width : 500) - 40)
    height: Math.min(500, (parent ? parent.height : 600) - 80)
    padding: 16

    Overlay.modal: Rectangle { color: "#99000000" }
    background: Rectangle { color: AppStyle.bgWidget; radius: 8 }

    function openDialog() {
        let primPath = selectedPrimPaths[0]
        appliedSchemas = document.getAppliedSchemas(primPath)
        availableSchemas = document.getAvailableApiSchemas()
        schemaSearchField.text = ""
        filterSchemas("")
        open()
    }

    function filterSchemas(text) {
        let lowerText = text.toLowerCase()
        if (lowerText === "") {
            filteredSchemas = availableSchemas
        } else {
            let filtered = []
            for (let i = 0; i < availableSchemas.length; i++) {
                if (availableSchemas[i].toLowerCase().indexOf(lowerText) >= 0)
                    filtered.push(availableSchemas[i])
            }
            filteredSchemas = filtered
        }
    }

    function refresh() {
        let primPath = selectedPrimPaths[0]
        appliedSchemas = document.getAppliedSchemas(primPath)
        document.loadAttributes(selectedPrimPaths)
        schemaChanged()
    }

    contentItem: ColumnLayout {
        spacing: 8

        Label {
            text: "Edit API Schema"
            font.pixelSize: 16; font.bold: true; color: AppStyle.textWhite
        }
        Label {
            text: "Prim: " + (root.selectedPrimPaths.length > 0 ? root.selectedPrimPaths[0] : "")
            color: AppStyle.textSecondary; font.pixelSize: AppStyle.fontSizeSmall
            elide: Text.ElideMiddle
            Layout.fillWidth: true
        }

        Label { text: "Available API Schemas"; color: AppStyle.textPrimary; font.pixelSize: 13; font.bold: true }
        TextField {
            id: schemaSearchField
            placeholderText: "Search schemas..."
            placeholderTextColor: AppStyle.textDisabled
            Layout.fillWidth: true; implicitHeight: AppStyle.controlHeight
            color: AppStyle.textWhite; font.pixelSize: AppStyle.fontSize
            background: Rectangle { color: AppStyle.bgInput; radius: 3; border.color: schemaSearchField.activeFocus ? AppStyle.borderFocus : AppStyle.border }
            onTextChanged: root.filterSchemas(text)
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 150
            color: AppStyle.bgBase
            radius: 4

            ListView {
                id: availableSchemaList
                anchors.fill: parent
                anchors.margins: 2
                clip: true
                model: root.filteredSchemas
                delegate: Rectangle {
                    width: availableSchemaList.width
                    height: 26
                    color: schemaItemArea.containsMouse ? AppStyle.bgInput : "transparent"
                    radius: 3

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left; anchors.leftMargin: 8
                        text: modelData
                        color: AppStyle.textBright; font.pixelSize: AppStyle.fontSizeSmall
                    }
                    MouseArea {
                        id: schemaItemArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.document.applyApiSchema(root.selectedPrimPaths, modelData)
                            root.refresh()
                        }
                    }
                }
            }
        }

        Label { text: "Applied APIs"; color: AppStyle.textPrimary; font.pixelSize: 13; font.bold: true }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 120
            color: AppStyle.bgBase
            radius: 4

            ListView {
                id: appliedSchemaList
                anchors.fill: parent
                anchors.margins: 2
                clip: true
                model: root.appliedSchemas
                delegate: Rectangle {
                    width: appliedSchemaList.width
                    height: 28
                    color: "transparent"
                    radius: 3

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 4
                        spacing: 4

                        Text {
                            text: modelData
                            color: AppStyle.textBright; font.pixelSize: AppStyle.fontSizeSmall
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        Rectangle {
                            width: removeLabel.width + 12
                            height: 20
                            radius: 3
                            color: removeArea.containsMouse ? "#882222" : "#552222"

                            Text {
                                id: removeLabel
                                anchors.centerIn: parent
                                text: "Remove"
                                color: "#ff8888"; font.pixelSize: AppStyle.fontSizeTiny
                            }
                            MouseArea {
                                id: removeArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    root.document.removeApiSchema(root.selectedPrimPaths, modelData)
                                    root.refresh()
                                }
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            Rectangle {
                width: closeBtnText.width + 20; height: AppStyle.controlHeight; radius: 3
                color: closeBtnArea.containsMouse ? AppStyle.bgHover : AppStyle.bgInput
                Text { id: closeBtnText; anchors.centerIn: parent; text: "Close"; color: AppStyle.textPrimary; font.pixelSize: AppStyle.fontSize }
                MouseArea { id: closeBtnArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.close() }
            }
        }
    }
}
