#pragma once

#include <QAbstractListModel>
#include <QVector>
#include "SceneObjectMeta.h"

// GUI-thread-only model backing the QML ListView. This is the "truth"
// of the scene. MyVTKItem::initializeVTK() replays this list to rebuild
// the VTK actors whenever the QML scene graph recreates the render node.
class SceneModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles
    {
        IdRole = Qt::UserRole + 1, // exposed to QML as a QString (QUuid without braces)
        NameRole,
        TypeRole,
        VisibleRole,
        SelectedRole
    };

    explicit SceneModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Mutators: GUI thread only. Call these from SceneController, never
    // from a dispatch_async() lambda.
    void addObject(const SceneObjectMeta& meta);
    void removeObject(const QUuid& id);
    void setVisible(const QUuid& id, bool visible);
    void setSelected(const QUuid& id, bool selected); // single-select: clears any previous selection

    QVector<SceneObjectMeta> allObjects() const { return m_objects; }
    const SceneObjectMeta* find(const QUuid& id) const;

private:
    int indexOf(const QUuid& id) const;

    QVector<SceneObjectMeta> m_objects;
};
