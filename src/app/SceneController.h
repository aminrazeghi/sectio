#pragma once

#include <QObject>
#include <QString>
#include <QUuid>
#include <QVariantMap>
#include "SceneModel.h"

class MyVTKItem;

// GUI-thread facade exposed to QML. This is the ONLY class QML talks to
// for scene mutation. It always updates SceneModel (truth) first, then
// dispatch_async()'s the matching VTK-side mutation.
class SceneController : public QObject
{
    Q_OBJECT
public:
    explicit SceneController(SceneModel* model, QObject* parent = nullptr);

    // Wired up from main.cpp once the QML item exists.
    void setVtkItem(MyVTKItem* item);

    Q_INVOKABLE void createPrimitive(const QString& type, const QVariantMap& params = {});
    // Picks "stl" vs "step" by file extension and imports through the
    // matching SceneObjectFactoryRegistry entry.
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

private slots:
    void onObjectPicked(const QUuid& id);

private:
    void addObjectInternal(const SceneObjectMeta& meta);
    void pushSectionPlane(); // re-sends the full section state after any single field changes

    SceneModel* m_model;
    MyVTKItem* m_vtkItem = nullptr;

    bool m_sectionEnabled = false;
    int m_sectionAxis = 0;
    double m_sectionDistance = 0.0;
    double m_sectionRotationDeg = 0.0;
};
