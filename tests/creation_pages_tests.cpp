#include "Studio.h"
#include "ConversationMediaStore.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QImage>
#include <QDataStream>

class CreationPagesTest : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_remoteFixture;
    static void json(const QString &path,const QJsonObject &value){
        QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(value).toJson());
    }
private slots:
    void publicBuildRejectsRemoteExecution() {
        qputenv("MOSS_IMAGE_DEVICE","remote");
        Studio studio;
        qunsetenv("MOSS_IMAGE_DEVICE");
        QVERIFY(!studio.remoteGpu()->available());
        QCOMPARE(studio.imageDevice(),QString("local"));
        studio.setImageDevice("remote");
        QCOMPARE(studio.imageDevice(),QString("local"));
        studio.remoteGpu()->connectServer();
        QVERIFY(!studio.remoteGpu()->busy());
        QVERIFY(!studio.remoteGpu()->ready());
    }
    void missingConversationModelsKeepDraftAndRequestDownload() {
        QTemporaryDir dir,models;qputenv("MOSS_OUTPUT_ROOT",dir.path().toUtf8());
        const auto old=qgetenv("MOSS_MODEL_ROOT");qputenv("MOSS_MODEL_ROOT",models.path().toUtf8());
        Studio studio;auto thread=studio.conversation();thread->setTask("image");thread->setDraft("保留这个创作要求");
        const auto messages=thread->messages().size();QSignalSpy requested(studio.models(),&ModelManager::downloadRequested);
        studio.sendConversation();QCOMPARE(requested.size(),1);QVERIFY(!studio.busy());QVERIFY(!thread->pending());
        QCOMPARE(thread->draft(),QString("保留这个创作要求"));QCOMPARE(thread->messages().size(),messages);
        QVERIFY(studio.models()->requested().contains("image"));QVERIFY(studio.models()->describeRequest().contains("MOSS"));
        // A private old manifest must never satisfy public Ditto readiness.
        QDir().mkpath(models.filePath("ditto"));json(models.filePath("ditto/manifest.json"),{{"private",true}});
        QVERIFY(!studio.dittoReady());
        if(old.isEmpty())qunsetenv("MOSS_MODEL_ROOT");else qputenv("MOSS_MODEL_ROOT",old);
    }
    void deletingImportedMessageCleansPrivateCopyAndPersists() {
        QTemporaryDir dir;qputenv("MOSS_OUTPUT_ROOT",dir.path().toUtf8());
        const auto original=dir.filePath("original.png");QImage image(32,32,QImage::Format_RGB32);image.fill(Qt::cyan);QVERIFY(image.save(original));
        QString copy,id;
        {
            Studio studio;auto thread=studio.conversation();id=thread->id();const auto asset=thread->importMedia(QUrl::fromLocalFile(original),"target");
            copy=QUrl(thread->asset(asset)["url"].toString()).toLocalFile();QVERIFY(QFileInfo::exists(copy));
            thread->chooseAudio("");thread->toggleSelected(asset);
            const auto message=thread->messages().first().toMap()["id"].toString();
            QVERIFY(studio.deleteConversationMessage(id,message,true));QVERIFY(thread->messages().isEmpty());QVERIFY(thread->assets().isEmpty());QVERIFY(thread->clips().isEmpty());QVERIFY(thread->target().isEmpty());
            QVERIFY(QFileInfo::exists(original));QVERIFY(!QFileInfo::exists(copy));studio.flushWorkspace();
        }
        Studio restored;QCOMPARE(restored.conversation()->id(),id);QVERIFY(restored.conversation()->messages().isEmpty());QVERIFY(restored.conversation()->assets().isEmpty());QVERIFY(QFileInfo::exists(original));
    }
    void deletionRespectsSavedProjectsOtherWorksAndRetainOption() {
        QTemporaryDir dir;qputenv("MOSS_OUTPUT_ROOT",dir.path().toUtf8());
        const auto original=dir.filePath("original.png");QImage image(32,32,QImage::Format_RGB32);image.fill(Qt::cyan);QVERIFY(image.save(original));
        Studio studio;auto first=studio.conversation();const auto asset=first->importMedia(QUrl::fromLocalFile(original),"target");
        const auto copy=QUrl(first->asset(asset)["url"].toString()).toLocalFile();const auto message=first->messages().first().toMap()["id"].toString();
        studio.flushWorkspace();studio.duplicatePage(0);auto second=studio.conversation();QVERIFY(first!=second);
        QVERIFY(studio.deleteConversationMessage(first->id(),message,true));QVERIFY(QFileInfo::exists(copy));
        QVERIFY(studio.deleteConversationMessage(second->id(),message,true));QVERIFY(!QFileInfo::exists(copy));
        const auto saved=second->importMedia(QUrl::fromLocalFile(original),"target");const auto savedCopy=QUrl(second->asset(saved)["url"].toString()).toLocalFile();
        studio.saveConversation(QUrl::fromLocalFile(dir.filePath("saved.moss.json")));
        QVERIFY(studio.deleteConversationMessage(second->id(),second->messages().last().toMap()["id"].toString(),true));QVERIFY(QFileInfo::exists(savedCopy));
        const auto retained=second->importMedia(QUrl::fromLocalFile(original),"target");const auto retainedCopy=QUrl(second->asset(retained)["url"].toString()).toLocalFile();
        QVERIFY(studio.deleteConversationMessage(second->id(),second->messages().last().toMap()["id"].toString(),false));QVERIFY(QFileInfo::exists(retainedCopy));
    }
    void deletedCardsKeepReferencedAssetsAndStablePairing() {
        QTemporaryDir dir;const auto original=dir.filePath("original.png");QImage image(32,32,QImage::Format_RGB32);image.fill(Qt::cyan);QVERIFY(image.save(original));
        ConversationDocument doc("test",dir.path(),{});const auto asset=doc.importMedia(QUrl::fromLocalFile(original),"target");
        const auto imported=doc.messages().first().toMap()["id"].toString();doc.setTask("edit");doc.setDraft("修改颜色");QVERIFY(doc.beginRequest());
        QVERIFY(!doc.removeMessage(imported));doc.finishRequest(false,{},"测试");
        const auto request=doc.messages()[1].toMap()["id"].toString();const auto response=doc.messages()[2].toMap()["id"].toString();
        QCOMPARE(doc.messages()[2].toMap()["replyTo"].toString(),request);
        QVERIFY(doc.removeMessage(imported));QVERIFY(!doc.asset(asset).isEmpty());
        QVERIFY(doc.removeMessage(request));QVERIFY(doc.asset(asset).isEmpty());QCOMPARE(doc.messages().first().toMap()["id"].toString(),response);
        doc.setTask("image");doc.setDraft("新图像");QVERIFY(doc.beginRequest());doc.finishRequest(false,{},"测试");
        ConversationDocument restored("test",dir.path(),doc.json());QCOMPARE(restored.messages().first().toMap()["replyTo"].toString(),request);
    }
    void audioAndPosterDependenciesSurviveSourceCardDeletion() {
        QTemporaryDir dir;const auto path=dir.filePath("asset.png");QImage image(32,32,QImage::Format_RGB32);image.fill(Qt::cyan);QVERIFY(image.save(path));
        ConversationDocument doc("test",dir.path(),{});const auto poster=doc.addAsset(path,"image");doc.addResults({poster},"import");const auto posterMessage=doc.messages().last().toMap()["id"].toString();
        const auto audio=doc.addAsset(path,"image");doc.addResults({audio},"import");const auto audioMessage=doc.messages().last().toMap()["id"].toString();
        const auto video=doc.addAsset(path,"image","result",QUrl::fromLocalFile(path).toString(),audio);doc.addResults({video},"avatar");
        QVERIFY(doc.removeMessage(posterMessage));QVERIFY(doc.removeMessage(audioMessage));QVERIFY(!doc.asset(audio).isEmpty());QVERIFY(!doc.asset(poster).isEmpty());
        QVERIFY(doc.removeMessage(doc.messages().last().toMap()["id"].toString()));QVERIFY(doc.assets().isEmpty());
    }
    void cleanupRefusesUnknownRootsSymlinksAndLiveCanvasFiles() {
        QTemporaryDir dir,outside;const auto owned=dir.filePath("job");QVERIFY(QDir().mkpath(owned));
        const auto file=owned+"/output.png";QFile f(file);QVERIFY(f.open(QIODevice::WriteOnly));f.write("fixture");f.close();
        QVERIFY(ConversationMediaStore::mark(owned));
        QVERIFY(ConversationMediaStore::clean({owned},{dir.path()},QVariantMap{{"canvasLayers",QVariantMap{{"source",file}}}}).isEmpty());QVERIFY(QFileInfo::exists(file));
        const auto other=outside.filePath("data");QVERIFY(QDir().mkpath(other));QVERIFY(ConversationMediaStore::mark(other));
        const auto link=dir.filePath("linked");QVERIFY(QFile::link(other,link));
        ConversationMediaStore::clean({dir.path(),other,link},{dir.path()},{});QVERIFY(QFileInfo::exists(other));QVERIFY(QFileInfo::exists(file));
        ConversationMediaStore::clean({owned},{dir.path()},{});QVERIFY(!QFileInfo::exists(owned));
    }
    void generatedDataCleanupKeepsRecoverableWorksAndOutsideSymlinkTargets() {
        QTemporaryDir dir,outside;qputenv("MOSS_OUTPUT_ROOT",dir.path().toUtf8());
        const auto job=dir.filePath("generated-job");QVERIFY(QDir().mkpath(job));
        const auto imagePath=job+"/image.png";QImage image(32,32,QImage::Format_RGB32);image.fill(Qt::cyan);QVERIFY(image.save(imagePath));
        const auto source=outside.filePath("original.png");QVERIFY(image.save(source));QVERIFY(QFile::link(source,job+"/linked-original.png"));
        QVERIFY(ConversationMediaStore::mark(job));
        Studio studio;auto first=studio.conversation();const auto result=first->addAsset(imagePath,"image");first->addResults({result},"image");
        QCOMPARE(first->asset(result)["temporaryDirectory"].toString(),QFileInfo(job).canonicalFilePath());
        studio.flushWorkspace();studio.duplicatePage(0);auto second=studio.conversation();const auto message=first->messages().last().toMap()["id"].toString();
        studio.deletePage(0);QVERIFY(studio.deleteConversationMessage(second->id(),message,true));QVERIFY(QFileInfo::exists(imagePath));
        studio.restoreDeletedPage();QCOMPARE(studio.conversation(),first);
        QVERIFY(studio.deleteConversationMessage(first->id(),message,true));QVERIFY(!QFileInfo::exists(job));QVERIFY(QFileInfo::exists(source));
    }
    void saveFailureDoesNotDeleteCardOrFile() {
        QTemporaryDir dir;qputenv("MOSS_OUTPUT_ROOT",dir.path().toUtf8());const auto source=dir.filePath("original.png");QImage image(16,16,QImage::Format_RGB32);image.fill(Qt::cyan);QVERIFY(image.save(source));
        Studio studio;auto thread=studio.conversation();const auto id=thread->importMedia(QUrl::fromLocalFile(source),"target");const auto copy=QUrl(thread->asset(id)["url"].toString()).toLocalFile();
        QVERIFY(QFile::remove(dir.filePath("creation-pages.json")));QVERIFY(QDir().mkpath(dir.filePath("creation-pages.json")));
        QVERIFY(!studio.deleteConversationMessage(thread->id(),thread->messages().first().toMap()["id"].toString(),true));QCOMPARE(thread->messages().size(),1);QVERIFY(QFileInfo::exists(copy));
        QVERIFY(QDir().rmdir(dir.filePath("creation-pages.json")));
    }
    void conversationsKeepIndependentDraftsSelectionAndStableObjects() {
        QTemporaryDir dir;qputenv("MOSS_OUTPUT_ROOT",dir.path().toUtf8());
        const auto source=dir.filePath("source.png");QImage image(48,48,QImage::Format_RGB32);image.fill(Qt::cyan);QVERIFY(image.save(source));
        QString firstId,secondId,storedImage;
        {
            Studio s;auto first=s.conversation();firstId=first->id();const auto enginePage=s.currentPage();
            const auto imported=first->importMedia(QUrl::fromLocalFile(source),"target");QVERIFY(!imported.isEmpty());storedImage=first->asset(imported)["url"].toString();
            first->setTask("image");first->setDraft("海报草稿");first->setTask("avatar");first->setDraft("您好，欢迎回来。");first->chooseTarget(imported);
            first->setTask("pack");first->toggleSelected(imported);const auto clip=first->clips().first().toMap();first->moveClip(clip["id"].toString(),2.5);
            s.newConversation();auto second=s.conversation();secondId=second->id();second->setDraft("第二件作品");
            QCOMPARE(s.currentPage(),enginePage); // Work browsing does no image/document restoration.
            QElapsedTimer timer;timer.start();
            for(int i=0;i<200;++i){s.openConversation(firstId);QCOMPARE(s.conversation(),first);s.openConversation(secondId);QCOMPARE(s.conversation(),second);}
            QVERIFY2(timer.elapsed()<1000,"Switching cached works must not perform media I/O");
            s.openConversation(firstId);QCOMPARE(first->clips().first().toMap()["start"].toDouble(),2.5);
            first->setTask("image");QCOMPARE(first->draft(),QString("海报草稿"));first->setTask("avatar");QCOMPARE(first->draft(),QString("您好，欢迎回来。"));
            QCOMPARE(first->target()["id"].toString(),imported);QFile::remove(source);QVERIFY(QFileInfo(QUrl(storedImage).toLocalFile()).isFile());
            s.flushWorkspace();
        }
        Studio restored;QCOMPARE(restored.conversation()->id(),firstId);QCOMPARE(restored.conversation()->draft(),QString("您好，欢迎回来。"));
        QCOMPARE(restored.conversation()->clips().first().toMap()["start"].toDouble(),2.5);
        restored.openConversation(secondId);QCOMPARE(restored.conversation()->draft(),QString("第二件作品"));
    }
    void conversationLayoutAndBrushSurviveWorkSwitchAndRestart() {
        QTemporaryDir dir;qputenv("MOSS_OUTPUT_ROOT",dir.path().toUtf8());
        const auto source=dir.filePath("source.png");QImage image(512,512,QImage::Format_RGB32);image.fill(Qt::cyan);QVERIFY(image.save(source));
        QString firstId,secondId;QImage firstMask,secondMask;QJsonObject layout;
        {
            Studio s;auto first=s.conversation();firstId=first->id();
            first->importMedia(QUrl::fromLocalFile(source),"target");first->setTask("edit");QVERIFY(s.prepareConversationEdit());
            s.editDocument()->beginStroke();s.editDocument()->paintLine({30,40},{180,90},32,false);s.editDocument()->endStroke();firstMask=s.editDocument()->mask();
            s.document()->loadTemplate("objects");s.document()->setCanvasSize(512,512);layout=s.document()->json();
            s.newConversation();auto second=s.conversation();secondId=second->id();
            second->importMedia(QUrl::fromLocalFile(source),"target");second->setTask("edit");QVERIFY(s.prepareConversationEdit());
            s.editDocument()->addRectangle(300,300,80,70);secondMask=s.editDocument()->mask();QVERIFY(firstMask!=secondMask);
            for(int i=0;i<3;++i){
                s.openConversation(firstId);QVERIFY(s.prepareConversationEdit());QCOMPARE(s.editDocument()->mask(),firstMask);QCOMPARE(s.document()->json(),layout);
                s.openConversation(secondId);QVERIFY(s.prepareConversationEdit());QCOMPARE(s.editDocument()->mask(),secondMask);QVERIFY(s.document()->regions().isEmpty());
            }
            s.flushWorkspace();
        }
        Studio restored;restored.openConversation(firstId);QVERIFY(restored.prepareConversationEdit());QCOMPARE(restored.editDocument()->mask(),firstMask);QCOMPARE(restored.document()->json(),layout);
        restored.openConversation(secondId);QVERIFY(restored.prepareConversationEdit());QCOMPARE(restored.editDocument()->mask(),secondMask);
    }
    void removingAnotherWorkKeepsTheViewedConversation() {
        QTemporaryDir dir;qputenv("MOSS_OUTPUT_ROOT",dir.path().toUtf8());Studio s;
        s.newConversation();s.newConversation();auto viewed=s.conversation();viewed->setDraft("保留当前对话");
        s.deletePage(1);QCOMPARE(s.conversation(),viewed);QCOMPARE(viewed->draft(),QString("保留当前对话"));
        s.deletePage(1);QVERIFY(s.conversation()!=viewed);
        s.restoreDeletedPage();QCOMPARE(s.conversation(),viewed);QCOMPARE(viewed->draft(),QString("保留当前对话"));
    }
    void interruptedConversationNeverPretendsToHaveGeneratedMedia() {
        QTemporaryDir dir;
        ConversationDocument doc("test",dir.path(),{});doc.setDraft("生成一张海报");QVERIFY(doc.beginRequest());QVERIFY(doc.pending());
        doc.setTask("sfx");doc.setDraft("风声");
        ConversationDocument restored("test",dir.path(),doc.json());QVERIFY(!restored.pending());
        QCOMPARE(restored.messages().last().toMap()["status"].toString(),QString("interrupted"));QVERIFY(restored.assets().isEmpty());
        doc.finishRequest(false,{},"引擎未就绪");QCOMPARE(doc.messages().last().toMap()["task"].toString(),QString("image"));
        QCOMPARE(doc.messages().last().toMap()["status"].toString(),QString("failed"));QCOMPARE(doc.messages().size(),2);
        const auto before=doc.messages().size();doc.setTask("pack");QVERIFY(!doc.beginRequest());QCOMPARE(doc.messages().size(),before);
    }
    void conversationProjectRoundTripAndLayoutResources() {
        QTemporaryDir dir;qputenv("MOSS_OUTPUT_ROOT",dir.path().toUtf8());Studio s;
        const auto count=s.creationPages().size();s.useLayoutTemplate("poster");QCOMPARE(s.creationPages().size(),count+1);
        const auto draft=s.conversation()->draft();QVERIFY(!draft.isEmpty());
        const auto path=dir.filePath("export.moss.json");s.saveConversation(QUrl::fromLocalFile(path));
        QFile f(path);QVERIFY(f.open(QIODevice::ReadOnly));auto json=QJsonDocument::fromJson(f.readAll()).object();QVERIFY(json["conversation"].isObject());QVERIFY(json["layout"].toBool());
        s.loadProject(QUrl::fromLocalFile(path));QCOMPARE(s.conversation()->draft(),draft);
        QCOMPARE(s.importLayoutTemplate(QUrl::fromLocalFile(path)),QString());QCOMPARE(s.layoutTemplates().size(),4);
        const auto custom=s.layoutTemplates().last().toMap()["id"].toString();s.removeLayoutTemplate(custom);QCOMPARE(s.layoutTemplates().size(),3);
    }
    void mouthAdaptationPersistsAndInvalidatesOnlyRenderedVideo() {
        QTemporaryDir dir;qputenv("MOSS_OUTPUT_ROOT",dir.path().toUtf8());
        const auto audio=dir.filePath("voice.wav"),video=dir.filePath("old.mp4"),path=dir.filePath("old.moss.json");
        for(const auto &file:{audio,video}){QFile f(file);QVERIFY(f.open(QIODevice::WriteOnly));f.write("fixture");}
        Document doc;auto project=doc.json();
        project["video"]=QUrl::fromLocalFile(video).toString();
        project["sound"]=QJsonObject{{"voiceSource","tts"},{"speechResult",audio}};
        json(path,project);
        {
            Studio s;s.loadProject(QUrl::fromLocalFile(path));
            QCOMPARE(s.sound()->settings()["mouthReference"].toString(),QString("auto"));
            QVERIFY(!s.pageVideoUrl().isEmpty());
            s.sound()->set("mouthReference","source");
            QVERIFY(s.pageVideoUrl().isEmpty());
            QCOMPARE(s.sound()->settings()["speechResult"].toString(),audio);
            QCOMPARE(s.timeline()->track("speech")["kind"].toString(),QString("audio"));
            s.sound()->set("mouthReference","invalid");
            QCOMPARE(s.sound()->settings()["mouthReference"].toString(),QString("source"));
            const auto page=s.currentPage();
            s.newPage();QCOMPARE(s.sound()->settings()["mouthReference"].toString(),QString("auto"));
            s.selectPage(page);QCOMPARE(s.sound()->settings()["mouthReference"].toString(),QString("source"));
            s.flushWorkspace();
        }
        Studio restored;
        QCOMPARE(restored.sound()->settings()["mouthReference"].toString(),QString("source"));
        QCOMPARE(restored.sound()->settings()["speechResult"].toString(),audio);
        SoundPlan malformed;malformed.restore({{"mouthReference","invalid"}});
        QCOMPARE(malformed.settings()["mouthReference"].toString(),QString("auto"));
    }
    void voiceOnlyGenerationKeepsTheSharedImageAndSkipsAvatar() {
        QTemporaryDir dir;qputenv("MOSS_OUTPUT_ROOT",dir.path().toUtf8());
        const auto source=dir.filePath("image.png"),audio=dir.filePath("cached.wav");
        QImage image(32,32,QImage::Format_RGB32);image.fill(Qt::cyan);QVERIFY(image.save(source));
        QFile wav(audio);QVERIFY(wav.open(QIODevice::WriteOnly));wav.write("cached speech fixture");wav.close();
        Studio s;s.importTrackImage("image",QUrl::fromLocalFile(source));
        const auto before=s.canvasImage();const auto imageUrl=s.canvasCompositeUrl();
        QVERIFY(imageUrl.startsWith("image://moss-canvas/"));
        s.sound()->set("voiceSource","tts");s.sound()->set("speechText","用户手动输入的文案");
        s.sound()->setResult("speechResult",audio);
        // Voice synthesis must not validate or run an unrelated SFX request.
        s.sound()->set("sfxEnabled",true);s.sound()->set("sfxPrompt","");
        QSignalSpy completed(&s,&Studio::jobFinished);s.generateTrack("voice");
        QVERIFY2(s.error().isEmpty(),qPrintable(s.error()));QCOMPARE(completed.size(),1);
        QVERIFY(completed.first().first().toBool());QVERIFY(!s.busy());QVERIFY(s.pageVideoUrl().isEmpty());
        QCOMPARE(s.canvasImage(),before);QCOMPARE(s.canvasCompositeUrl(),imageUrl);
        QCOMPARE(s.timeline()->track("speech")["kind"].toString(),QString("audio"));
    }
    void pageActivationPublishesACompleteSnapshot(){
        QTemporaryDir dir;qputenv("MOSS_OUTPUT_ROOT",dir.path().toUtf8());
        Studio s;s.document()->setPrompt("第一页");
        s.sound()->set("speechText","第一页配音");
        s.newPage();s.document()->setPrompt("第二页");
        s.sound()->set("speechText","第二页配音");
        s.timeline()->setRange("speech",2.,7.);
        QSignalSpy switching(&s,&Studio::pageSwitchingChanged);
        QSignalSpy activated(&s,&Studio::pageActivated);
        QStringList observed;
        connect(&s,&Studio::pageActivated,&s,[&]{
            QVERIFY(!s.switchingPage());
            observed.append(s.document()->prompt()+" / "+s.sound()->settings()["speechText"].toString());
        });
        s.selectPage(0);s.selectPage(1);
        QCOMPARE(switching.size(),4);QCOMPARE(activated.size(),2);
        QCOMPARE(observed,QStringList({"第一页 / 第一页配音","第二页 / 第二页配音"}));
        QCOMPARE(s.timeline()->track("speech")["start"].toDouble(),2.);
        QCOMPARE(s.timeline()->track("speech")["duration"].toDouble(),7.);
        // Duplicating or replacing a page can leave its numeric index unchanged.
        // Consumers still need the completed-page notification in those cases.
        s.deletePage(1);s.restoreDeletedPage();
        QCOMPARE(activated.size(),4);QCOMPARE(observed.last(),QString("第二页 / 第二页配音"));
        s.selectPage(1);QCOMPARE(activated.size(),4);
    }
    void canvasObjectsArePageLocalAndExportSquare(){
        QTemporaryDir dir;qputenv("MOSS_OUTPUT_ROOT",dir.path().toUtf8());
        const auto base=dir.filePath("base.png"),photo=dir.filePath("photo.png"),vector=dir.filePath("object.svg");
        QImage image(64,32,QImage::Format_RGB32);image.fill(Qt::blue);QVERIFY(image.save(base));
        image=QImage(16,16,QImage::Format_RGB32);image.fill(Qt::green);QVERIFY(image.save(photo));
        QFile svg(vector);QVERIFY(svg.open(QIODevice::WriteOnly));
        svg.write("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\"><rect width=\"100\" height=\"100\" fill=\"red\"/></svg>");svg.close();
        QJsonObject objects;QImage expected;
        const auto project=dir.filePath("canvas.moss.json");
        {
            Studio s;s.importTrackImage("image",QUrl::fromLocalFile(base));
            const auto fixedBase=s.pageVisualUrl();
            QVERIFY(s.layerDocument()->importFiles({QUrl::fromLocalFile(vector),QUrl::fromLocalFile(photo)}));
            auto layers=s.layerDocument();
            layers->updateLayer(0,"x",128);layers->updateLayer(0,"y",128);
            layers->updateLayer(0,"width",256);layers->updateLayer(0,"height",256);
            layers->updateLayer(1,"x",700);layers->updateLayer(1,"y",700);
            layers->updateLayer(1,"width",128);layers->updateLayer(1,"height",128);
            expected=s.canvasImage();QCOMPARE(expected.size(),QSize(1024,1024));
            QCOMPARE(expected.pixelColor(140,140),QColor(Qt::red));
            QCOMPARE(expected.pixelColor(500,500),QColor(Qt::blue));
            QCOMPARE(expected.pixelColor(720,720),QColor(Qt::green));
            QCOMPARE(expected.pixelColor(0,0).alpha(),0);
            QCOMPARE(s.pageVisualUrl(),fixedBase);objects=layers->json();
            QVERIFY(QFile::remove(vector));QVERIFY(QFile::remove(photo));
            s.exportCanvas(QUrl::fromLocalFile(dir.filePath("canvas.png")));
            QVERIFY2(s.error().isEmpty(),qPrintable(s.error()));
            QCOMPARE(QImage(dir.filePath("canvas.png")).convertToFormat(expected.format()),expected);
            s.saveProject(QUrl::fromLocalFile(project));
            s.duplicatePage(0);QCOMPARE(layers->json(),objects);QVERIFY(!layers->canUndo());
            layers->updateLayer(0,"x",400);
            s.selectPage(0);QCOMPARE(layers->json(),objects);QVERIFY(!layers->canUndo());
            s.newPage();QVERIFY(!layers->hasContent());QVERIFY(!s.hasCanvasContent());QVERIFY(!layers->canUndo());
            s.loadProject(QUrl::fromLocalFile(project));QCOMPARE(layers->json(),objects);
            QCOMPARE(s.canvasImage(),expected);s.selectPage(0);
        }
        Studio restored;QCOMPARE(restored.layerDocument()->json(),objects);
        QCOMPARE(restored.canvasImage(),expected);QVERIFY(!restored.layerDocument()->canUndo());
        restored.deletePage(0);restored.restoreDeletedPage();QCOMPARE(restored.canvasImage(),expected);
    }
    void legacyGlobalLayersMigrateOnceWithoutCrossPageUndo(){
        QTemporaryDir dir;qputenv("MOSS_OUTPUT_ROOT",dir.path().toUtf8());
        const auto source=dir.filePath("legacy.png"),legacy=dir.filePath("layers-session.mosslayers.json");
        QImage image(16,16,QImage::Format_RGB32);image.fill(Qt::magenta);QVERIFY(image.save(source));
        LayerDocument original;QVERIFY(original.importFiles({QUrl::fromLocalFile(source)}));
        QVERIFY(original.saveProject(QUrl::fromLocalFile(legacy)));
        QFile file(legacy);QVERIFY(file.open(QIODevice::ReadOnly));const auto bytes=file.readAll();file.close();
        {
            Studio s;QVERIFY(s.layerDocument()->hasContent());
            QCOMPARE(s.layerDocument()->json(),original.json());QVERIFY(!s.layerDocument()->canUndo());
            s.newPage();QVERIFY(!s.layerDocument()->hasContent());s.layerDocument()->undo();
            QVERIFY(!s.layerDocument()->hasContent());
        }
        {
            Studio restored;QVERIFY(!restored.layerDocument()->hasContent());
            restored.selectPage(0);QCOMPARE(restored.layerDocument()->json(),original.json());
        }
        QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),bytes);
    }
    void initTestCase(){
        // Isolate remote history too; real project archives must not seed test pages.
        const auto config=m_remoteFixture.path()+"/gpu-server.json";
        json(config,{{"appRuns",m_remoteFixture.path()+"/archive"}});
        qputenv("MOSS_GPU_CONFIG",config.toUtf8());
        qputenv("MOSS_IMAGE_DEVICE","local");
    }
    void changingRemoteArchiveKeepsHistoryAndPendingRecovery(){
        QTemporaryDir dir;
        const auto savedConfig=qgetenv("MOSS_GPU_CONFIG");
        const auto config=dir.path()+"/gpu.json",oldRoot=dir.path()+"/old",newRoot=dir.path()+"/new";
        json(config,{{"appRuns",oldRoot}});
        qputenv("MOSS_GPU_CONFIG",config.toUtf8());
        qputenv("MOSS_OUTPUT_ROOT",(dir.path()+"/local").toUtf8());
        const auto oldJob=oldRoot+"/20260101-image",newJob=newRoot+"/20260201-image";
        for(const auto &path:{oldJob,newJob}){
            QVERIFY(QDir().mkpath(path));QImage image(32,32,QImage::Format_RGB32);
            image.fill(Qt::green);QVERIFY(image.save(path+"/image.png"));
            Document document;json(path+"/project.json",document.json());
            json(path+"/job.json",{{"kind","image"},{"success",true},{"backend","CUDA · 远程 GPU"},
                {"executionDevice","remote"},{"output",path+"/image.png"}});
        }
        json(oldJob+"/remote-pending.json",{{"jobId","20260101-image"}});
        {
            RemoteImageClient remote;
            remote.chooseArchive(QUrl::fromLocalFile(newRoot));
            QCOMPARE(remote.archiveRoots().size(),2);
            QVERIFY(remote.archiveRoots().contains(oldRoot));
            QCOMPARE(remote.latestPending(),oldJob);QCOMPARE(remote.pendingCount(),1);
        }
        {
            Studio studio;QCOMPARE(studio.history().size(),2);
            studio.selectHistory(1);
            QCOMPARE(studio.creationImageUrl(),QUrl::fromLocalFile(oldJob+"/image.png").toString());
        }
        qputenv("MOSS_GPU_CONFIG",savedConfig);
    }
    void timelinesAreIndependentAcrossCopiesProjectsAndRestart(){
        QTemporaryDir dir;qputenv("MOSS_OUTPUT_ROOT",dir.path().toUtf8());
        const auto source=dir.path()+"/external.png",background=dir.path()+"/background.png";
        const auto project=dir.path()+"/timeline.moss.json";
        QImage image(80,64,QImage::Format_RGB32);image.fill(Qt::blue);QVERIFY(image.save(source));
        image.fill(Qt::green);QVERIFY(image.save(background));
        QJsonObject original;
        {
            Studio s;
            s.importTrackImage("image",QUrl::fromLocalFile(source));
            QCOMPARE(s.timeline()->track("image")["source"].toString(),s.pageVisualUrl());
            QCOMPARE(s.timeline()->tracks().size(),3);
            for(const auto &track:s.timeline()->tracks())QVERIFY(track.toMap()["key"]!="image");
            QVERIFY(!s.repainting());
            s.importTrackImage("background",QUrl::fromLocalFile(background));
            const auto storedBackground=QUrl(s.timeline()->track("background")["source"].toString()).toLocalFile();
            QVERIFY(storedBackground!=background);QVERIFY(QFile::exists(storedBackground));
            QVERIFY(QFile::remove(source));QVERIFY(QFile::remove(background));
            QVERIFY(QFile::exists(QUrl(s.pageVisualUrl()).toLocalFile()));
            s.timeline()->setRange("image",5.,12.);
            s.timeline()->setRange("speech",2.,7.);
            s.timeline()->setRange("sfx",8.,3.);
            s.timeline()->setRange("background",0.,20.);
            s.timeline()->setTransform("image",.1,.2,.7,.6);
            QCOMPARE(s.timeline()->track("image")["start"].toDouble(),0.);
            QCOMPARE(s.timeline()->track("image")["duration"].toDouble(),30.);
            QCOMPARE(s.timeline()->track("image")["x"].toDouble(),0.);
            QCOMPARE(s.timeline()->track("image")["width"].toDouble(),1.);
            s.timeline()->setTransform("speech",.5,.1,.4,.8);
            s.timeline()->setTransform("background",.1,.2,.7,.6);
            s.timeline()->setValue("background","color","#112233");
            s.timeline()->setValue("sfx","enabled",false);
            original=s.timeline()->snapshot();
            s.duplicatePage(0);
            QCOMPARE(s.timeline()->snapshot(),original);
            s.timeline()->setRange("speech",25.,35.);
            s.timeline()->setTransform("background",0.,0.,1.,1.);
            s.timeline()->setValue("background","color","#aabbcc");
            QVERIFY(s.timeline()->snapshot()!=original);
            s.selectPage(0);QCOMPARE(s.timeline()->snapshot(),original);
            s.deletePage(0);s.restoreDeletedPage();
            QCOMPARE(s.timeline()->snapshot(),original);
            s.saveProject(QUrl::fromLocalFile(project));QVERIFY(QFile::exists(project));
            s.newPage();QCOMPARE(s.timeline()->track("image")["duration"].toDouble(),30.);
            QVERIFY(s.timeline()->track("image")["source"].toString().isEmpty());
            s.loadProject(QUrl::fromLocalFile(project));
            QCOMPARE(s.timeline()->snapshot(),original);
        }
        Studio restored;QCOMPARE(restored.timeline()->snapshot(),original);
        QCOMPARE(restored.timeline()->playhead(),0.);
        QCOMPARE(restored.timeline()->selectedTrack(),QString("image"));
    }
    void legacyProjectMigratesCanvasAndNarration(){
        QTemporaryDir dir;qputenv("MOSS_OUTPUT_ROOT",dir.path().toUtf8());
        const auto imagePath=dir.path()+"/image.png",audioPath=dir.path()+"/narration.wav";
        const auto videoPath=dir.path()+"/portrait.mp4",projectPath=dir.path()+"/legacy.moss.json";
        QImage image(40,32,QImage::Format_RGB32);image.fill(Qt::yellow);QVERIFY(image.save(imagePath));
        for(const auto &path:{audioPath,videoPath}){
            QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write("track source fixture");
        }
        Document document;auto project=document.json();
        project["image"]=QUrl::fromLocalFile(imagePath).toString();
        project["visual"]=QUrl::fromLocalFile(imagePath).toString();
        project["audio"]=audioPath;project["video"]=QUrl::fromLocalFile(videoPath).toString();
        project["sound"]=QJsonObject{{"voiceSource","import"}};
        json(projectPath,project);
        Studio s;s.loadProject(QUrl::fromLocalFile(projectPath));
        QCOMPARE(s.timeline()->track("image")["source"].toString(),s.pageVisualUrl());
        QCOMPARE(s.timeline()->track("speech")["source"].toString(),s.pageVideoUrl());
        QCOMPARE(s.timeline()->track("speech")["kind"].toString(),QString("video"));
        QCOMPARE(s.timeline()->track("speech")["audioSource"].toString(),QUrl::fromLocalFile(audioPath).toString());
        QCOMPARE(s.timeline()->track("speech")["duration"].toDouble(),30.);
        QCOMPARE(s.timeline()->tracks().size(),3);
    }
    void legacyProjectCanvasTransformIsDiscardedAndSavedFixed(){
        QTemporaryDir dir;qputenv("MOSS_OUTPUT_ROOT",dir.path().toUtf8());
        const auto imagePath=dir.path()+"/legacy-canvas.png",projectPath=dir.path()+"/legacy-position.moss.json";
        const auto fixedProject=dir.path()+"/fixed-canvas.moss.json";
        QImage image(40,32,QImage::Format_RGB32);image.fill(Qt::yellow);QVERIFY(image.save(imagePath));
        Document document;auto project=document.json();
        const auto imageUrl=QUrl::fromLocalFile(imagePath).toString();
        project["image"]=imageUrl;project["visual"]=imageUrl;
        project["timeline"]=QJsonObject{{"version",1},{"tracks",QJsonArray{
            QJsonObject{{"key","image"},{"source",imageUrl},{"start",50.},{"duration",90.},
                        {"x",.2},{"y",.4},{"width",.3},{"height",.5}},
            QJsonObject{{"key","background"},{"duration",6.}}
        }}};
        json(projectPath,project);
        Studio s;s.loadProject(QUrl::fromLocalFile(projectPath));
        const auto canvas=s.timeline()->track("image");
        QCOMPARE(canvas["source"].toString(),imageUrl);
        QCOMPARE(canvas["start"].toDouble(),0.);QCOMPARE(canvas["duration"].toDouble(),30.);
        QCOMPARE(canvas["x"].toDouble(),0.);QCOMPARE(canvas["y"].toDouble(),0.);
        QCOMPARE(canvas["width"].toDouble(),1.);QCOMPARE(canvas["height"].toDouble(),1.);
        QCOMPARE(s.timeline()->duration(),30.);
        const auto normalized=s.timeline()->snapshot();
        s.saveProject(QUrl::fromLocalFile(fixedProject));s.newPage();
        s.loadProject(QUrl::fromLocalFile(fixedProject));
        QCOMPARE(s.timeline()->snapshot(),normalized);
        QCOMPARE(s.pageVisualUrl(),imageUrl);
    }
    void narrationSourceSwitchRemovesObsoleteVideoAndNeverMixesInputs(){
        QTemporaryDir dir;qputenv("MOSS_OUTPUT_ROOT",dir.path().toUtf8());
        const auto imported=dir.path()+"/import.wav",synthesized=dir.path()+"/tts.wav";
        const auto video=dir.path()+"/talking.mp4",projectPath=dir.path()+"/spoken.moss.json";
        for(const auto &path:{imported,synthesized,video}){
            QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write("independent source fixture");
        }
        Document document;auto project=document.json();
        project["audio"]=imported;project["video"]=QUrl::fromLocalFile(video).toString();
        project["sound"]=QJsonObject{{"voiceSource","import"},{"speechResult",synthesized}};
        json(projectPath,project);
        Studio s;s.loadProject(QUrl::fromLocalFile(projectPath));
        QCOMPARE(s.timeline()->track("speech")["kind"].toString(),QString("video"));
        s.sound()->set("voiceSource","tts");
        QVERIFY(s.pageVideoUrl().isEmpty());
        QCOMPARE(s.timeline()->track("speech")["source"].toString(),QUrl::fromLocalFile(synthesized).toString());
        QCOMPARE(s.timeline()->track("speech")["audioSource"].toString(),QUrl::fromLocalFile(synthesized).toString());
        QCOMPARE(s.timeline()->track("speech")["kind"].toString(),QString("audio"));
        s.sound()->set("voiceSource","import");
        QCOMPARE(s.timeline()->track("speech")["source"].toString(),QUrl::fromLocalFile(imported).toString());
        QCOMPARE(s.timeline()->track("speech")["audioSource"].toString(),QUrl::fromLocalFile(imported).toString());
        s.sound()->set("voiceSource","none");
        QVERIFY(s.timeline()->track("speech")["source"].toString().isEmpty());
        QVERIFY(s.timeline()->track("speech")["audioSource"].toString().isEmpty());
        s.sound()->set("voiceSource","tts");s.sound()->set("speechText","更新后的文案");
        QVERIFY(s.timeline()->track("speech")["source"].toString().isEmpty());
        QVERIFY(s.timeline()->track("speech")["audioSource"].toString().isEmpty());
        QVERIFY(s.pageVideoUrl().isEmpty());
    }
    void individualTrackExportsPreserveOriginalBytes(){
        QTemporaryDir dir;qputenv("MOSS_OUTPUT_ROOT",dir.path().toUtf8());
        Studio s;
        const auto image=dir.path()+"/generated.png";
        QImage fixture(48,48,QImage::Format_ARGB32);fixture.fill(QColor(10,20,30,128));QVERIFY(fixture.save(image));
        s.importTrackImage("image",QUrl::fromLocalFile(image));
        const auto audio=dir.path()+"/effect.wav",video=dir.path()+"/avatar.mp4";
        // The copy contract is independent of media decoding. Native export
        // playback is verified by the worker integration suite.
        {
            QFile file(audio);QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(QByteArray("RIFF\0\1\2\3WAVEfixture-audio",24)),qint64(24));
        }
        {
            QFile file(video);QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(QByteArray("\0\0\0\x18""ftypisomfixture-video",25)),qint64(25));
        }
        s.timeline()->setSource("sfx",audio,"audio");
        s.timeline()->setSource("speech",video,"video");
        for(const auto &key:{"image","sfx","speech"}){
            const auto source=QUrl(s.timeline()->track(key)["source"].toString()).toLocalFile();
            const auto destination=dir.path()+"/export-"+key;
            s.exportTrack(key,QUrl::fromLocalFile(destination));QVERIFY2(s.error().isEmpty(),qPrintable(s.error()));
            QFile before(source),after(destination);
            QVERIFY(before.open(QIODevice::ReadOnly));QVERIFY(after.open(QIODevice::ReadOnly));
            QCOMPARE(after.readAll(),before.readAll());
        }
        s.exportTrack("background",QUrl::fromLocalFile(dir.path()+"/no-source"));
        QVERIFY(!s.error().isEmpty());QVERIFY(!QFile::exists(dir.path()+"/no-source"));
    }
    void compositionImportImmediatelyPopulatesFixedCanvas(){
        QTemporaryDir dir;qputenv("MOSS_OUTPUT_ROOT",dir.path().toUtf8());
        const auto source=dir.path()+"/composition.png";
        QImage fixture(80,64,QImage::Format_RGB32);fixture.fill(Qt::cyan);QVERIFY(fixture.save(source));
        Studio s;QVERIFY(s.layerDocument()->importFiles({QUrl::fromLocalFile(source)}));
        QVERIFY(s.editComposition());
        QVERIFY(!s.pageVisualUrl().isEmpty());
        QCOMPARE(s.timeline()->track("image")["source"].toString(),s.pageVisualUrl());
    }
    void customVoiceOwnsRecordingAndProjectRestoresSound(){
        QTemporaryDir dir;qputenv("MOSS_OUTPUT_ROOT",dir.path().toUtf8());
        const auto source=dir.path()+"/reference.wav",project=dir.path()+"/page.moss.json";
        QFile wave(source);QVERIFY(wave.open(QIODevice::WriteOnly));
        QDataStream out(&wave);out.setByteOrder(QDataStream::LittleEndian);
        const int bytes=16000*2*4;
        out.writeRawData("RIFF",4);out<<quint32(36+bytes);out.writeRawData("WAVEfmt ",8);
        out<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(16000)<<quint32(32000)<<quint16(2)<<quint16(16);
        out.writeRawData("data",4);out<<quint32(bytes);
        out.writeRawData(QByteArray(bytes,0).constData(),bytes);wave.close();
        QString voiceId,ownedPath;
        {
            Studio s;
            QVERIFY(!s.sound()->createVoice("",QUrl::fromLocalFile(source),"测试录音").isEmpty());
            QCOMPARE(s.sound()->createVoice("测试音色",QUrl::fromLocalFile(source),"测试录音"),QString());
            voiceId=s.sound()->settings()["voiceId"].toString();
            ownedPath=s.sound()->selectedVoice()["reference"].toString();
            QVERIFY(source!=ownedPath);QVERIFY(QFile::remove(source));
            QVERIFY(QFile::exists(ownedPath));
            s.document()->setPrompt("保存这幅画");s.sound()->set("speechText","项目中的配音");
            s.sound()->set("sfxEnabled",true);s.sound()->set("sfxPrompt","soft rain");s.setNarrating(true);
            s.saveProject(QUrl::fromLocalFile(project));QVERIFY(QFile::exists(project));
            s.newPage();s.loadProject(QUrl::fromLocalFile(project));
            QCOMPARE(s.document()->prompt(),QString("保存这幅画"));
            QCOMPARE(s.sound()->settings()["voiceId"].toString(),voiceId);
            QCOMPARE(s.sound()->settings()["voiceSource"].toString(),QString("tts"));
            QCOMPARE(s.sound()->settings()["speechText"].toString(),QString("项目中的配音"));
            QVERIFY(s.sound()->settings()["sfxEnabled"].toBool());QVERIFY(s.narrating());
        }
        Studio restored;QCOMPARE(restored.sound()->selectedVoice()["reference"].toString(),ownedPath);
        QCOMPARE(restored.sound()->settings()["voiceId"].toString(),voiceId);
    }
    void invalidSoundFailsBeforeImageJob(){
        QTemporaryDir dir;qputenv("MOSS_OUTPUT_ROOT",dir.path().toUtf8());
        Studio s;s.document()->setPrompt("测试画面");
        s.sound()->set("voiceSource","tts");
        s.generateWithSound();
        QVERIFY(!s.busy());QVERIFY(s.error().contains("文案"));
        QVERIFY(QDir(dir.path()).entryList({"*-voice-*","*-page-*"},QDir::Dirs).isEmpty());
    }
    void repaintIsIndependentAndRestores(){
        QTemporaryDir dir;qputenv("MOSS_OUTPUT_ROOT",dir.path().toUtf8());
        const auto original=dir.path()+"/external.png",result=dir.path()+"/result.png";
        QImage fixture(80,64,QImage::Format_RGB32);fixture.fill(Qt::blue);QVERIFY(fixture.save(original));
        fixture.fill(Qt::red);QVERIFY(fixture.save(result));
        QImage mask;
        QString localSource;
        {
            Studio s;s.document()->setPrompt("原始生成内容");
            s.importEditImage(QUrl::fromLocalFile(original));
            QVERIFY(s.repainting());localSource=s.editDocument()->sourcePath();
            QVERIFY(localSource!=original);QVERIFY(QFile::remove(original));
            s.editDocument()->addRectangle(5,7,20,18);mask=s.editDocument()->mask();
            s.editDocument()->setProperty("prompt","修改选区");s.editDocument()->setProperty("strength",.6);
            s.editDocument()->setProperty("blendMode","direct");
            s.editDocument()->setResult(result);
            s.setRepainting(false);QVERIFY(!s.repainting());s.setRepainting(true);
            QCOMPARE(s.editDocument()->mask(),mask);QCOMPARE(s.document()->prompt(),QString("原始生成内容"));
            s.duplicatePage(0);s.editDocument()->clearMask();s.editDocument()->setProperty("prompt","副本重绘");
            s.selectPage(0);QCOMPARE(s.editDocument()->mask(),mask);QVERIFY(!s.editDocument()->canUndo());
            QCOMPARE(s.editDocument()->resultUrl(),QUrl::fromLocalFile(result).toString());
            s.newPage();QVERIFY(!s.editDocument()->ready());QVERIFY(!s.editDocument()->hasMask());
            QVERIFY(s.editDocument()->m_prompt.isEmpty());QVERIFY(!s.repainting());
            s.selectPage(0);
        }
        {
            Studio s;QCOMPARE(s.editDocument()->mask(),mask);QCOMPARE(s.editDocument()->sourcePath(),localSource);
            QCOMPARE(s.editDocument()->m_prompt,QString("修改选区"));QCOMPARE(s.editDocument()->m_strength,.6);
            QCOMPARE(s.editDocument()->m_blendMode,QString("direct"));
            QCOMPARE(s.editDocument()->resultUrl(),QUrl::fromLocalFile(result).toString());QVERIFY(s.repainting());
            s.deletePage(0);s.restoreDeletedPage();QCOMPARE(s.editDocument()->mask(),mask);
        }
    }
    void soundSourceIsExclusiveAndPageLocal(){
        QTemporaryDir dir;qputenv("MOSS_OUTPUT_ROOT",dir.path().toUtf8());
        QFile audio(dir.path()+"/audio.wav");QVERIFY(audio.open(QIODevice::WriteOnly));audio.write("fixture");audio.close();
        QString stored;
        {
            Studio s;s.setPageAudio(audio.fileName());stored=s.pageAudio();
            QVERIFY(stored!=audio.fileName());QCOMPARE(s.sound()->settings()["voiceSource"].toString(),QString("import"));
            s.sound()->set("voiceSource","tts");s.sound()->set("speechText","这是配音文案。");
            s.sound()->set("sfxEnabled",true);s.sound()->set("sfxPrompt","birds singing");
            s.setNarrating(true);QVERIFY(!s.repainting());
            s.newPage();QVERIFY(s.pageAudio().isEmpty());QCOMPARE(s.sound()->settings()["voiceSource"].toString(),QString("none"));
            s.setRepainting(true);QVERIFY(!s.narrating());
            s.selectPage(0);QVERIFY(s.narrating());QCOMPARE(s.pageAudio(),stored);
            QCOMPARE(s.sound()->settings()["voiceSource"].toString(),QString("tts"));
            QCOMPARE(s.sound()->settings()["speechText"].toString(),QString("这是配音文案。"));
            s.sound()->set("voiceSource","import");QCOMPARE(s.sound()->settings()["speechText"].toString(),QString("这是配音文案。"));
            s.sound()->set("voiceSource","both");QCOMPARE(s.sound()->settings()["voiceSource"].toString(),QString("import"));
            s.duplicatePage(0);s.sound()->set("sfxPrompt","rain");s.selectPage(0);
            QCOMPARE(s.sound()->settings()["sfxPrompt"].toString(),QString("birds singing"));
        }
        {
            Studio s;QVERIFY(s.narrating());QCOMPARE(s.pageAudio(),stored);
            QCOMPARE(s.sound()->settings()["voiceSource"].toString(),QString("import"));
            QCOMPARE(s.sound()->settings()["sfxPrompt"].toString(),QString("birds singing"));
            QVERIFY(s.sound()->settings()["sfxEnabled"].toBool());
        }
    }
    void independentDraftsAndRestart(){
        QTemporaryDir dir;QVERIFY(dir.isValid());qputenv("MOSS_OUTPUT_ROOT",dir.path().toUtf8());
        QString originalId,duplicateId;
        {
            Studio s;QCOMPARE(s.creationPages().size(),1);
            s.document()->loadTemplate("poster");s.setProperty("steps",70);s.setProperty("seed",123);
            const auto original=s.document()->json();
            s.newPage();QCOMPARE(s.currentPage(),1);QVERIFY(s.document()->prompt().isEmpty());
            QVERIFY(!s.document()->canUndo());
            s.document()->setPrompt("第二页的独立内容");s.setProperty("steps",20);s.setProperty("seed",456);
            s.selectPage(0);QCOMPARE(s.document()->json(),original);
            QCOMPARE(s.property("steps").toInt(),70);QCOMPARE(s.property("seed").toInt(),123);
            s.duplicatePage(0);QCOMPARE(s.currentPage(),1);QCOMPARE(s.document()->json(),original);
            originalId=s.creationPages()[0].toMap()["id"].toString();
            duplicateId=s.creationPages()[1].toMap()["id"].toString();
            QVERIFY(originalId!=duplicateId);
            s.document()->setPrompt("副本内容");
            s.selectPage(0);QCOMPARE(s.document()->json(),original);
            s.selectPage(2);QCOMPARE(s.document()->prompt(),QString("第二页的独立内容"));
            QCOMPARE(s.property("steps").toInt(),20);QCOMPARE(s.property("seed").toInt(),456);
            s.document()->undo();QCOMPARE(s.document()->prompt(),QString("第二页的独立内容"));
        }
        {
            Studio restored;QCOMPARE(restored.creationPages().size(),3);QCOMPARE(restored.currentPage(),2);
            QCOMPARE(restored.document()->prompt(),QString("第二页的独立内容"));
            restored.selectPage(1);QCOMPARE(restored.document()->prompt(),QString("副本内容"));
            QCOMPARE(restored.creationPages()[0].toMap()["id"].toString(),originalId);
            QCOMPARE(restored.creationPages()[1].toMap()["id"].toString(),duplicateId);
        }
    }
    void deleteRestoreAndLastPage(){
        QTemporaryDir dir;qputenv("MOSS_OUTPUT_ROOT",dir.path().toUtf8());
        Studio s;s.document()->setPrompt("需要保留的草稿");s.newPage();s.document()->setPrompt("另一个草稿");
        s.deletePage(0);QCOMPARE(s.creationPages().size(),1);QCOMPARE(s.currentPage(),0);
        QCOMPARE(s.document()->prompt(),QString("另一个草稿"));QVERIFY(s.canRestorePage());
        s.restoreDeletedPage();QCOMPARE(s.creationPages().size(),2);QCOMPARE(s.currentPage(),0);
        QCOMPARE(s.document()->prompt(),QString("需要保留的草稿"));
        s.deletePage(1);s.deletePage(0);QCOMPARE(s.creationPages().size(),1);
        QVERIFY(s.document()->prompt().isEmpty());QVERIFY(!s.document()->canUndo());
        s.restoreDeletedPage();QCOMPARE(s.document()->prompt(),QString("需要保留的草稿"));
    }
    void migrateHistoryWithoutDeletingOutputs(){
        QTemporaryDir dir;qputenv("MOSS_OUTPUT_ROOT",dir.path().toUtf8());
        QDir().mkpath(dir.path()+"/legacy-job");
        Document doc;doc.loadTemplate("note");
        auto project=doc.json();project["generation"]=QJsonObject{{"steps",70},{"seed",818},{"cfg",4},{"flowShift",5}};
        const auto image=dir.path()+"/legacy-job/image.png";QImage fixture(64,64,QImage::Format_RGB32);fixture.fill(Qt::green);QVERIFY(fixture.save(image));
        json(dir.path()+"/legacy-job/project.json",project);
        json(dir.path()+"/legacy-job/job.json",{{"kind","image"},{"success",true},{"output",image}});
        {
            Studio s;QCOMPARE(s.creationPages().size(),2);
            s.selectPage(0);QCOMPARE(s.document()->json(),doc.json());QCOMPARE(s.property("seed").toInt(),818);
            QCOMPARE(s.creationImageUrl(),QUrl::fromLocalFile(image).toString());
            s.deletePage(0);QVERIFY(QFile::exists(image));QCOMPARE(s.history().size(),1);
        }
        {
            Studio s;QCOMPARE(s.creationPages().size(),1); // Deletion survives restart.
            s.selectHistory(0);QCOMPARE(s.creationPages().size(),2);
            QCOMPARE(s.document()->json(),doc.json());
            s.selectHistory(0);QCOMPARE(s.creationPages().size(),2); // No duplicate page.
        }
    }
    void openingProjectPreservesCurrentDraft(){
        QTemporaryDir dir;qputenv("MOSS_OUTPUT_ROOT",dir.path().toUtf8());
        Studio s;s.document()->setPrompt("不要覆盖这份草稿");
        Document imported;imported.loadTemplate("poster");auto project=imported.json();
        project["generation"]=QJsonObject{{"steps",70},{"seed",999},{"cfg",5},{"flowShift",5}};
        json(dir.path()+"/import.json",project);
        s.loadProject(QUrl::fromLocalFile(dir.path()+"/import.json"));
        QCOMPARE(s.creationPages().size(),2);QCOMPARE(s.currentPage(),1);
        QCOMPARE(s.document()->json(),imported.json());QCOMPARE(s.property("seed").toInt(),999);
        QVERIFY(s.creationImageUrl().isEmpty());QVERIFY(!s.document()->canUndo());
        s.selectPage(0);QCOMPARE(s.document()->prompt(),QString("不要覆盖这份草稿"));
        s.loadProject(QUrl::fromLocalFile(dir.path()+"/missing.json"));
        QCOMPARE(s.creationPages().size(),2);QCOMPARE(s.document()->prompt(),QString("不要覆盖这份草稿"));
    }
};
QTEST_GUILESS_MAIN(CreationPagesTest)
#include "creation_pages_tests.moc"
