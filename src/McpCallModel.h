#pragma once
#include <QAbstractListModel>
#include <QDateTime>
#include <QVariantMap>

class McpCallModel : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
  explicit McpCallModel(QObject *parent = nullptr)
      : QAbstractListModel(parent) {}
  int rowCount(const QModelIndex &parent = {}) const override {
    return parent.isValid() ? 0 : m_rows.size();
  }
  int count() const { return m_rows.size(); }
  QVariant data(const QModelIndex &index, int role) const override {
    return index.isValid() && index.row() < m_rows.size() &&
                   role == Qt::UserRole
               ? m_rows[index.row()]
               : QVariant();
  }
  QHash<int, QByteArray> roleNames() const override {
    return {{Qt::UserRole, "entry"}};
  }
  QString append(QVariantMap row);
  void update(QString id, QVariantMap fields);
  Q_INVOKABLE QVariantMap get(QString id) const;
  Q_INVOKABLE void clear();
signals:
  void countChanged();
  void entryChanged(QString id);

private:
  QList<QVariantMap> m_rows;
};
