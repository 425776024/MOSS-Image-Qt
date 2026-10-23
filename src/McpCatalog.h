#pragma once
#include <QJsonArray>
#include <QJsonObject>

// Single source for discovery, validation, UI catalog and generated reference.
namespace McpCatalog {
QJsonArray tools();
QJsonObject tool(const QString &name);
QString validate(const QJsonValue &value, const QJsonObject &schema,
                 QString path = "arguments");
QString reference();
} // namespace McpCatalog
