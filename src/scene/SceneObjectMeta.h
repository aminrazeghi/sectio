#pragma once

#include <QString>
#include <QUuid>
#include <QVariantMap>
#include <array>

// The single source of truth for "what exists in the scene".
// Lives on the GUI thread only. Never holds a VTK pointer, because
// QQuickVTKItem can tear down and rebuild all VTK objects at any time
// (see MyVTKItem::initializeVTK). This struct is what survives that.
struct SceneObjectMeta
{
    QUuid id;
    QString name;
    QString type;                       // key into SceneObjectFactoryRegistry: "sphere", "cube", "cone", "stl"...
    QVariantMap params;                 // factory-specific: radius, size, file path, etc.
    bool visible = true;
    bool selected = false;
    std::array<double, 3> color{ 0.75, 0.75, 0.8 };
};
