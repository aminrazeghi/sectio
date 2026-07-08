#pragma once

#include <QQuickVTKItem.h>
#include <QUuid>
#include "VTKSceneData.h"
#include "SceneModel.h"
#include "SceneObjectMeta.h"

// The QQuickVTKItem subclass. This is the *only* class that knows about
// the QQuickVTKItem threading contract. Everything else (SceneModel,
// SceneController, QML) is oblivious to it.
//
// Rules enforced by this class (do not weaken these when extending it):
//   1. initializeVTK() rebuilds the whole scene from SceneModel, because
//      it may be called again at any time with a fresh vtkUserData.
//   2. No VTK object is ever touched outside initializeVTK(),
//      destroyingVTK(), or a dispatch_async() lambda.
//   3. Anything the render thread needs to tell the GUI thread (e.g. a
//      pick result) goes back via a queued call, never a direct pointer.
class MyVTKItem : public QQuickVTKItem
{
    Q_OBJECT
public:
    using QQuickVTKItem::QQuickVTKItem;

    // Set once from main.cpp before the item is shown. Read only inside
    // initializeVTK(), which runs on the render thread while the GUI
    // thread is blocked -- so this cross-thread read is safe (see the
    // class docs on initializeVTK for why).
    void setSceneModel(SceneModel* model) { m_sceneModel = model; }

    vtkUserData initializeVTK(vtkRenderWindow* renderWindow) override;
    void destroyingVTK(vtkRenderWindow* renderWindow, vtkUserData userData) override;

    // Shared by initializeVTK() (full rebuild) and SceneController's
    // dispatch_async lambdas (incremental add). Must only be called from
    // the render thread.
    static void addActorForMeta(VTKSceneData* data, const SceneObjectMeta& meta);

    // Recomputes data->sectionPlane's origin/normal from (axis, distance,
    // rotationDeg) and adds/removes it from data->sectionPlaneCollection per
    // `enabled`. Because every actor's clipFilter shares that one collection,
    // this single call updates every cross-section in the scene at once.
    // axis: 0=X, 1=Y, 2=Z. rotationDeg tilts the plane's normal around the
    // next axis in cyclic order (X->around Z, Y->around X, Z->around Y).
    // distance offsets the plane along its own (rotated) normal from the
    // world origin. Must only be called from the render thread.
    static void updateSectionPlane(VTKSceneData* data, int axis, double distance, double rotationDeg, bool enabled);

signals:
    // Emitted on the GUI thread once a pick result has safely crossed
    // back over from the render thread. A null QUuid means "nothing hit".
    void objectPicked(const QUuid& id);

protected:
    bool event(QEvent* ev) override;

private:
    SceneModel* m_sceneModel = nullptr;
};
