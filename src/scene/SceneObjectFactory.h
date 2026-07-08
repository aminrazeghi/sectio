#pragma once

#include <QString>
#include <QStringList>
#include <QHash>
#include <QVariantMap>
#include <functional>
#include <vtkSmartPointer.h>
#include <vtkPolyDataAlgorithm.h>

// A factory only ever builds a *source* (a vtkPolyDataAlgorithm pipeline
// head, e.g. vtkSphereSource or vtkSTLReader). MyVTKItem::addActorForMeta()
// wraps whatever this returns in a mapper + actor. This split keeps the
// registry unaware of rendering concerns.
using SceneObjectSourceBuilder = std::function<vtkSmartPointer<vtkPolyDataAlgorithm>(const QVariantMap& params)>;

// Analogous to your SolverRegistry: type-name -> factory. Extend this
// (not MyVTKItem) whenever you add a new kind of scene object.
class SceneObjectFactoryRegistry
{
public:
    static SceneObjectFactoryRegistry& instance();

    void registerFactory(const QString& type, SceneObjectSourceBuilder builder);
    vtkSmartPointer<vtkPolyDataAlgorithm> build(const QString& type, const QVariantMap& params) const;
    QStringList registeredTypes() const;

private:
    QHash<QString, SceneObjectSourceBuilder> m_builders;
};

// Registers the built-in primitive + STL import factories. Call once at
// startup (see main.cpp). Add your own registerFactory() calls here (or
// in a similar function) as you add object types.
void registerBuiltinFactories();
