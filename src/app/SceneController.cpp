#include "SceneController.h"
#include "MyVTKItem.h"
#include "SceneObjectFactory.h"

#include <qobject.h>
#include <vtkRenderer.h>
#include <vtkActor.h>
#include <vtkProperty.h>
#include <vtkAlgorithm.h>
#include <vtkCallbackCommand.h>
#include <vtkCommand.h>
#include <vtkPolyData.h>
#include <vtkNew.h>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

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

    m_importing = true;
    m_importProgress = 0.0;
    emit importingChanged();
    emit importProgressChanged();

    // The actual file read (STL) or read+tessellate (STEP, via OpenCASCADE)
    // is pure CPU work with no GPU/OpenGL dependency, so it's safe to run on
    // an arbitrary Qt thread-pool thread -- unlike mutating actors/renderer,
    // which must stay on the render thread (see dispatch_async elsewhere in
    // this file). Running it there instead of on the GUI thread or the VTK
    // render thread is what keeps a 100MB+ file from freezing the app.
    //
    // `this` is captured raw (not QPointer) because SceneController is a
    // long-lived, effectively app-scoped object (constructed once in main())
    // -- same assumption the rest of this codebase makes (e.g. MyVTKItem's
    // own dispatch_async lambdas capture `this` the same way).
    SceneController* self = this;
    // Fire-and-forget: nothing needs to await/cancel this job, so the
    // returned QFuture is intentionally discarded.
    (void)QtConcurrent::run([self, meta]() {
        auto source = SceneObjectFactoryRegistry::instance().build(meta.type, meta.params);
        if (!source)
        {
            QMetaObject::invokeMethod(self, [self]() {
                self->onImportFailed(QStringLiteral("Unknown import type"));
            }, Qt::QueuedConnection);
            return;
        }

        // Forwards vtkAlgorithm's built-in progress reporting (vtkSTLReader
        // reports real read progress; OcctStepSource reports coarse,
        // stage-based progress -- see its RequestData) back to the GUI
        // thread as it happens, from this same background thread.
        vtkNew<vtkCallbackCommand> progressCallback;
        progressCallback->SetClientData(self);
        progressCallback->SetCallback([](vtkObject* caller, unsigned long, void* clientData, void*) {
            auto* controller = static_cast<SceneController*>(clientData);
            const double progress = static_cast<vtkAlgorithm*>(caller)->GetProgress();
            QMetaObject::invokeMethod(controller, [controller, progress]() {
                controller->onImportProgress(progress);
            }, Qt::QueuedConnection);
        });
        source->AddObserver(vtkCommand::ProgressEvent, progressCallback);

        source->Update(); // the expensive part -- runs entirely on this background thread

        // vtkSmartPointer (refcounted, copyable), not vtkNew (move-only) --
        // this needs to be captured by value into the queued lambda below.
        vtkSmartPointer<vtkPolyData> result = vtkSmartPointer<vtkPolyData>::New();
        if (auto* output = vtkPolyData::SafeDownCast(source->GetOutputDataObject(0)))
            result->DeepCopy(output);

        QMetaObject::invokeMethod(self, [self, meta, result]() {
            self->onImportFinished(meta, result);
        }, Qt::QueuedConnection);
    });
}

void SceneController::onImportProgress(double progress)
{
    m_importProgress = progress;
    emit importProgressChanged();
}

void SceneController::onImportFailed(const QString& reason)
{
    Q_UNUSED(reason); // no error UI yet -- surfacing this is a natural next step
    m_importing = false;
    emit importingChanged();
}

void SceneController::onImportFinished(const SceneObjectMeta& meta, vtkSmartPointer<vtkPolyData> polyData)
{
    m_importing = false;
    m_importProgress = 1.0;
    emit importingChanged();
    emit importProgressChanged();

    addObjectFromPolyData(meta, polyData);
}

void SceneController::addObjectFromPolyData(const SceneObjectMeta& meta, vtkSmartPointer<vtkPolyData> polyData)
{
    m_model->addObject(meta); // truth updated on the GUI thread immediately

    if (!m_vtkItem)
        return;

    m_vtkItem->dispatch_async([meta, polyData, opacity = m_opacity](vtkRenderWindow*, QQuickVTKItem::vtkUserData userData) {
        auto* data = static_cast<VTKSceneData*>(userData.Get());
        MyVTKItem::addActorForPolyData(data, meta, polyData);
        auto it = data->actors.find(meta.id);
        if (it != data->actors.end() && it->actor)
            it->actor->GetProperty()->SetOpacity(opacity);
        if (data && data->renderer)
            data->renderer->ResetCamera();
    });
    m_vtkItem->scheduleRender();
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
