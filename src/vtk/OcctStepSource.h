#pragma once

#include <vtkPolyDataAlgorithm.h>
#include <QString>

// A vtkPolyDataAlgorithm *source* (no input ports, one polydata output) that
// reads a STEP file via OpenCASCADE and tessellates the B-rep into a
// triangle mesh. Slots into SceneObjectFactoryRegistry exactly like
// vtkSTLReader does for "stl" -- see SceneObjectFactory.cpp's "step" entry.
class OcctStepSource : public vtkPolyDataAlgorithm
{
public:
    static OcctStepSource* New();
    vtkTypeMacro(OcctStepSource, vtkPolyDataAlgorithm);

    void SetFileName(const QString& fileName);

    // Tessellation quality (BRepMesh_IncrementalMesh deflection parameters).
    // Smaller = finer mesh, more triangles. Defaults are a reasonable
    // middle ground for typical mechanical-part-sized STEP files.
    vtkSetMacro(LinearDeflection, double);
    vtkGetMacro(LinearDeflection, double);
    vtkSetMacro(AngularDeflection, double);
    vtkGetMacro(AngularDeflection, double);

protected:
    OcctStepSource();
    ~OcctStepSource() override = default;

    int RequestData(vtkInformation* request,
                     vtkInformationVector** inputVector,
                     vtkInformationVector* outputVector) override;

private:
    OcctStepSource(const OcctStepSource&) = delete;
    void operator=(const OcctStepSource&) = delete;

    QString m_fileName;
    double LinearDeflection = 0.3;
    double AngularDeflection = 0.3;
};
