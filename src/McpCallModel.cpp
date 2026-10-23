#include "McpCallModel.h"
#include <QUuid>
QString McpCallModel::append(QVariantMap row) {
  row["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
  row["startedAt"] = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
  row["startedMs"] = QDateTime::currentMSecsSinceEpoch();
  row["state"] = "running";
  beginInsertRows({}, 0, 0);
  m_rows.prepend(row);
  endInsertRows();
  if (m_rows.size() > 200) {
    int oldest = m_rows.size() - 1;
    // Polling traffic must not evict the original call while its job runs.
    for (int i = m_rows.size() - 1; i > 0; --i) {
      const auto state = m_rows[i]["state"].toString();
      if (state != "queued" && state != "running" && state != "cancelling") {
        oldest = i;
        break;
      }
    }
    beginRemoveRows({}, oldest, oldest);
    m_rows.removeAt(oldest);
    endRemoveRows();
  }
  emit countChanged();
  return row["id"].toString();
}
void McpCallModel::update(QString id, QVariantMap fields) {
  for (int i = 0; i < m_rows.size(); ++i)
    if (m_rows[i]["id"] == id) {
      for (auto it = fields.begin(); it != fields.end(); ++it)
        m_rows[i][it.key()] = it.value();
      emit dataChanged(index(i), index(i));
      emit entryChanged(id);
      return;
    }
}
QVariantMap McpCallModel::get(QString id) const {
  for (const auto &row : m_rows)
    if (row["id"] == id)
      return row;
  return {};
}
void McpCallModel::clear() {
  beginResetModel();
  m_rows.clear();
  endResetModel();
  emit countChanged();
  emit entryChanged({});
}
