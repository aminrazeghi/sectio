#include "MyVTKItem.h"
#include "SceneObjectFactory.h"

#include <vtkRenderWindow.h>
#include <vtkRenderer.h>
#include <vtkPolyDataMapper.h>
#include <vtkActor.h>
#include <vtkProperty.h>
#include <vtkOutlineFilter.h>
#include <vtkPropPicker.h>
#include <vtkNew.h>
#include <vtkTrivialProducer.h>
#include <vtkPolyData.h>
#include <vtkAlgorithmOutput.h>

#include <QMouseEvent>
#include <QMetaObject>

#include <cmath>

void MyVTKItem::addActorForMeta(VTKSceneData* data, const SceneObjectMeta& meta)
{
    if (!data || !data->renderer)
        return;

    auto source = SceneObjectFactoryRegistry::instance().build(meta.type, meta.params);
    if (!source)
        return; // unknown type -- in a real app, log/report this to the user

    addActorForOutput(data, meta, source->GetOutputPort());
}

void MyVTKItem::addActorForPolyData(VTKSceneData* data, const SceneObjectMeta& meta, vtkPolyData* polyData)
{
    if (!data || !data->renderer || !polyData)
        return;

    // Wraps the already-parsed polyData as a pipeline source, so the rest of
    // the (clip filter -> mapper -> actor) chain doesn't need to care that
    // no vtkAlgorithm actually produced it.
    vtkNew<vtkTrivialProducer> producer;
    producer->SetOutput(polyData);

    addActorForOutput(data, meta, producer->GetOutputPort());
}

void MyVTKItem::addActorForOutput(VTKSceneData* data, const SceneObjectMeta& meta, vtkAlgorithmOutput* output)
{
    // Always routed through the clip filter -- with an empty
    // sectionPlaneCollection (the default) it passes geometry through
    // unchanged, so this has no effect until section view is enabled.
    vtkNew<vtkClipClosedSurface> clipFilter;
    clipFilter->SetClippingPlanes(data->sectionPlaneCollection);
    clipFilter->SetInputConnection(output);
    clipFilter->SetScalarModeToNone();

    vtkNew<vtkPolyDataMapper> mapper;
    mapper->SetInputConnection(clipFilter->GetOutputPort());

    vtkNew<vtkActor> actor;
    actor->SetMapper(mapper);
    actor->GetProperty()->SetColor(meta.color[0], meta.color[1], meta.color[2]);
    actor->SetVisibility(meta.visible);
    data->renderer->AddActor(actor);

    // Selection highlight: an outline actor, hidden unless selected.
    vtkNew<vtkOutlineFilter> outline;
    outline->SetInputConnection(output);
    vtkNew<vtkPolyDataMapper> outlineMapper;
    outlineMapper->SetInputConnection(outline->GetOutputPort());
    vtkNew<vtkActor> outlineActor;
    outlineActor->SetMapper(outlineMapper);
    outlineActor->GetProperty()->SetColor(1.0, 0.65, 0.0);
    outlineActor->GetProperty()->SetLineWidth(2.0);
    outlineActor->SetVisibility(meta.selected);
    data->renderer->AddActor(outlineActor);

    ActorEntry entry;
    entry.actor = actor;
    entry.outlineActor = outlineActor;
    entry.clipFilter = clipFilter;
    data->actors.insert(meta.id, entry);
}

void MyVTKItem::updateSectionPlane(VTKSceneData* data, int axis, double distance, double rotationDeg, bool enabled)
{
    if (!data || !data->sectionPlane || !data->sectionPlaneCollection)
        return;

    const double rad = rotationDeg * M_PI / 180.0;
    const double c = std::cos(rad);
    const double s = std::sin(rad);

    // Base normal is the chosen axis; rotationDeg tilts it around the next
    // axis in cyclic order (X->Z, Y->X, Z->Y) -- an arbitrary but consistent
    // single degree of freedom, enough to angle the cut without a full gizmo.
    double normal[3] = { 0.0, 0.0, 0.0 };
    switch (axis)
    {
        case 0: normal[0] = c;  normal[1] = s;  normal[2] = 0.0; break; // X, rotated around Z
        case 1: normal[0] = 0.0; normal[1] = c;  normal[2] = s;  break; // Y, rotated around X
        default: normal[0] = s; normal[1] = 0.0; normal[2] = c;  break; // Z, rotated around Y
    }

    data->sectionPlane->SetNormal(normal);
    data->sectionPlane->SetOrigin(normal[0] * distance, normal[1] * distance, normal[2] * distance);

    const bool alreadyEnabled = data->sectionPlaneCollection->GetNumberOfItems() > 0;
    if (enabled && !alreadyEnabled)
        data->sectionPlaneCollection->AddItem(data->sectionPlane);
    else if (!enabled && alreadyEnabled)
        data->sectionPlaneCollection->RemoveAllItems();
}

void MyVTKItem::setSceneOpacity(VTKSceneData* data, double opacity)
{
    if (!data)
        return;

    for (auto it = data->actors.begin(); it != data->actors.end(); ++it)
        if (it->actor)
            it->actor->GetProperty()->SetOpacity(opacity);
}

void MyVTKItem::applyBackground(vtkRenderer* renderer, bool dark)
{
    if (!renderer)
        return;

    // vtkRenderer::SetBackground takes normalized [0,1] components, not
    // [0,255] -- these are chosen to roughly match the Material style's
    // dark/light surface colors used elsewhere in the UI.
    if (dark)
        renderer->SetBackground(0.16, 0.16, 0.18);
    else
        renderer->SetBackground(0.88, 0.88, 0.90);
}

void MyVTKItem::setDarkMode(bool dark)
{
    m_darkMode = dark;
    emit darkModeChanged();

    // Updates the already-running renderer in place. If the render node
    // hasn't been created yet, initializeVTK() will pick up m_darkMode
    // (just set above) when it eventually runs, so this dispatch is purely
    // for the live-toggle case.
    dispatch_async([dark](vtkRenderWindow*, vtkUserData userData) {
        auto* data = static_cast<VTKSceneData*>(userData.Get());
        if (data)
            MyVTKItem::applyBackground(data->renderer, dark);
    });
    scheduleRender();
}

QQuickVTKItem::vtkUserData MyVTKItem::initializeVTK(vtkRenderWindow* renderWindow)
{
    // NOTE: this can run more than once over the item's lifetime -- the
    // QML scene graph is free to destroy and recreate the underlying
    // node. That's exactly why we rebuild from m_sceneModel instead of
    // trying to preserve any previous VTKSceneData.
    auto data = vtkSmartPointer<VTKSceneData>::New();

    data->sectionPlane = vtkSmartPointer<vtkPlane>::New();
    data->sectionPlaneCollection = vtkSmartPointer<vtkPlaneCollection>::New();

    data->renderer = vtkSmartPointer<vtkRenderer>::New();
    applyBackground(data->renderer, m_darkMode);
    // Without these, the renderer doesn't clear the previous frame before
    // drawing the next one, leaving ghosting/smearing from earlier frames.
    data->renderer->SetBackgroundAlpha(1.0);
    // data->renderer->SetErase(1);
    renderWindow->AddRenderer(data->renderer);

    if (m_sceneModel)
    {
        for (const SceneObjectMeta& meta : m_sceneModel->allObjects())
            addActorForMeta(data, meta);
    }

    data->renderer->ResetCamera();
    return data;
}

void MyVTKItem::destroyingVTK(vtkRenderWindow* renderWindow, vtkUserData userData)
{
    Q_UNUSED(renderWindow);
    Q_UNUSED(userData);
    // Nothing manual to do: vtkSmartPointer members of VTKSceneData
    // release the renderer/actors/mappers automatically. If you add
    // non-VTK resources (file handles, GPU buffers you own directly,
    // etc.) to VTKSceneData, release them here explicitly.
}

bool MyVTKItem::event(QEvent* ev)
{
    if (ev->type() == QEvent::MouseButtonPress)
    {
        auto* me = static_cast<QMouseEvent*>(ev);
        if (me->button() == Qt::LeftButton)
        {
            const QPointF pos = me->position();

            dispatch_async([this, pos](vtkRenderWindow* renderWindow, vtkUserData userData) {
                auto* data = static_cast<VTKSceneData*>(userData.Get());
                if (!data || !data->renderer)
                    return;

                const int viewportHeight = renderWindow->GetSize()[1];

                vtkNew<vtkPropPicker> picker;
                // VTK's Y axis is flipped relative to Qt's.
                picker->Pick(pos.x(), viewportHeight - pos.y(), 0, data->renderer);

                const QUuid pickedId = data->idForActor(picker->GetActor());

                // Hop back to the GUI thread. QMetaObject::invokeMethod's
                // functor overload posts an event to the thread that
                // `this` (a QQuickItem, hence GUI-thread-affine) lives
                // on -- it does NOT touch any VTK state itself, so it's
                // safe to call from here.
                QMetaObject::invokeMethod(this, [this, pickedId]() {
                    emit objectPicked(pickedId);
                }, Qt::QueuedConnection);
            });
        }
    }

    // IMPORTANT: still forward to the base class so camera rotate/pan/
    // zoom interaction keeps working -- we're augmenting event handling,
    // not replacing it.
    return QQuickVTKItem::event(ev);
}
