#pragma once

#include <vtkObject.h>
#include <vtkSmartPointer.h>
#include <vtkActor.h>
#include <vtkRenderer.h>
#include <vtkClipClosedSurface.h>
#include <vtkPlane.h>
#include <vtkPlaneCollection.h>
#include <QUuid>
#include <QHash>

// One VTK actor plus its (initially hidden) selection outline actor.
struct ActorEntry
{
    vtkSmartPointer<vtkActor> actor;
    vtkSmartPointer<vtkActor> outlineActor;
    // Always in the pipeline between the source and the mapper (see
    // MyVTKItem::addActorForMeta). Clips/caps against sectionPlaneCollection;
    // with zero planes in that collection it passes data through unchanged.
    vtkSmartPointer<vtkClipClosedSurface> clipFilter;
};

// This is the vtkUserData returned by MyVTKItem::initializeVTK().
//
// IMPORTANT: this object and everything it holds lives entirely on the
// QML render thread. It is only ever touched from:
//   - MyVTKItem::initializeVTK()
//   - MyVTKItem::destroyingVTK()
//   - lambdas passed to QQuickVTKItem::dispatch_async()
// Never store a pointer to this (or to any vtkActor/vtkRenderer inside it)
// anywhere that the GUI thread could dereference it directly.
class VTKSceneData : public vtkObject
{
public:
    static VTKSceneData* New();
    vtkTypeMacro(VTKSceneData, vtkObject);

    vtkSmartPointer<vtkRenderer> renderer;
    QHash<QUuid, ActorEntry> actors;

    // Section view: one shared plane, one shared collection. Every actor's
    // clipFilter points at the same sectionPlaneCollection instance, so
    // mutating the plane (or adding/removing it from the collection) here
    // updates every actor's cross-section simultaneously -- no per-actor
    // loop needed. Empty collection == section view disabled.
    vtkSmartPointer<vtkPlane> sectionPlane;
    vtkSmartPointer<vtkPlaneCollection> sectionPlaneCollection;

    QUuid idForActor(vtkActor* actor) const
    {
        if (!actor)
            return {};
        for (auto it = actors.constBegin(); it != actors.constEnd(); ++it)
            if (it.value().actor.GetPointer() == actor)
                return it.key();
        return {};
    }

protected:
    VTKSceneData() = default;
    ~VTKSceneData() override = default;

private:
    VTKSceneData(const VTKSceneData&) = delete;
    void operator=(const VTKSceneData&) = delete;
};
