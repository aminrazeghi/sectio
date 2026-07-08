import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs
import QtCore
import SceneApp 1.0

ApplicationWindow {
    id: window
    visible: true
    width: 1100
    height: 700
    title: "Sectio"

    // Persisted across runs (see Qt.labs.settings docs) -- this is the one
    // source of truth for the theme; every themed color below derives from
    // Material.theme rather than being hand-picked per control.
    Settings {
        id: appSettings
        category: "ui"
        property bool darkMode: true
    }

    Material.theme: appSettings.darkMode ? Material.Dark : Material.Light

    RowLayout {
        anchors.fill: parent
        spacing: 1

        // ---------------- Left panel: object list ----------------
        ColumnLayout {
            // Layout.maximumWidth caps the sidebar at its preferred width.
            // Without it, a nested Layout's implicit size (derived from its
            // children) can override Layout.preferredWidth and balloon this
            // column to consume nearly all available width, squeezing the
            // VTK viewport down to a few px.
            Layout.preferredWidth: 280
            Layout.maximumWidth: 280
            Layout.fillHeight: true
            spacing: 4

            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 6
                Button {
                    text: "Import…"
                    enabled: !sceneController.importing
                    onClicked: importFileDialog.open()
                }
                Item { Layout.fillWidth: true }
                ToolButton {
                    text: "⚙"
                    onClicked: settingsDialog.open()
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: 6
                spacing: 4

                Label { text: "Opacity: " + Math.round(opacitySlider.value * 100) + "%" }
                Slider {
                    id: opacitySlider
                    Layout.fillWidth: true
                    from: 0.05; to: 1.0; value: 1.0
                    onMoved: sceneController.setOpacity(value)
                }
            }

            ListView {
                id: listView
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: sceneModel

                delegate: ItemDelegate {
                    width: listView.width
                    height: 40
                    highlighted: model.selected
                    onClicked: sceneController.selectObject(model.id)

                    contentItem: RowLayout {
                        anchors.fill: parent
                        anchors.margins: 6

                        CheckBox {
                            checked: model.visible
                            onToggled: sceneController.setVisible(model.id, checked)
                        }
                        Label {
                            text: model.name
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        Button {
                            text: "✕"
                            implicitWidth: 28
                            onClicked: sceneController.deleteObject(model.id)
                        }
                    }
                }
            }

            // ---------------- Section view controls ----------------
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: 6
                spacing: 4

                CheckBox {
                    id: sectionEnabledCheck
                    text: "Section View"
                    onToggled: sceneController.setSectionEnabled(checked)
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    enabled: sectionEnabledCheck.checked
                    opacity: enabled ? 1.0 : 0.5
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Axis:" }
                        ButtonGroup { id: axisGroup }
                        RadioButton {
                            text: "X"; checked: true
                            ButtonGroup.group: axisGroup
                            onToggled: if (checked) sceneController.setSectionAxis(0)
                        }
                        RadioButton {
                            text: "Y"
                            ButtonGroup.group: axisGroup
                            onToggled: if (checked) sceneController.setSectionAxis(1)
                        }
                        RadioButton {
                            text: "Z"
                            ButtonGroup.group: axisGroup
                            onToggled: if (checked) sceneController.setSectionAxis(2)
                        }
                    }

                    Label { text: "Distance: " + distanceSlider.value.toFixed(1) }
                    Slider {
                        id: distanceSlider
                        Layout.fillWidth: true
                        from: -200; to: 200; value: 0
                        onMoved: sceneController.setSectionDistance(value)
                    }

                    Label { text: "Rotation: " + rotationSlider.value.toFixed(0) + "°" }
                    Slider {
                        id: rotationSlider
                        Layout.fillWidth: true
                        from: -180; to: 180; value: 0
                        onMoved: sceneController.setSectionRotation(value)
                    }
                }
            }
        }

        // ---------------- Right panel: VTK viewport ----------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            MyVtkItem {
                id: vtkViewport
                objectName: "vtkViewport" // looked up via findChild<MyVTKItem*> in main.cpp
                Layout.fillWidth: true
                Layout.fillHeight: true
                darkMode: appSettings.darkMode
            }

            // Reflects SceneController::importFile()'s background-thread
            // progress (see its Q_PROPERTYs importing/importProgress) --
            // the actual file read/parse never touches the GUI or render
            // thread, so this bar animates smoothly instead of the app
            // freezing while a large STL/STEP file loads.
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 6
                visible: sceneController.importing
                spacing: 8

                ProgressBar {
                    Layout.fillWidth: true
                    from: 0; to: 1
                    value: sceneController.importProgress
                }
                Label {
                    text: Math.round(sceneController.importProgress * 100) + "%"
                }
            }
        }
    }

    // ---------------- Drag-and-drop import ----------------
    // Whole-window drop zone -- routes to the exact same
    // sceneController.importFile() the Import button and file dialog use,
    // so background loading / progress bar / extension filtering all just
    // work unchanged. Each dropped file is imported independently and
    // concurrently (matches clicking "Import..." multiple times fast);
    // the progress bar reflects whichever import last reported progress
    // when several are in flight at once.
    DropArea {
        id: dropArea
        anchors.fill: parent

        property bool acceptableDrag: false

        onEntered: {
            acceptableDrag = drag.urls.some(function (u) {
                var s = u.toString().toLowerCase()
                return s.endsWith(".stl") || s.endsWith(".step") || s.endsWith(".stp")
            })
            drag.accepted = acceptableDrag
        }
        onExited: acceptableDrag = false
        onDropped: {
            for (var i = 0; i < drop.urls.length; i++)
                sceneController.importFile(drop.urls[i])
            acceptableDrag = false
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: dropArea.acceptableDrag
        color: Qt.rgba(0.227, 0.435, 0.690, 0.25)
        border.color: "#3a6fb0"
        border.width: 3
        z: 1000

        Label {
            anchors.centerIn: parent
            text: qsTr("Drop STL/STEP file to import")
            font.pixelSize: 24
            color: "white"
        }
    }

    FileDialog {
        id: importFileDialog
        title: qsTr("Import a model")
        nameFilters: ["CAD/mesh files (*.stl *.step *.stp)", "STL files (*.stl)", "STEP files (*.step *.stp)", "All files (*)"]
        onAccepted: {
            var path = selectedFile.toString();
            sceneController.importFile(path)
        }
    }

    Dialog {
        id: settingsDialog
        title: qsTr("Settings")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Close

        ColumnLayout {
            Switch {
                text: qsTr("Dark mode")
                checked: appSettings.darkMode
                onToggled: appSettings.darkMode = checked
            }
        }
    }
}
