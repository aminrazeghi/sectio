#include "SceneController.h"
#include "MyVTKItem.h"

#include <qobject.h>
#include <vtkRenderer.h>
#include <vtkActor.h>
#include <vtkProperty.h>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QUrl>

SceneController::SceneController(SceneModel* model, QObject* parent)
    : QObject(parent), m_model(model)
{
}

void SceneController::setVtkItem(MyVTKItem* item)
{
    m_vtkItem = item;
    if (m_vtkItem)
        connect(m_vtkItem, &MyVTKItem::objectPicked, this, &SceneController::onObjectPicked);
}

void SceneController::addObjectInternal(const SceneObjectMeta& meta)
{
    m_model->addObject(meta); // truth updated on the GUI thread immediately

    if (!m_vtkItem)
        return;

    m_vtkItem->dispatch_async([meta, opacity = m_opacity](vtkRenderWindow*, QQuickVTKItem::vtkUserData userData) {
        auto* data = static_cast<VTKSceneData*>(userData.Get());
        MyVTKItem::addActorForMeta(data, meta);
        // Newly added actors default to fully opaque -- match whatever the
        // opacity slider is currently set to, so imports stay consistent
        // with objects already in the scene.
        auto it = data->actors.find(meta.id);
        if (it != data->actors.end() && it->actor)
            it->actor->GetProperty()->SetOpacity(opacity);
        // The camera was only framed once, at startup, against an empty
        // scene. Without this, newly added actors can end up outside the
        // current view frustum and simply never appear.
        if (data && data->renderer)
            data->renderer->ResetCamera();
    });
    m_vtkItem->scheduleRender();
}

void SceneController::createPrimitive(const QString& type, const QVariantMap& params)
{
    SceneObjectMeta meta;
    meta.id = QUuid::createUuid();
    meta.type = type;
    meta.params = params;
    meta.name = QStringLiteral("%1_%2").arg(type, meta.id.toString(QUuid::WithoutBraces).left(8));

    // Spread new primitives out a bit so they don't all land on top of
    // each other -- purely cosmetic for this MWE.
    if (!meta.params.contains("cx"))
    {
        // QRandomGenerator::bounded() has no (double, double) overload --
        // only integer ones -- hence generateDouble() (which returns a
        // value in [0, 1)) scaled/shifted into [-2, 2) by hand.
        auto* rng = QRandomGenerator::global();
        meta.params["cx"] = rng->generateDouble() * 4.0 - 2.0;
        meta.params["cy"] = rng->generateDouble() * 4.0 - 2.0;
        meta.params["cz"] = rng->generateDouble() * 4.0 - 2.0;
    }

    addObjectInternal(meta);
}

void SceneController::importFile(const QString& filePath)
{
    QString path = filePath;
    if (filePath.startsWith("file://"))
        path = QUrl(filePath).toLocalFile();

    const QString suffix = QFileInfo(path).suffix().toLower();
    QString type;
    if (suffix == "stl")
        type = "stl";
    else if (suffix == "step" || suffix == "stp")
        type = "step";
    else
        return; // unsupported extension -- in a real app, surface this to the user

    SceneObjectMeta meta;
    meta.id = QUuid::createUuid();
    meta.type = type;
    meta.name = QFileInfo(path).fileName();
    meta.params = { { "path", path } };

    addObjectInternal(meta);
}

void SceneController::deleteObject(const QString& idString)
{
    const QUuid id(idString);
    m_model->removeObject(id);

    if (!m_vtkItem)
        return;

    m_vtkItem->dispatch_async([id](vtkRenderWindow*, QQuickVTKItem::vtkUserData userData) {
        auto* data = static_cast<VTKSceneData*>(userData.Get());
        if (!data)
            return;
        auto it = data->actors.find(id);
        if (it == data->actors.end())
            return;
        data->renderer->RemoveActor(it->actor);
        if (it->outlineActor)
            data->renderer->RemoveActor(it->outlineActor);
        data->actors.erase(it);
    });
    m_vtkItem->scheduleRender();
}

void SceneController::selectObject(const QString& idString)
{
    const QUuid id(idString);
    m_model->setSelected(id, true);

    if (!m_vtkItem)
        return;

    m_vtkItem->dispatch_async([id](vtkRenderWindow*, QQuickVTKItem::vtkUserData userData) {
        auto* data = static_cast<VTKSceneData*>(userData.Get());
        if (!data)
            return;
        for (auto it = data->actors.begin(); it != data->actors.end(); ++it)
        {
            if (it->outlineActor)
                it->outlineActor->SetVisibility(it.key() == id);
        }
    });
    m_vtkItem->scheduleRender();
}

void SceneController::setVisible(const QString& idString, bool visible)
{
    const QUuid id(idString);
    m_model->setVisible(id, visible);

    if (!m_vtkItem)
        return;

    m_vtkItem->dispatch_async([id, visible](vtkRenderWindow*, QQuickVTKItem::vtkUserData userData) {
        auto* data = static_cast<VTKSceneData*>(userData.Get());
        if (!data)
            return;
        auto it = data->actors.find(id);
        if (it != data->actors.end())
            it->actor->SetVisibility(visible);
    });
    m_vtkItem->scheduleRender();
}

void SceneController::pushSectionPlane()
{
    if (!m_vtkItem)
        return;

    const int axis = m_sectionAxis;
    const double distance = m_sectionDistance;
    const double rotationDeg = m_sectionRotationDeg;
    const bool enabled = m_sectionEnabled;

    m_vtkItem->dispatch_async([axis, distance, rotationDeg, enabled](vtkRenderWindow*, QQuickVTKItem::vtkUserData userData) {
        auto* data = static_cast<VTKSceneData*>(userData.Get());
        MyVTKItem::updateSectionPlane(data, axis, distance, rotationDeg, enabled);
    });
    m_vtkItem->scheduleRender();
}

void SceneController::setSectionEnabled(bool enabled)
{
    m_sectionEnabled = enabled;
    pushSectionPlane();
}

void SceneController::setSectionAxis(int axis)
{
    m_sectionAxis = axis;
    pushSectionPlane();
}

void SceneController::setSectionDistance(double distance)
{
    m_sectionDistance = distance;
    pushSectionPlane();
}

void SceneController::setSectionRotation(double rotationDeg)
{
    m_sectionRotationDeg = rotationDeg;
    pushSectionPlane();
}

void SceneController::setOpacity(double opacity)
{
    m_opacity = opacity;

    if (!m_vtkItem)
        return;

    m_vtkItem->dispatch_async([opacity](vtkRenderWindow*, QQuickVTKItem::vtkUserData userData) {
        auto* data = static_cast<VTKSceneData*>(userData.Get());
        MyVTKItem::setSceneOpacity(data, opacity);
    });
    m_vtkItem->scheduleRender();
}

void SceneController::onObjectPicked(const QUuid& id)
{
    // A null id means the click missed every actor -- clear selection.
    m_model->setSelected(id, !id.isNull());

    if (!m_vtkItem)
        return;

    // Always resync outlines, even on a miss: a null id here correctly
    // hides every outline actor since none of them equal it.
    m_vtkItem->dispatch_async([id](vtkRenderWindow*, QQuickVTKItem::vtkUserData userData) {
        auto* data = static_cast<VTKSceneData*>(userData.Get());
        if (!data)
            return;
        for (auto it = data->actors.begin(); it != data->actors.end(); ++it)
            if (it->outlineActor)
                it->outlineActor->SetVisibility(it.key() == id);
    });
    m_vtkItem->scheduleRender();
}
