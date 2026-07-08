import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs
import SceneApp 1.0

ApplicationWindow {
    id: window
    visible: true
    width: 1100
    height: 700
    title: "STP Viewer"

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
                Button { text: "+ Sphere"; onClicked: sceneController.createPrimitive("sphere") }
                Button { text: "+ Cube"; onClicked: sceneController.createPrimitive("cube") }
                // Button { text: "+ Cone"; onClicked: sceneController.createPrimitive("cone") }
                Button { text: "Import…"; onClicked: importFileDialog.open() }
            }

            ListView {
                id: listView
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: sceneModel

                delegate: Rectangle {
                    width: listView.width
                    height: 40
                    color: model.selected ? "#3a6fb0" : (index % 2 === 0 ? "#2b2b2b" : "#242424")

                    // Click-to-select. Declared first (and thus below the
                    // row's interactive controls in stacking order) so the
                    // checkbox/delete button still receive their own clicks.
                    MouseArea {
                        anchors.fill: parent
                        onClicked: sceneController.selectObject(model.id)
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 6

                        CheckBox {
                            checked: model.visible
                            onToggled: sceneController.setVisible(model.id, checked)
                        }
                        Label {
                            text: model.name
                            color: "white"
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        Button {
                            text: "\u2715"
                            implicitWidth: 28
                            onClicked: sceneController.deleteObject(model.id)
                        }
                    }
                }
            }
        }

        // ---------------- Right panel: VTK viewport ----------------
        MyVtkItem {
            id: vtkViewport
            objectName: "vtkViewport" // looked up via findChild<MyVTKItem*> in main.cpp
            Layout.fillWidth: true
            Layout.fillHeight: true
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
}
