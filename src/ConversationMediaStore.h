#pragma once
#include <QStringList>
#include <QVariant>

// Only directories explicitly registered by our import/generation code may be
// reclaimed. Old/foreign projects without this ownership record are retained.
namespace ConversationMediaStore {
QString canonical(QString path);
bool mark(const QString &directory);
QString ownedDirectory(QString path);
bool pin(const QVariant &project);
QStringList references(const QVariant &value);
QStringList clean(const QStringList &directories,const QStringList &allowedRoots,const QVariant &liveProjects);
}
