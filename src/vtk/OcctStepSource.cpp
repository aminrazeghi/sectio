#include "OcctStepSource.h"

#include <vtkObjectFactory.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkPolyData.h>
#include <vtkPoints.h>
#include <vtkCellArray.h>
#include <vtkPolyDataNormals.h>
#include <vtkNew.h>

#include <STEPControl_Reader.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopLoc_Location.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <Poly_Triangle.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>

vtkStandardNewMacro(OcctStepSource);

OcctStepSource::OcctStepSource()
{
    // This is a source: it produces geometry from a file, not from an
    // upstream VTK pipeline, so it takes no input connections.
    this->SetNumberOfInputPorts(0);
    this->SetNumberOfOutputPorts(1);
}

void OcctStepSource::SetFileName(const QString& fileName)
{
    if (m_fileName == fileName)
        return;
    m_fileName = fileName;
    this->Modified();
}

int OcctStepSource::RequestData(vtkInformation* /*request*/,
                                 vtkInformationVector** /*inputVector*/,
                                 vtkInformationVector* outputVector)
{
    vtkInformation* outInfo = outputVector->GetInformationObject(0);
    vtkPolyData* output = vtkPolyData::SafeDownCast(outInfo->Get(vtkDataObject::DATA_OBJECT()));

    if (m_fileName.isEmpty())
    {
        vtkErrorMacro("OcctStepSource: no file name set");
        return 0;
    }

    STEPControl_Reader reader;
    const IFSelect_ReturnStatus status = reader.ReadFile(m_fileName.toLocal8Bit().constData());
    if (status != IFSelect_RetDone)
    {
        vtkErrorMacro("Failed to read STEP file: " << m_fileName.toStdString());
        return 0;
    }

    reader.TransferRoots();
    const TopoDS_Shape shape = reader.OneShape();
    if (shape.IsNull())
    {
        vtkErrorMacro("STEP file contains no usable shape: " << m_fileName.toStdString());
        return 0;
    }

    // Tessellate the B-rep into triangles. This attaches a Poly_Triangulation
    // to each face, which we then read back out below.
    BRepMesh_IncrementalMesh mesher(shape, LinearDeflection, Standard_False, AngularDeflection, Standard_True);

    vtkNew<vtkPoints> points;
    vtkNew<vtkCellArray> polys;

    for (TopExp_Explorer faceExp(shape, TopAbs_FACE); faceExp.More(); faceExp.Next())
    {
        const TopoDS_Face& face = TopoDS::Face(faceExp.Current());

        TopLoc_Location loc;
        const Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
        if (tri.IsNull())
            continue; // face failed to mesh (degenerate/tiny) -- skip rather than abort the whole import

        const gp_Trsf& trsf = loc.Transformation();
        const vtkIdType pointOffset = points->GetNumberOfPoints();

        for (Standard_Integer i = 1; i <= tri->NbNodes(); ++i)
        {
            gp_Pnt p = tri->Node(i);
            p.Transform(trsf); // node coords are local to the face's placement
            points->InsertNextPoint(p.X(), p.Y(), p.Z());
        }

        // A reversed face means the triangulation's winding is backwards
        // relative to the shape's outward normal -- flip it so every face
        // in the output has consistent, outward-facing winding.
        const bool reversed = (face.Orientation() == TopAbs_REVERSED);

        for (Standard_Integer i = 1; i <= tri->NbTriangles(); ++i)
        {
            Standard_Integer n1, n2, n3;
            tri->Triangle(i).Get(n1, n2, n3);

            const vtkIdType ids[3] = {
                pointOffset + (n1 - 1),
                pointOffset + (reversed ? n3 - 1 : n2 - 1),
                pointOffset + (reversed ? n2 - 1 : n3 - 1),
            };
            polys->InsertNextCell(3, ids);
        }
    }

    vtkNew<vtkPolyData> raw;
    raw->SetPoints(points);
    raw->SetPolys(polys);

    // Faces are tessellated independently, so shared edges get duplicated
    // points with no normal continuity. vtkPolyDataNormals both computes
    // normals and (via consistency-checking) smooths shading across them.
    vtkNew<vtkPolyDataNormals> normalsFilter;
    normalsFilter->SetInputData(raw);
    normalsFilter->ConsistencyOn();
    normalsFilter->SplittingOn();
    normalsFilter->SetFeatureAngle(60.0);
    normalsFilter->Update();

    output->ShallowCopy(normalsFilter->GetOutput());
    return 1;
}
