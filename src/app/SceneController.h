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

private slots:
    void onObjectPicked(const QUuid& id);

private:
    void addObjectInternal(const SceneObjectMeta& meta);

    SceneModel* m_model;
    MyVTKItem* m_vtkItem = nullptr;
};
