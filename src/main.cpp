#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
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

    // Organization/application name give QtCore's Settings type (used by
    // the dark/light mode toggle in main.qml) a stable place to persist to.
    QGuiApplication::setOrganizationName("Sectio");
    QGuiApplication::setApplicationName("Sectio");

    // The Material style is what lets main.qml switch the whole UI between
    // dark/light via a single Material.theme binding, instead of hardcoding
    // colors per-control. Must be set before the QML engine loads any QML.
    QQuickStyle::setStyle("Material");

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

    // Supports opening a file from the desktop (double-click / "Open
    // With", via the .desktop file's "Exec=Sectio %f") or a plain
    // command-line argument. importFile() already strips a file:// prefix
    // if the file manager passes a URI instead of a bare path.
    const QStringList args = QCoreApplication::arguments();
    if (args.size() > 1)
        sceneController.importFile(args.at(1));

    return app.exec();
}
