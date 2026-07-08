#include "SceneObjectFactory.h"

#include <vtkSphereSource.h>
#include <vtkCubeSource.h>
#include <vtkConeSource.h>
#include <vtkSTLReader.h>
#include "OcctStepSource.h"

SceneObjectFactoryRegistry& SceneObjectFactoryRegistry::instance()
{
    static SceneObjectFactoryRegistry registry;
    return registry;
}

void SceneObjectFactoryRegistry::registerFactory(const QString& type, SceneObjectSourceBuilder builder)
{
    m_builders.insert(type, std::move(builder));
}

vtkSmartPointer<vtkPolyDataAlgorithm> SceneObjectFactoryRegistry::build(
    const QString& type, const QVariantMap& params) const
{
    auto it = m_builders.constFind(type);
    if (it == m_builders.constEnd())
        return nullptr;
    return (*it)(params);
}

QStringList SceneObjectFactoryRegistry::registeredTypes() const
{
    return m_builders.keys();
}

void registerBuiltinFactories()
{
    auto& registry = SceneObjectFactoryRegistry::instance();

    registry.registerFactory("sphere", [](const QVariantMap& params) -> vtkSmartPointer<vtkPolyDataAlgorithm> {
        auto source = vtkSmartPointer<vtkSphereSource>::New();
        source->SetRadius(params.value("radius", 0.5).toDouble());
        source->SetThetaResolution(32);
        source->SetPhiResolution(32);
        source->SetCenter(params.value("cx", 0.0).toDouble(),
                           params.value("cy", 0.0).toDouble(),
                           params.value("cz", 0.0).toDouble());
        return source;
    });

    registry.registerFactory("cube", [](const QVariantMap& params) -> vtkSmartPointer<vtkPolyDataAlgorithm> {
        auto source = vtkSmartPointer<vtkCubeSource>::New();
        source->SetXLength(params.value("sx", 1.0).toDouble());
        source->SetYLength(params.value("sy", 1.0).toDouble());
        source->SetZLength(params.value("sz", 1.0).toDouble());
        source->SetCenter(params.value("cx", 0.0).toDouble(),
                           params.value("cy", 0.0).toDouble(),
                           params.value("cz", 0.0).toDouble());
        return source;
    });

    registry.registerFactory("cone", [](const QVariantMap& params) -> vtkSmartPointer<vtkPolyDataAlgorithm> {
        auto source = vtkSmartPointer<vtkConeSource>::New();
        source->SetRadius(params.value("radius", 0.5).toDouble());
        source->SetHeight(params.value("height", 1.0).toDouble());
        source->SetResolution(32);
        source->SetCenter(params.value("cx", 0.0).toDouble(),
                           params.value("cy", 0.0).toDouble(),
                           params.value("cz", 0.0).toDouble());
        return source;
    });

    // Real-world hook: this is the pattern you'd follow for OpenFOAM case
    // geometry, imported meshes, etc. -- one factory per file/object type.
    registry.registerFactory("stl", [](const QVariantMap& params) -> vtkSmartPointer<vtkPolyDataAlgorithm> {
        auto reader = vtkSmartPointer<vtkSTLReader>::New();
        reader->SetFileName(params.value("path").toString().toStdString().c_str());
        return reader;
    });

    // STEP is a B-rep CAD format, not a mesh -- OcctStepSource reads it via
    // OpenCASCADE and tessellates it into a triangle mesh internally.
    registry.registerFactory("step", [](const QVariantMap& params) -> vtkSmartPointer<vtkPolyDataAlgorithm> {
        auto source = vtkSmartPointer<OcctStepSource>::New();
        source->SetFileName(params.value("path").toString());
        return source;
    });
}
