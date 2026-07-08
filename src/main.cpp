#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>

#include "MyVTKItem.h"
#include "SceneModel.h"
#include "SceneController.h"
#include "SceneObjectFactory.h"

int main(int argc, char** argv)
{
    // Must be called before the QGuiApplication is constructed -- it
    // configures the surface format so VTK and Qt Quick can share the
    // same graphics context/RHI.
    QQuickVTKItem::setGraphicsApi();

    QGuiApplication app(argc, argv);

    registerBuiltinFactories();

    qmlRegisterType<MyVTKItem>("SceneApp", 1, 0, "MyVtkItem");

    SceneModel sceneModel;
    SceneController sceneController(&sceneModel);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("sceneModel", &sceneModel);
    engine.rootContext()->setContextProperty("sceneController", &sceneController);

    engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    // Wire the C++ backend to the QML-instantiated viewport. objectName
    // is set on the MyVtkItem instance in main.qml.
    QObject* root = engine.rootObjects().constFirst();
    if (auto* vtkItem = root->findChild<MyVTKItem*>(QStringLiteral("vtkViewport")))
    {
        vtkItem->setSceneModel(&sceneModel);
        sceneController.setVtkItem(vtkItem);
    }

    return app.exec();
}
