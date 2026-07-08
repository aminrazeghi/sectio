cmake_minimum_required(VERSION 3.16)
project(StpViewer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Qt/VTK/OpenCASCADE in this environment are installed under ~/.local rather
# than a system prefix.
list(APPEND CMAKE_PREFIX_PATH "$ENV{HOME}/.local")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)

find_package(VTK REQUIRED COMPONENTS
    CommonCore
    CommonDataModel
    FiltersCore
    FiltersSources
    FiltersGeneral
    FiltersModeling
    IOGeometry
    RenderingCore
    RenderingOpenGL2
    GUISupportQtQuick
)

find_package(OpenCASCADE REQUIRED)

qt_add_executable(StpViewer
    src/main.cpp
    src/scene/SceneModel.h
    src/scene/SceneModel.cpp
    src/scene/SceneObjectMeta.h
    src/scene/SceneObjectFactory.h
    src/scene/SceneObjectFactory.cpp
    src/vtk/VTKSceneData.h
    src/vtk/VTKSceneData.cpp
    src/vtk/MyVTKItem.h
    src/vtk/MyVTKItem.cpp
    src/vtk/OcctStepSource.h
    src/vtk/OcctStepSource.cpp
    src/app/SceneController.h
    src/app/SceneController.cpp
)

# Flat include dirs so intra-project headers can use plain #include "Foo.h"
# regardless of which subfolder Foo.h/.cpp lives in.
target_include_directories(StpViewer PRIVATE
    src
    src/scene
    src/vtk
    src/app
    ${OpenCASCADE_INCLUDE_DIR}
)

qt_add_resources(StpViewer "qml"
    PREFIX "/"
    FILES qml/main.qml
)

target_link_libraries(StpViewer PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    ${VTK_LIBRARIES}
    ${OpenCASCADE_FoundationClasses_LIBRARIES}
    ${OpenCASCADE_ModelingData_LIBRARIES}
    ${OpenCASCADE_ModelingAlgorithms_LIBRARIES}
    ${OpenCASCADE_DataExchange_LIBRARIES}
)

# VTK's factory-based object creation (e.g. OpenGL render window/mapper
# implementations) needs to be wired up per-executable.
vtk_module_autoinit(
    TARGETS StpViewer
    MODULES ${VTK_LIBRARIES}
)
