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

#include <QMouseEvent>
#include <QMetaObject>

void MyVTKItem::addActorForMeta(VTKSceneData* data, const SceneObjectMeta& meta)
{
    if (!data || !data->renderer)
        return;

    auto source = SceneObjectFactoryRegistry::instance().build(meta.type, meta.params);
    if (!source)
        return; // unknown type -- in a real app, log/report this to the user

    vtkNew<vtkPolyDataMapper> mapper;
    mapper->SetInputConnection(source->GetOutputPort());

    vtkNew<vtkActor> actor;
    actor->SetMapper(mapper);
    actor->GetProperty()->SetColor(meta.color[0], meta.color[1], meta.color[2]);
    actor->SetVisibility(meta.visible);
    data->renderer->AddActor(actor);

    // Selection highlight: an outline actor, hidden unless selected.
    vtkNew<vtkOutlineFilter> outline;
    outline->SetInputConnection(source->GetOutputPort());
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
    data->actors.insert(meta.id, entry);
}

QQuickVTKItem::vtkUserData MyVTKItem::initializeVTK(vtkRenderWindow* renderWindow)
{
    // NOTE: this can run more than once over the item's lifetime -- the
    // QML scene graph is free to destroy and recreate the underlying
    // node. That's exactly why we rebuild from m_sceneModel instead of
    // trying to preserve any previous VTKSceneData.
    auto data = vtkSmartPointer<VTKSceneData>::New();

    data->renderer = vtkSmartPointer<vtkRenderer>::New();
    data->renderer->SetBackground(98, 93, 90);
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
