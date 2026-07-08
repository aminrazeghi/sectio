# VTK + Qt Quick MWE

Minimum working project demonstrating the split discussed:

- **SceneObjectMeta** — plain-data description of one scene object (GUI thread, no VTK).
- **SceneModel** — `QAbstractListModel` holding `QVector<SceneObjectMeta>`. This is the
  single source of truth. Backs the QML `ListView` directly.
- **SceneObjectFactoryRegistry** — type-name -> builder map (`sphere`, `cube`, `cone`, `stl`).
  Add new object kinds here, nowhere else.
- **VTKSceneData** — render-thread-only `vtkObject` holding the `vtkRenderer` and the
  `id -> {actor, outlineActor}` map. This *is* the `vtkUserData` returned by `initializeVTK()`.
- **MyVTKItem** — the `QQuickVTKItem` subclass. Owns all threading-contract logic:
  rebuild-from-metadata in `initializeVTK()`, actor mutation only inside `dispatch_async()`
  lambdas, and picking (`event()` override -> `vtkPropPicker` -> queued signal back to GUI thread).
- **SceneController** — GUI-thread facade exposed to QML (`sceneController.createPrimitive(...)`
  etc.). Always writes `SceneModel` first, then `dispatch_async()`s the matching VTK mutation.
- **main.qml** — `ListView` bound to `sceneModel`, plus a `MyVtkItem` viewport. Selecting a row
  calls `sceneController.selectObject(id)`; clicking an actor in 3D calls back through
  `MyVTKItem::objectPicked` -> `SceneController::onObjectPicked` -> updates `sceneModel` again.

## Data flow cheat sheet

```
QML button/list click
        │
        ▼
SceneController (GUI thread, Q_INVOKABLE)
        │  1. mutate SceneModel synchronously (truth updated, QML updates instantly)
        │  2. dispatch_async(lambda)             ─────────────┐
        ▼                                                     │ queued, runs just
   scheduleRender()                                           │ before next VTK render
                                                                ▼
                                                   MyVTKItem's render thread
                                                   mutates VTKSceneData
                                                   (actors/renderer/etc.)

3D viewport click (picking)
        │
        ▼
MyVTKItem::event() (GUI thread, captures screen pos)
        │  dispatch_async(lambda)  ───────────────────────────┐
        ▼                                                     ▼
   (returns immediately)                         render thread: vtkPropPicker->Pick(...)
                                                   QMetaObject::invokeMethod(this, ..., Queued)
                                                                │
                                                                ▼
                                                   back on GUI thread: emit objectPicked(id)
                                                                │
                                                                ▼
                                                   SceneController::onObjectPicked(id)
                                                   -> SceneModel::setSelected(id)
                                                   -> dispatch_async to sync outline actors
```

## Why `initializeVTK()` rebuilds everything from `SceneModel`

Qt Quick's scene graph can destroy and recreate the underlying node (and therefore the
`vtkUserData`/actors) at any time -- window changes, item reparenting, etc. If actor state
lived only in VTK-land, that would silently wipe your scene. Because `SceneModel` is the
truth and `initializeVTK()` always replays it, a node recreation is invisible to the user.

## Adding a new object type

1. Add a factory in `SceneObjectFactory.cpp`:
   ```cpp
   registry.registerFactory("cylinder", [](const QVariantMap& p) -> vtkSmartPointer<vtkPolyDataAlgorithm> {
       auto source = vtkSmartPointer<vtkCylinderSource>::New();
       source->SetRadius(p.value("radius", 0.5).toDouble());
       source->SetHeight(p.value("height", 1.0).toDouble());
       return source;
   });
   ```
2. Call it from QML/SceneController: `sceneController.createPrimitive("cylinder", {radius: 0.3})`.

No changes needed to `MyVTKItem`, `SceneModel`, or the picking logic -- that's the point of
the registry.

## Adding a new *kind* of interaction (e.g. transform gizmo, box-select, multi-select)

- If it only changes *appearance* per-object (color, wireframe, opacity): add a field to
  `SceneObjectMeta`, a role to `SceneModel`, and mirror it inside `addActorForMeta()` /
  a small `dispatch_async` setter in `SceneController` -- same pattern as `visible`/`selected`.
- If it needs a *new persistent VTK object* not tied to one actor (e.g. a light, a widget,
  a measurement tool): add it as a member of `VTKSceneData`, initialize it in
  `initializeVTK()`, and mutate it via its own `dispatch_async` calls from a new
  `SceneController` method.
- If it needs to read back geometry (bounds, picked point coordinates, volume, etc.): compute
  it inside the `dispatch_async` lambda (render thread has full VTK access) and send it back
  with `QMetaObject::invokeMethod(this, [...]{ emit ... }, Qt::QueuedConnection)`, exactly like
  the picking code does. Never return a value directly out of a `dispatch_async` lambda.

## Build

Requires Qt 6 (Quick, QuickControls2) and VTK built with `GUISupportQtQuick` (VTK >= 9.2,
built against Qt6). On Arch/CachyOS both are typically available via the `vtk` and `qt6-*`
packages, or build VTK from source with `-DVTK_GROUP_ENABLE_Qt=YES`.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/VtkQtQuickMWE
```

This project was written against the documented `QQuickVTKItem` API but has not been
compiled here (no Qt/VTK toolchain in this environment) -- expect to fix minor include/
CMake target-name issues for your exact VTK/Qt package layout.
