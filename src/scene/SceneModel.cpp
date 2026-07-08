#include "SceneModel.h"

SceneModel::SceneModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int SceneModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return m_objects.size();
}

QVariant SceneModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_objects.size())
        return {};

    const SceneObjectMeta& meta = m_objects.at(index.row());
    switch (role)
    {
        case IdRole:       return meta.id.toString(QUuid::WithoutBraces);
        case NameRole:     return meta.name;
        case TypeRole:     return meta.type;
        case VisibleRole:  return meta.visible;
        case SelectedRole: return meta.selected;
        default:           return {};
    }
}

QHash<int, QByteArray> SceneModel::roleNames() const
{
    return {
        { IdRole, "id" },
        { NameRole, "name" },
        { TypeRole, "type" },
        { VisibleRole, "visible" },
        { SelectedRole, "selected" },
    };
}

int SceneModel::indexOf(const QUuid& id) const
{
    for (int i = 0; i < m_objects.size(); ++i)
        if (m_objects.at(i).id == id)
            return i;
    return -1;
}

void SceneModel::addObject(const SceneObjectMeta& meta)
{
    beginInsertRows(QModelIndex(), m_objects.size(), m_objects.size());
    m_objects.append(meta);
    endInsertRows();
}

void SceneModel::removeObject(const QUuid& id)
{
    int row = indexOf(id);
    if (row < 0)
        return;

    beginRemoveRows(QModelIndex(), row, row);
    m_objects.remove(row);
    endRemoveRows();
}

void SceneModel::setVisible(const QUuid& id, bool visible)
{
    int row = indexOf(id);
    if (row < 0)
        return;

    m_objects[row].visible = visible;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, { VisibleRole });
}

void SceneModel::setSelected(const QUuid& id, bool selected)
{
    // Single-select model: clear any other selected row first.
    for (int i = 0; i < m_objects.size(); ++i)
    {
        bool shouldBeSelected = (m_objects.at(i).id == id) && selected;
        if (m_objects[i].selected != shouldBeSelected)
        {
            m_objects[i].selected = shouldBeSelected;
            const QModelIndex idx = index(i);
            emit dataChanged(idx, idx, { SelectedRole });
        }
    }
}

const SceneObjectMeta* SceneModel::find(const QUuid& id) const
{
    int row = indexOf(id);
    return row < 0 ? nullptr : &m_objects.at(row);
}
