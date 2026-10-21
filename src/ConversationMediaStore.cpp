#include "ConversationMediaStore.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUrl>
#include <QSet>

namespace {
constexpr auto marker="/.moss-conversation-owned";
constexpr auto retained="/.moss-conversation-retained";
bool record(const QString &path){QSaveFile file(path);return file.open(QIODevice::WriteOnly)&&file.write("1\n")==2&&file.commit();}
bool inside(const QString &file,const QString &directory){return file==directory||file.startsWith(directory+"/");}
bool owned(const QString &directory){QFile file(directory+marker);return file.open(QIODevice::ReadOnly)&&file.readAll()=="1\n";}
void collect(const QVariant &value,QStringList &result){
    if(value.metaType().id()==QMetaType::QVariantMap){const auto map=value.toMap();for(auto it=map.begin();it!=map.end();++it)collect(it.value(),result);}
    else if(value.metaType().id()==QMetaType::QVariantList||value.metaType().id()==QMetaType::QStringList){for(const auto &item:value.toList())collect(item,result);}
    else if(value.metaType().id()==QMetaType::QString){const auto path=ConversationMediaStore::canonical(value.toString());if(!path.isEmpty())result<<path;}
}
}
QString ConversationMediaStore::canonical(QString path){
    if(path.startsWith("file:"))path=QUrl(path).toLocalFile();
    if(!QDir::isAbsolutePath(path))return {};
    return QFileInfo(path).canonicalFilePath();
}
bool ConversationMediaStore::mark(const QString &directory){
    const QFileInfo info(directory);return info.isDir()&&!info.isSymLink()&&record(info.canonicalFilePath()+marker);
}
QString ConversationMediaStore::ownedDirectory(QString path){
    path=canonical(path);if(path.isEmpty())return {};
    auto directory=QFileInfo(path).isDir()?path:QFileInfo(path).absolutePath();
    while(directory!="/"&&!directory.isEmpty()){
        if(owned(directory))return directory;directory=QFileInfo(directory).absolutePath();
    }
    return {};
}
QStringList ConversationMediaStore::references(const QVariant &value){QStringList paths;collect(value,paths);paths.removeDuplicates();return paths;}
bool ConversationMediaStore::pin(const QVariant &project){
    for(const auto &path:references(project)){
        auto directory=QFileInfo(path).isDir()?path:QFileInfo(path).absolutePath();
        // Files can be nested inside an owned inference job directory.
        while(directory!="/"&&!directory.isEmpty()){
            if(owned(directory)){if(!record(directory+retained))return false;break;}
            directory=QFileInfo(directory).absolutePath();
        }
    }
    return true;
}
QStringList ConversationMediaStore::clean(const QStringList &directories,const QStringList &allowedRoots,const QVariant &liveProjects){
    const auto refs=references(liveProjects);QStringList failures;QSet<QString> visited;
    for(const auto &path:directories){
        const QFileInfo info(path);const auto directory=info.canonicalFilePath();
        if(directory.isEmpty()||info.isSymLink()||visited.contains(directory))continue;visited.insert(directory);
        bool allowed=false;
        for(const auto &root:allowedRoots){
            const auto base=canonical(root);if(base.isEmpty())continue;
            // Never accept the root itself or arbitrary deep/nested directories.
            const auto relative=QDir(base).relativeFilePath(directory);
            allowed|=(!relative.contains('/')&&relative!=".."&&relative!=".")||
                (relative.startsWith("conversation-assets/")&&relative.count('/')==1);
        }
        if(!allowed||!owned(directory)||QFileInfo::exists(directory+retained))continue;
        bool used=false;for(const auto &ref:refs)if(inside(ref,directory)){used=true;break;}
        if(!used&&!QDir(directory).removeRecursively())failures<<directory;
    }
    return failures;
}
