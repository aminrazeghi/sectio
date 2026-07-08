#pragma once

#include <QObject>
#include <QString>
#include <QUuid>
#include <QVariantMap>
#include <vtkSmartPointer.h>
#include "SceneModel.h"

class MyVTKItem;
class vtkPolyData;

// GUI-thread facade exposed to QML. This is the ONLY class QML talks to
// for scene mutation. It always updates SceneModel (truth) first, then
// dispatch_async()'s the matching VTK-side mutation.
class SceneController : public QObject
{
    Q_OBJECT
    // Drives the QML progress bar during importFile(). See importFile()'s
    // comment for why the actual file read/parse happens off both the GUI
    // and VTK render threads.
    Q_PROPERTY(bool importing READ isImporting NOTIFY importingChanged)
    Q_PROPERTY(double importProgress READ importProgress NOTIFY importProgressChanged)
public:
    explicit SceneController(SceneModel* model, QObject* parent = nullptr);

    // Wired up from main.cpp once the QML item exists.
    void setVtkItem(MyVTKItem* item);

    bool isImporting() const { return m_importing; }
    double importProgress() const { return m_importProgress; }

    Q_INVOKABLE void createPrimitive(const QString& type, const QVariantMap& params = {});
    // Picks "stl" vs "step" by file extension and imports through the
    // matching SceneObjectFactoryRegistry entry. The actual file read and
    // (for STEP) tessellation run on a Qt thread-pool thread -- see the
    // .cpp -- so large (100MB+) files don't freeze the UI. Progress is
    // reported via the importing/importProgress properties.
    Q_INVOKABLE void importFile(const QString& filePath);
    Q_INVOKABLE void deleteObject(const QString& idString);
    Q_INVOKABLE void selectObject(const QString& idString);
    Q_INVOKABLE void setVisible(const QString& idString, bool visible);

    // Section view: a single shared cutting plane applied to every actor
    // in the scene. axis: 0=X, 1=Y, 2=Z.
    Q_INVOKABLE void setSectionEnabled(bool enabled);
    Q_INVOKABLE void setSectionAxis(int axis);
    Q_INVOKABLE void setSectionDistance(double distance);
    Q_INVOKABLE void setSectionRotation(double rotationDeg);

    // Applies to every actor in the scene (0.0-1.0). Newly imported/created
    // objects also pick up the current value -- see addObjectInternal().
    Q_INVOKABLE void setOpacity(double opacity);

signals:
    void importingChanged();
    void importProgressChanged();

private slots:
    void onObjectPicked(const QUuid& id);

private:
    void addObjectInternal(const SceneObjectMeta& meta);
    void pushSectionPlane(); // re-sends the full section state after any single field changes

    // Background-thread completion callbacks for importFile(). Invoked via
    // QMetaObject::invokeMethod(this, ..., Qt::QueuedConnection) from the
    // worker thread, so these themselves always run on the GUI thread.
    void onImportProgress(double progress);
    void onImportFinished(const SceneObjectMeta& meta, vtkSmartPointer<vtkPolyData> polyData);
    void onImportFailed(const QString& reason);

    // Adds meta to SceneModel and builds its actor from an already-parsed
    // polyData (as opposed to addObjectInternal(), which builds/reads the
    // source lazily on the render thread -- fine for cheap primitives, not
    // for a 100MB+ file).
    void addObjectFromPolyData(const SceneObjectMeta& meta, vtkSmartPointer<vtkPolyData> polyData);

    SceneModel* m_model;
    MyVTKItem* m_vtkItem = nullptr;

    bool m_sectionEnabled = false;
    int m_sectionAxis = 0;
    double m_sectionDistance = 0.0;
    double m_sectionRotationDeg = 0.0;
    double m_opacity = 1.0;

    bool m_importing = false;
    double m_importProgress = 0.0;
};
