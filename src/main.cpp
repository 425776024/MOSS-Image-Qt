#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTimer>
#include <QDir>
#include <memory>
#include "Studio.h"
#include "WorkspaceLibrary.h"
#include <QStyleHints>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>
#include <QQuickImageProvider>
#include <QSettings>
#include <QKeyEvent>
#ifdef Q_OS_MACOS
#include "WindowAppearance.h"
#endif
class CanvasImageProvider final : public QQuickImageProvider {
public:
    explicit CanvasImageProvider(Studio *studio):QQuickImageProvider(QQuickImageProvider::Image),m_studio(studio){}
    QImage requestImage(const QString &,QSize *size,const QSize &requested) override {
        auto image=m_studio->canvasImage();if(size)*size=image.size();
        if(requested.isValid())image=image.scaled(requested,Qt::KeepAspectRatio,Qt::SmoothTransformation);
        return image;
    }
private:
    Studio *m_studio;
};
int main(int argc,char **argv){
    if(qEnvironmentVariableIsSet("MOSS_SSH_ASKPASS_SOCKET")){
        QCoreApplication app(argc,argv);return RemoteImageClient::askpass();
    }
    // Select the test platform before Qt initializes Cocoa. Hiding a native
    // window after show() is too late: launching it can already steal focus.
    QStringList startupArguments;
    for(int i=1;i<argc;++i)startupArguments.append(QString::fromLocal8Bit(argv[i]));
    const QStringList automatedModes={"--home-smoke","--smoke-ui","--window-test",
        "--download-model-test","--conversation-test","--timeline-test","--generate-test","--edit-test",
        "--avatar-test","--page-video-test","--voice-only-test","--voice-design-test","--avatar-design-test"};
    bool backgroundTest=startupArguments.contains("--headless");
    if(!startupArguments.contains("--interactive-test"))
        for(const auto &mode:automatedModes)backgroundTest|=startupArguments.contains(mode);
    if(backgroundTest){
        qputenv("QT_QPA_PLATFORM","offscreen");
        qputenv("QT_QUICK_BACKEND","software");
    }
    QGuiApplication app(argc,argv);
#ifdef MOSS_APP_STORE
    qputenv("MOSS_DISABLE_DEV_PATHS","1");
    qunsetenv("MOSS_SETTINGS_ROOT");
#endif
    if(backgroundTest)qInfo("Moss background test: offscreen rendering; no desktop window or input automation.");
    app.setOrganizationName("MossStudio");app.setApplicationName("MossStudio");app.setApplicationVersion("0.1.0");
    app.setApplicationDisplayName(QString::fromUtf8("新身数字人"));
    if(qEnvironmentVariableIsSet("MOSS_SETTINGS_ROOT")){
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,qEnvironmentVariable("MOSS_SETTINGS_ROOT"));
    }
    if(startupArguments.contains("--public-release-check")){
        if(QFile::exists(":/remote/scripts/remote_image_worker.py") ||
           QFile::exists(":/remote/config/gpu-server.json"))return 13;
        std::puts("MOSS_PUBLIC_LOCAL_ONLY_V1");return 0;
    }
    QQuickStyle::setStyle("Basic");
    // A deployed bundle owns its voices and workers. Do not probe the checkout
    // on normal desktop launch, even when this Mac still has that checkout.
    if(QFileInfo::exists(QCoreApplication::applicationDirPath()+"/../Resources/assets/voices/catalog.json"))
        qputenv("MOSS_DISABLE_DEV_PATHS","1");
    Studio studio;WorkspaceLibrary workspace(studio.outputRoot());QQmlApplicationEngine engine;
    app.setApplicationDisplayName(workspace.appName());
    QObject::connect(&workspace,&WorkspaceLibrary::preferencesChanged,&app,[&]{app.setApplicationDisplayName(workspace.appName());});
    const auto args=app.arguments();
    const int languageAt=args.indexOf("--ui-language");
    if(languageAt>=0&&languageAt+1<args.size())workspace.setSessionLanguage(args[languageAt+1]);
    const int themeAt=args.indexOf("--ui-theme");
    if(themeAt>=0&&themeAt+1<args.size())workspace.setAppearanceMode(args[themeAt+1]);
    engine.rootContext()->setContextProperty("studio",&studio);
    engine.rootContext()->setContextProperty("workspace",&workspace);
    engine.addImageProvider("moss-canvas",new CanvasImageProvider(&studio));
    QObject::connect(&engine,&QQmlApplicationEngine::objectCreationFailed,&app,[]{QCoreApplication::exit(2);},Qt::QueuedConnection);
    engine.loadFromModule("MossStudio","Main");
    if (engine.rootObjects().isEmpty()) return 2;
    auto homeWindow=qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    auto editorWindow=homeWindow; // Creation now lives in the one main window.
    const bool editorTest=args.contains("--smoke-ui")||args.contains("--timeline-test")||args.contains("--generate-test")||args.contains("--edit-test")||args.contains("--avatar-test")||args.contains("--page-video-test")||args.contains("--voice-only-test");
    auto mainWindow=editorTest?editorWindow:homeWindow;
#ifdef Q_OS_MACOS
    auto updateAppearance=[&]{if(app.platformName()!="cocoa")return;const bool dark=workspace.appearanceMode()=="dark"||(workspace.appearanceMode()=="system"&&app.styleHints()->colorScheme()==Qt::ColorScheme::Dark);configureWindowAppearance(homeWindow,dark);};
    updateAppearance();
    QObject::connect(&workspace,&WorkspaceLibrary::preferencesChanged,&app,updateAppearance);
    QObject::connect(app.styleHints(),&QStyleHints::colorSchemeChanged,&app,updateAppearance);
#endif
    if(mainWindow) mainWindow->show();
    if(editorTest){
        const auto at=args.indexOf("--editor-mode");
        const bool av=args.contains("--timeline-test")||args.contains("--avatar-test")||args.contains("--page-video-test")||args.contains("--voice-only-test");
        editorWindow->setProperty("creationMode",at>=0&&at+1<args.size()?args[at+1]:av?"av":"image");
    }
    if(args.contains("--window-test")){
        QTimer::singleShot(400,&app,[&]{
            const auto first=studio.conversation();studio.newConversation();const auto second=studio.conversation();
            QMetaObject::invokeMethod(homeWindow,"showEditor",Q_ARG(QVariant,QVariant("image")));
            studio.openConversation(first->id());
            QTimer::singleShot(250,&app,[&,first,second]{
                const bool single=QGuiApplication::topLevelWindows().size()==1&&homeWindow->isVisible();
                const bool cached=homeWindow->property("cachedViewCount").toInt()>=2&&studio.conversation()==first&&first!=second;
                const auto active=qvariant_cast<QObject *>(homeWindow->property("activeView"));
                const auto composer=active?active->findChild<QQuickItem *>("conversationComposer"):nullptr;
                const auto history=active?active->findChild<QQuickItem *>("conversationHistory"):nullptr;
                const bool aligned=composer&&history&&qAbs(composer->mapToScene(QPointF()).x()-history->mapToScene(QPointF()).x())<1&&qAbs(composer->width()-history->width())<1;
                const auto dir=qEnvironmentVariable("MOSS_EVIDENCE_ROOT",QString(MOSS_SOURCE_ROOT)+"/.local/evidence");QDir().mkpath(dir);
                homeWindow->grabWindow().save(dir+"/conversation-window.png");
                QFile report(dir+"/window-state.json");if(report.open(QIODevice::WriteOnly))report.write(QJsonDocument(QJsonObject{{"singleMainWindow",single},{"cachedWorkViews",cached},{"worksRatio",homeWindow->property("worksRatio").toDouble()},{"alignedConversationRail",aligned},{"composerWidth",composer?composer->width():0},{"historyWidth",history?history->width():0}}).toJson());
                app.exit(single&&cached&&aligned?0:9);
            });
        });
    }
    if(args.contains("--conversation-test")){
        auto argument=[&](QString key,QString fallback){const int at=args.indexOf(key);return at>=0&&at+1<args.size()?args[at+1]:fallback;};
        const auto task=argument("--task","image");studio.newConversation();auto thread=studio.conversation();
        if(task=="edit"||task=="avatar")thread->importMedia(QUrl::fromLocalFile(argument("--source",QString(MOSS_SOURCE_ROOT)+"/.local/test-inputs/portrait.png")),"target");
        if(task=="avatar")thread->importMedia(QUrl::fromLocalFile(argument("--audio",QString(MOSS_SOURCE_ROOT)+"/.local/test-inputs/audio.wav")),"audio");
        if(args.contains("--voice-id"))thread->setVoiceId(argument("--voice-id",{}));
        thread->setVoiceOptions({{"emotion",argument("--voice-emotion","中性")},{"dialect",argument("--voice-dialect","standard")}});
        thread->setTask(task);thread->setDraft(argument("--prompt",task=="sfx"?"Soft wind blowing through a quiet garden, natural ambient sound.":task=="edit"?"Change the orange hat to a blue hat, keeping the same person and garden.":task=="voice"?"您好，欢迎来到对话创作。":"A blue ceramic cup beside a small orange, clean cream background, soft studio light."));
        studio.prepareConversationSettings();
        if(args.contains("--layout-template")){
            studio.document()->loadTemplate(argument("--layout-template","objects"));
            thread->setDraft(studio.document()->prompt());
        }
        studio.document()->setCanvasSize(512,512);studio.setProperty("steps",argument("--steps","8").toInt());
        if(task=="edit"&&args.contains("--brush-mask")&&studio.prepareConversationEdit()){
            const auto values=argument("--brush-mask","150,200,350,200,100").split(',');
            if(values.size()==5){
                auto edit=studio.editDocument();edit->beginStroke();
                edit->paintLine(QPointF(values[0].toDouble(),values[1].toDouble()),QPointF(values[2].toDouble(),values[3].toDouble()),values[4].toDouble(),false);
                edit->endStroke();
            }
        }
        auto completed=std::make_shared<bool>(false);
        auto finish=[&,thread,completed,task](bool success){
            if(*completed)return;*completed=true;
            QTimer::singleShot(500,&app,[&,thread,task,success]{
                const auto dir=qEnvironmentVariable("MOSS_EVIDENCE_ROOT",QString(MOSS_SOURCE_ROOT)+"/.local/evidence");QDir().mkpath(dir);
                homeWindow->grabWindow().save(dir+"/conversation-"+task+".png");
                QFile report(dir+"/conversation-"+task+".json");if(report.open(QIODevice::WriteOnly)){auto result=thread->json();result["success"]=success;result["engineError"]=studio.error();result["document"]=studio.document()->json();result["edit"]=studio.editDocument()->snapshot();report.write(QJsonDocument(result).toJson());}
                studio.flushWorkspace();app.exit(success?0:11);
            });
        };
        QObject::connect(&studio,&Studio::jobFinished,&app,finish);
        auto started=std::make_shared<bool>(false);
        auto launch=[&,thread,finish,started,task]{
            if(*started||((task=="image"||task=="edit")&&studio.imageDevice()=="remote"&&!studio.remoteGpu()->ready()))return;
            *started=true;studio.sendConversation();
            if(!studio.busy())finish(!thread->pending()&&!thread->messages().isEmpty()&&thread->messages().last().toMap()["status"]=="complete");
        };
        QObject::connect(studio.remoteGpu(),&RemoteImageClient::changed,&app,launch);
        QTimer::singleShot(500,&app,launch);
        QTimer::singleShot(45000,&app,[started,finish]{if(!*started)finish(false);});
        QTimer::singleShot(600000,&app,[&]{studio.cancel();app.exit(12);});
    }
    // The test harness follows the same asynchronous connection and Studio path as the UI.
    auto startImageTest=[&](auto action){
        auto started=std::make_shared<bool>(false);
        auto launch=[&,started,action]{
            if(*started||(studio.imageDevice()=="remote"&&!studio.remoteGpu()->ready()))return;
            *started=true;action();
        };
        QObject::connect(studio.remoteGpu(),&RemoteImageClient::changed,&app,launch);
        QTimer::singleShot(500,&app,launch);
        QTimer::singleShot(45000,&app,[&,started]{if(!*started)app.exit(8);});
    };
    if(args.contains("--smoke-ui")||args.contains("--home-smoke")){
        auto argument=[&](QString key,QString fallback){int at=args.indexOf(key);return at>=0&&at+1<args.size()?args[at+1]:fallback;};
        if(!args.contains("--preserve-preview")&&!args.contains("--home-smoke"))studio.document()->example();
        if(args.contains("--home-smoke")){
            homeWindow->setProperty("destination",argument("--home-page","chat"));
            if(auto settings=homeWindow->findChild<QObject *>("homeSettings"))
                settings->setProperty("section",argument("--settings-section",argument("--home-page","chat")=="about"?"5":"0").toInt());
            if(args.contains("--show-editor"))editorWindow->show();
            const auto work=argument("--select-work",QString()).toInt();
            if(args.contains("--select-work")&&work>=0&&work<studio.creationPages().size())
                studio.openConversation(studio.creationPages()[work].toMap()["id"].toString());
            if(args.contains("--capture-task"))studio.conversation()->setTask(argument("--capture-task","image"));
            if(args.contains("--capture-portrait")){
                const auto source=QUrl::fromLocalFile(argument("--capture-portrait",QString()));
                if(auto library=homeWindow->findChild<QObject *>("homeAvatarLibrary")){
                    library->setProperty("pendingAvatarSource",source);
                    if(argument("--home-page",{})=="avatars"||argument("--capture-dialog",{})=="homeAvatarImportDialog")
                        QMetaObject::invokeMethod(library,"previewAvatar",Q_ARG(QVariant,QVariant(source)));
                }
                if(auto creator=homeWindow->findChild<QObject *>("homeAvatarCreator"))creator->setProperty("sourceUrl",source.toString());
            }
            if(args.contains("--capture-export"))QTimer::singleShot(400,&app,[&,argument]{
                const auto dir=qEnvironmentVariable("MOSS_EVIDENCE_ROOT",QString(MOSS_SOURCE_ROOT)+"/.local/evidence");
                QDir().mkpath(dir);
                const auto source=QUrl::fromLocalFile(argument("--capture-export",{}));
                studio.conversation()->importMedia(source,"target");
                const auto assets=studio.conversation()->assets();
                if(!assets.isEmpty())studio.exportConversationAsset(assets.last().toMap()["id"].toString(),QUrl::fromLocalFile(dir+"/dialog-export.png"));
            });
            if(args.contains("--capture-voice-edit")){
                if(auto voices=homeWindow->findChild<QObject *>("homeVoiceLibrary"))
                    for(const auto &voice:studio.sound()->voices())if(!voice.toMap()["builtin"].toBool()){
                        QMetaObject::invokeMethod(voices,"editVoice",Q_ARG(QVariant,voice));break;
                    }
            }
            const auto detailKind=argument("--capture-detail",QString());
            if(!detailKind.isEmpty())QTimer::singleShot(400,&app,[&,detailKind]{
                auto active=qvariant_cast<QObject *>(homeWindow->property("activeView"));
                if(!active)return;
                for(const auto &value:studio.conversation()->messages()){
                    const auto message=value.toMap();
                    if(detailKind=="request"&&message["role"]=="user"){
                        QMetaObject::invokeMethod(active,"showDetails",Q_ARG(QVariant,message["id"]),Q_ARG(QVariant,QVariant("")));return;
                    }
                    for(const auto &id:message["assetIds"].toList())
                        if(studio.conversation()->asset(id.toString())["kind"]==detailKind){
                            QMetaObject::invokeMethod(active,"showDetails",Q_ARG(QVariant,message["id"]),Q_ARG(QVariant,id));return;
                        }
                }
                qWarning()<<"No detail fixture for"<<detailKind;app.exit(4);
            });
            if(args.contains("--capture-remote")){
                studio.setImageDevice("remote");
                if(auto remote=homeWindow->findChild<QObject *>("homeRemoteGpuSettings"))remote->setProperty("advanced",true);
            }
            if(args.contains("--open-image-tools"))QTimer::singleShot(400,&app,[&]{
                auto active=qvariant_cast<QObject *>(homeWindow->property("activeView"));
                if(active)QMetaObject::invokeMethod(active,"openImageTools");
                if(studio.document()->regions().size())studio.document()->setSelected(0);
            });
            if(args.contains("--open-pack-tools"))QTimer::singleShot(400,&app,[&]{
                auto active=qvariant_cast<QObject *>(homeWindow->property("activeView"));
                if(active)QMetaObject::invokeMethod(active,"showPackTools");
            });
            if(args.contains("--capture-models"))QTimer::singleShot(300,&app,[&]{
                const int at=args.indexOf("--capture-models");
                studio.models()->requestDownload(at+1<args.size()?args[at+1]:"image");
            });
            const auto dialogName=argument("--capture-dialog",QString());
            const auto controlName=argument("--activate-control",QString());
            if(!controlName.isEmpty())QTimer::singleShot(650,&app,[&,controlName]{
                auto active=qvariant_cast<QObject *>(homeWindow->property("activeView"));
                auto control=active?active->findChild<QObject *>(controlName):nullptr;
                if(!control)control=homeWindow->findChild<QObject *>(controlName);
                if(!control){
                    // Repeater delegates can be visually parented without a
                    // corresponding QObject parent chain.
                    std::function<QObject *(QQuickItem *)> findControl=[&](QQuickItem *item)->QObject *{
                        if(item->objectName()==controlName)return item;
                        for(auto child:item->childItems())if(auto found=findControl(child))return found;
                        return nullptr;
                    };
                    control=findControl(homeWindow->contentItem());
                }
                if(!control||!QMetaObject::invokeMethod(control,"clicked")){qWarning()<<"Capture control not found:"<<controlName;app.exit(4);}
            });
            const bool packPicker=args.contains("--capture-pack-picker");
            if(!dialogName.isEmpty())QTimer::singleShot(700,&app,[&,dialogName,packPicker]{
                auto active=qvariant_cast<QObject *>(homeWindow->property("activeView"));
                if(packPicker&&active)active->setProperty("pickerMode","pack");
                auto dialog=active ? active->findChild<QObject *>(dialogName) : nullptr;
                if(!dialog)dialog=homeWindow->findChild<QObject *>(dialogName);
                if(!dialog){qWarning()<<"Capture dialog not found:"<<dialogName;app.exit(4);return;}
                if(dialogName=="generationParametersDialog"||dialogName=="canvasSizeChoices"||dialogName=="canvasCustomSize")
                    studio.prepareConversationSettings();
                if(dialogName=="canvasSizeChoices"||dialogName=="canvasCustomSize"){
                    auto parameters=active->findChild<QObject *>("generationParametersDialog");
                    if(parameters)QMetaObject::invokeMethod(parameters,"open");
                }
                QMetaObject::invokeMethod(dialog,"open");
            });
            const auto inputName=argument("--check-input",QString());
            const auto dialogKey=argument("--dialog-key",QString());
            if(!dialogKey.isEmpty())QTimer::singleShot(1100,&app,[&,dialogKey]{
                if(app.platformName()!="offscreen"){qWarning()<<"Dialog key probe requires offscreen mode";app.exit(5);return;}
                const int key=dialogKey=="return" ? Qt::Key_Return : dialogKey=="cancel" ? Qt::Key_Period : Qt::Key_Escape;
                const auto modifiers=dialogKey=="cancel" ? Qt::MetaModifier : Qt::NoModifier;
                QKeyEvent press(QEvent::KeyPress,key,modifiers),release(QEvent::KeyRelease,key,modifiers);
                QCoreApplication::sendEvent(mainWindow,&press);QCoreApplication::sendEvent(mainWindow,&release);
            });
            if(!inputName.isEmpty())QTimer::singleShot(1100,&app,[&,inputName]{
                // Dispatch directly to our offscreen window, never to the OS.
                if(app.platformName()!="offscreen"){qWarning()<<"Input probe requires offscreen mode";app.exit(5);return;}
                auto active=qvariant_cast<QObject *>(homeWindow->property("activeView"));
                auto input=active?active->findChild<QObject *>(inputName):nullptr;
                if(!input)input=homeWindow->findChild<QObject *>(inputName);
                auto editor=input?input->findChild<QQuickItem *>(inputName+"Text"):nullptr;
                if(!editor){qWarning()<<"Input probe field not found"<<inputName;app.exit(5);return;}
                const QString probe=QString::fromUtf8("编辑同步检查：蓝色杯子。");
                editor->forceActiveFocus();QMetaObject::invokeMethod(editor,"selectAll");
                QKeyEvent press(QEvent::KeyPress,0,Qt::NoModifier,probe);
                QKeyEvent release(QEvent::KeyRelease,0,Qt::NoModifier,probe);
                QCoreApplication::sendEvent(mainWindow,&press);QCoreApplication::sendEvent(mainWindow,&release);
                const bool synced=editor->property("text")==probe&&input->property("value")==probe;
                qInfo()<<"Offscreen input synchronization"<<inputName<<synced;
                if(!synced)app.exit(5);
            });
        }
        mainWindow->setProperty("page",argument("--capture-page","0").toInt());
        mainWindow->setProperty("navCollapsed",!args.contains("--expanded-navigation"));
        if(args.contains("--compact-window"))mainWindow->resize(1080,720);
        if(args.contains("--minimum-window"))mainWindow->resize(mainWindow->minimumSize());
        if(args.contains("--store-screenshot"))mainWindow->resize(1440,900);
        const auto captureName=argument("--capture-name","layout-ui");
        const auto captureDialog=argument("--capture-dialog",QString());
        QTimer::singleShot(1800,&app,[&,captureName,captureDialog]{
            auto win=mainWindow;
            const auto dir=qEnvironmentVariable("MOSS_EVIDENCE_ROOT",QString(MOSS_SOURCE_ROOT)+"/.local/evidence");
            QDir().mkpath(dir);
            bool ok=win&&win->grabWindow().save(dir+"/"+captureName+".png");
            auto active=qvariant_cast<QObject *>(homeWindow->property("activeView"));
            auto history=active?active->findChild<QQuickItem *>("conversationHistory"):nullptr;
            QJsonArray cards,waveforms,requests;
            if(history){
                const QRectF viewport(history->mapToScene(QPointF()),history->size());
                std::function<void(QQuickItem *)> visit=[&](QQuickItem *item){
                    if((item->objectName()=="creationCard"||item->objectName()=="requestCard")&&item->isVisible()){
                        const QRectF rect(item->mapToScene(QPointF()),item->size());
                        const QJsonObject bounds{{"x",rect.x()},{"y",rect.y()},{"width",rect.width()},{"height",rect.height()},{"fullyVisible",viewport.contains(rect)}};
                        if(item->objectName()=="requestCard")requests.append(bounds);else cards.append(bounds);
                    }
                    if(item->objectName()=="audioWaveform"&&item->isVisible()){
                        const auto point=item->mapToScene(QPointF());
                        waveforms.append(QJsonObject{{"x",point.x()},{"y",point.y()},{"width",item->width()},{"height",item->height()},{"ready",item->property("ready").toBool()},{"seconds",item->property("seconds").toDouble()},{"error",item->property("error").toString()}});
                    }
                    for(auto child:item->childItems())visit(child);
                };visit(history);
            }
            QJsonArray visibleUiText;
            std::function<void(QQuickItem *)> collectUiText=[&](QQuickItem *item){
                if(!item->isVisible() || item->opacity()<=0)return;
                const auto value=item->property("text");
                if(value.metaType().id()==QMetaType::QString && !value.toString().isEmpty())visibleUiText.append(value.toString());
                for(auto child:item->childItems())collectUiText(child);
            };collectUiText(win->contentItem());
            QJsonArray truncatedButtons;
            std::function<void(QQuickItem *)> inspectButtons=[&](QQuickItem *item){
                if(item->isVisible()&&item->objectName()=="appButtonLabel"&&item->property("truncated").toBool())truncatedButtons.append(item->property("text").toString());
                for(auto child:item->childItems())inspectButtons(child);
            };inspectButtons(homeWindow->contentItem());
            QFile geometry(dir+"/"+captureName+"-geometry.json");
            QJsonObject panels;
            for(const auto &name:{"navigationRail","worksPane","conversationChatPanel","conversationInspector","conversationHistory","conversationComposer"}){
                auto item=QString(name).startsWith("conversation")&&active?active->findChild<QQuickItem *>(name):homeWindow->findChild<QQuickItem *>(name);
                if(item){const auto p=item->mapToScene(QPointF());panels[name]=QJsonObject{{"x",p.x()},{"y",p.y()},{"width",item->width()},{"height",item->height()},{"visible",item->isVisible()}};}
            }
            QJsonObject popup;
            QJsonObject portraitPreview;
            if(auto preview=homeWindow->findChild<QObject *>("homeAvatarImportPreview")){
                portraitPreview["ready"]=preview->property("ready").toBool();
                portraitPreview["source"]=preview->property("source").toUrl().toString();
                if(auto image=preview->findChild<QQuickItem *>("avatarImportPreviewImage")){
                    portraitPreview["width"]=image->width();portraitPreview["height"]=image->height();
                    const auto size=image->property("sourceSize").toSize();
                    portraitPreview["sourceWidth"]=size.width();portraitPreview["sourceHeight"]=size.height();
                }
                if(auto save=homeWindow->findChild<QObject *>("homeAvatarImportSave"))portraitPreview["saveEnabled"]=save->property("enabled").toBool();
                if(args.contains("--capture-portrait")&&!args.contains("--allow-image-error"))
                    ok=ok&&portraitPreview["ready"].toBool()&&portraitPreview["width"].toDouble()>0&&portraitPreview["height"].toDouble()>0;
            }
            if(!captureDialog.isEmpty()){
                auto dialog=active?active->findChild<QObject *>(captureDialog):nullptr;
                if(!dialog)dialog=homeWindow->findChild<QObject *>(captureDialog);
                if(dialog)for(const auto key:{"x","y","width","height","visible","modal","activeFocus"})popup[key]=QJsonValue::fromVariant(dialog->property(key));
            }
            if(geometry.open(QIODevice::WriteOnly))geometry.write(QJsonDocument(QJsonObject{{"platform",app.platformName()},{"language",workspace.language()},{"visibleUiText",visibleUiText},{"backgroundTest",backgroundTest},{"windowWidth",win->width()},{"windowHeight",win->height()},{"portraitPreview",portraitPreview},{"popup",popup},{"cards",cards},{"requests",requests},{"waveforms",waveforms},{"panels",panels},{"truncatedButtons",truncatedButtons}}).toJson());
            app.exit(ok?0:3);
        });
    }
    if(args.contains("--timeline-test")){
        auto argument=[&](QString key,QString fallback){int at=args.indexOf(key);return at>=0&&at+1<args.size()?args[at+1]:fallback;};
        const auto project=argument("--project","");
        if(!project.isEmpty())studio.loadProject(QUrl::fromLocalFile(project));
        const auto duration=argument("--duration","30").toDouble();
        for(const auto &key:{"speech","sfx","background"})studio.timeline()->setRange(key,0,duration);
        studio.timeline()->setTransform("speech",.62,.27,.34,.5);
        studio.timeline()->setValue("background","color","#dfe9d4");
        studio.setRepainting(false);
        studio.timeline()->select("image");
        const auto destination=QUrl::fromLocalFile(argument("--export",studio.outputRoot()+"/timeline-export.mp4"));
        QObject::connect(&studio,&Studio::jobFinished,&app,[&](bool success){
            QTimer::singleShot(800,&app,[&,success]{
                const auto dir=qEnvironmentVariable("MOSS_EVIDENCE_ROOT",QString(MOSS_SOURCE_ROOT)+"/.local/evidence");
                QDir().mkpath(dir);mainWindow->grabWindow().save(dir+"/timeline-ui.png");
                app.exit(success?0:7);
            });
        });
        QTimer::singleShot(500,&studio,[&studio,destination]{studio.exportTimeline(destination);});
    }
    if(args.contains("--generate-test")){
        auto argument=[&](QString key,QString fallback){int at=args.indexOf(key);return at>=0&&at+1<args.size()?args[at+1]:fallback;};
        if(args.contains("--layout"))studio.document()->example();
        else {
            studio.document()->setLayout(false);
            studio.document()->setPrompt("A friendly blue robot standing beside a small orange cube on a white table, green potted plant to the right, soft studio light, clean cream background.");
        }
        if(args.contains("--prompt"))studio.document()->setPrompt(argument("--prompt",{}));
        studio.setImageQuant(argument("--quant",studio.imageQuant()));
        studio.setProperty("seed",argument("--seed","42").toInt());
        int at=args.indexOf("--size");if(at>=0&&at+1<args.size()){studio.document()->setCanvasWidth(args[at+1].toInt());studio.document()->setCanvasHeight(args[at+1].toInt());}
        at=args.indexOf("--steps");if(at>=0&&at+1<args.size())studio.setProperty("steps",args[at+1].toInt());
        QObject::connect(&studio,&Studio::jobFinished,&app,[&](bool success){
            QTimer::singleShot(1200,&app,[&,success]{
                auto win=mainWindow;
                win->grabWindow().save(qEnvironmentVariable("MOSS_EVIDENCE_ROOT",QString(MOSS_SOURCE_ROOT)+"/.local/evidence")+"/generation-ui.png");
                app.exit(success?0:4);
            });
        });
        startImageTest([&studio]{studio.generate();});
    }
    if(args.contains("--edit-test")){
        auto argument=[&](QString key,QString fallback){int at=args.indexOf(key);return at>=0&&at+1<args.size()?args[at+1]:fallback;};
        studio.importEditImage(QUrl::fromLocalFile(argument("--source",QString(MOSS_SOURCE_ROOT)+"/.local/final-works/20261020-155506-81e811/image.png")));
        const auto rectangle=argument("--mask-rect","275,318,123,126").split(',');
        if(rectangle.size()!=4){app.exit(6);return 6;}
        studio.editDocument()->addRectangle(rectangle[0].toDouble(),rectangle[1].toDouble(),rectangle[2].toDouble(),rectangle[3].toDouble());
        studio.editDocument()->setProperty("prompt",argument("--prompt","A friendly blue robot standing beside a vivid red cube on a white table, green potted plant to the right, soft studio light, clean cream background. The cube is bright red."));
        studio.editDocument()->setProperty("blendMode",argument("--blend-mode","feather")=="direct"?"direct":"feather");
        studio.editDocument()->setProperty("strength",argument("--strength","0.85").toDouble());
        studio.setImageQuant(argument("--quant",studio.imageQuant()));
        studio.setProperty("seed",argument("--seed","42").toInt());
        studio.setProperty("steps",argument("--steps","16").toInt());
        mainWindow->setProperty("page",4);
        QObject::connect(&studio,&Studio::jobFinished,&app,[&](bool success){
            QTimer::singleShot(1200,&app,[&,success]{
                auto win=mainWindow;
                const auto dir=qEnvironmentVariable("MOSS_EVIDENCE_ROOT",QString(MOSS_SOURCE_ROOT)+"/.local/evidence");
                QDir().mkpath(dir);win->grabWindow().save(dir+"/edit-ui.png");
                app.exit(success?0:6);
            });
        });
        startImageTest([&studio]{studio.generateEdit();});
    }
    if(args.contains("--avatar-test")||args.contains("--page-video-test")){
        auto argument=[&](QString key,QString fallback){int at=args.indexOf(key);return at>=0&&at+1<args.size()?args[at+1]:fallback;};
        studio.importEditImage(QUrl::fromLocalFile(argument("--source",argument("--portrait",QString(MOSS_SOURCE_ROOT)+"/.local/test-inputs/portrait.png"))));
        if(args.contains("--voice-text")){
            studio.sound()->set("voiceSource","tts");studio.sound()->set("speechText",argument("--voice-text","你好。"));
            studio.sound()->set("voiceId",argument("--voice-id","videocut-original-mandarin-01"));
        }else if(!args.contains("--no-speech"))studio.setPageAudio(argument("--audio",QString(MOSS_SOURCE_ROOT)+"/.local/test-inputs/audio.wav"));
        else studio.sound()->set("voiceSource","none");
        if(args.contains("--sound-prompt")){
            studio.sound()->set("sfxEnabled",true);studio.sound()->set("sfxPrompt",argument("--sound-prompt",""));
            studio.sound()->set("sfxDuration",argument("--sound-duration","3").toInt());
        }
        studio.setNarrating(true);mainWindow->setProperty("page",0);
        QObject::connect(&studio,&Studio::jobFinished,&app,[&](bool success){
            if(success){
                auto player=mainWindow->findChild<QObject*>("pageVideoPlayer");
                if(player)QMetaObject::invokeMethod(player,"play");
            }
            QTimer::singleShot(1800,&app,[&,success]{
                auto win=mainWindow;
                win->grabWindow().save(qEnvironmentVariable("MOSS_EVIDENCE_ROOT",QString(MOSS_SOURCE_ROOT)+"/.local/evidence")+"/page-video-ui.png");
                app.exit(success?0:5);
            });
        });
        const double seconds=argument("--duration","3.2").toDouble();
        QTimer::singleShot(500,&studio,[&studio,seconds]{studio.generatePageVideo(seconds);});
    }
    if(args.contains("--avatar-design-test")){
        auto argument=[&](QString key,QString fallback){int at=args.indexOf(key);return at>=0&&at+1<args.size()?args[at+1]:fallback;};
        homeWindow->setProperty("destination","avatars");studio.setImageQuant(argument("--quant","Q8_0"));
        const auto description=argument("--prompt","A photorealistic studio head and shoulders portrait of a friendly woman in her early thirties, looking directly at the camera, natural face, dark shoulder-length hair, a simple blue blouse, calm gentle smile, soft even lighting, plain warm gray background, one person, face and shoulders fully visible, no text, no watermark.");
        const auto name=argument("--avatar-name","MOSS 设计形象");
        studio.document()->setPrompt("Existing editor document must be preserved.");const auto originalDocument=studio.document()->json();
        QObject::connect(&studio,&Studio::jobFinished,&app,[&,description,name,originalDocument](bool success){
            QTimer::singleShot(800,&app,[&,success,description,name,originalDocument]()mutable{
                const auto dir=qEnvironmentVariable("MOSS_EVIDENCE_ROOT",QString(MOSS_SOURCE_ROOT)+"/.local/evidence");QDir().mkpath(dir);homeWindow->grabWindow().save(dir+"/avatar-design-preview.png");
                auto save=homeWindow->findChild<QObject*>("avatarDesignSave");
                if(success&&save)success=QMetaObject::invokeMethod(save,"clicked");else success=false;
                const auto avatar=workspace.avatars().isEmpty()?QVariantMap():workspace.avatars().first().toMap();
                WorkspaceLibrary reopened(studio.outputRoot());
                const bool preserved=studio.document()->json()==originalDocument;
                success=success&&preserved&&avatar["name"]==name&&avatar["synthetic"].toBool()&&avatar["generationRecipe"].toMap()["prompt"]==description&&QFileInfo::exists(QUrl(avatar["url"].toString()).toLocalFile())&&!reopened.avatars().isEmpty()&&reopened.avatars().first().toMap()["id"]==avatar["id"];
                QFile report(dir+"/avatar-design.json");if(report.open(QIODevice::WriteOnly))report.write(QJsonDocument(QJsonObject{{"success",success},{"editorDocumentUnchanged",preserved},{"avatar",QJsonObject::fromVariantMap(avatar)},{"error",studio.error()}}).toJson());
                QTimer::singleShot(500,&app,[&,success,dir]{homeWindow->grabWindow().save(dir+"/avatar-design-saved.png");app.exit(success?0:16);});
            });
        });
        QTimer::singleShot(500,&app,[&,description,name]{
            auto library=homeWindow->findChild<QObject*>("homeAvatarLibrary");if(!library||!QMetaObject::invokeMethod(library,"designAvatar")){app.exit(16);return;}
            auto nameField=homeWindow->findChild<QObject*>("avatarDesignName");auto prompt=homeWindow->findChild<QObject*>("avatarDesignDescription");auto generate=homeWindow->findChild<QObject*>("avatarDesignGenerate");
            if(!nameField||!prompt||!generate){app.exit(16);return;}nameField->setProperty("text",name);prompt->setProperty("value",description);
            if(!QMetaObject::invokeMethod(generate,"clicked"))app.exit(16);
        });
        QTimer::singleShot(600000,&app,[&]{studio.cancel();app.exit(16);});
    }
    if(args.contains("--voice-design-test")){
        auto argument=[&](QString key,QString fallback){int at=args.indexOf(key);return at>=0&&at+1<args.size()?args[at+1]:fallback;};
        homeWindow->setProperty("destination","voices");
        const auto description=argument("--description","温暖沉稳的成年女声，吐字清晰，语速舒缓，适合讲述故事。");
        const auto text=argument("--voice-text","你好，很高兴与你见面。让我们用声音记录生活，把每一个故事讲给你听。");
        const auto name=argument("--voice-name","Ming 设计声音");
        QObject::connect(&studio,&Studio::jobFinished,&app,[&,description,text,name](bool success){
            QTimer::singleShot(800,&app,[&,description,text,name,success]()mutable{
            const auto dir=qEnvironmentVariable("MOSS_EVIDENCE_ROOT",QString(MOSS_SOURCE_ROOT)+"/.local/evidence");QDir().mkpath(dir);
            homeWindow->grabWindow().save(dir+"/voice-design-preview.png");
            auto save=homeWindow->findChild<QObject*>("voiceDesignSave");
            if(success&&save)success=QMetaObject::invokeMethod(save,"clicked");else success=false;
            const auto selected=studio.sound()->selectedVoice();
            success=success&&selected["origin"]=="ming-voice-design"&&selected["name"]==name&&QFileInfo::exists(selected["reference"].toString());
            SoundPlan reopened;reopened.initialize(studio.outputRoot());reopened.set("voiceId",selected["id"]);
            success=success&&reopened.selectedVoice()["designRecipe"].toMap()["description"]==description;
            QFile report(dir+"/voice-design.json");if(report.open(QIODevice::WriteOnly))report.write(QJsonDocument(QJsonObject{{"success",success},{"voice",QJsonObject::fromVariantMap(selected)},{"error",studio.error()}}).toJson());
            QTimer::singleShot(500,&app,[&,success,dir]{homeWindow->grabWindow().save(dir+"/voice-design-saved.png");app.exit(success?0:15);});
            });
        });
        QTimer::singleShot(500,&app,[&,description,text,name]{
            auto library=homeWindow->findChild<QObject*>("homeVoiceLibrary");
            if(!library||!QMetaObject::invokeMethod(library,"designVoice")){app.exit(15);return;}
            const auto set=[&](const char *object,const char *property,const QString &value){auto field=homeWindow->findChild<QObject*>(object);return field&&field->setProperty(property,value);};
            if(!set("voiceDesignName","text",name)||!set("voiceDesignDescription","value",description)||!set("voiceDesignText","value",text)){app.exit(15);return;}
            auto generate=homeWindow->findChild<QObject*>("voiceDesignGenerate");
            if(!generate||!QMetaObject::invokeMethod(generate,"clicked"))app.exit(15);
        });
        QTimer::singleShot(180000,&app,[&]{studio.cancel();app.exit(15);});
    }
    if(args.contains("--voice-only-test")){
        auto argument=[&](QString key,QString fallback){int at=args.indexOf(key);return at>=0&&at+1<args.size()?args[at+1]:fallback;};
        studio.importTrackImage("image",QUrl::fromLocalFile(argument("--source",QString(MOSS_SOURCE_ROOT)+"/.local/test-inputs/portrait.png")));
        studio.sound()->set("voiceSource","tts");studio.sound()->set("speechText",argument("--voice-text","你好，欢迎使用图像与音视频创作工作台。"));
        studio.sound()->set("voiceId",argument("--voice-id","videocut-original-mandarin-01"));
        QMetaObject::invokeMethod(editorWindow,"openInspector",Q_ARG(QVariant,QVariant("voice")));
        QObject::connect(&studio,&Studio::jobFinished,&app,[&](bool success){
            const bool audioOnly=success&&studio.pageVideoUrl().isEmpty()&&QFileInfo::exists(studio.sound()->settings()["speechResult"].toString());
            QTimer::singleShot(600,&app,[&,audioOnly]{
                const auto dir=qEnvironmentVariable("MOSS_EVIDENCE_ROOT",QString(MOSS_SOURCE_ROOT)+"/.local/evidence");
                QDir().mkpath(dir);editorWindow->grabWindow().save(dir+"/voice-only.png");app.exit(audioOnly?0:10);
            });
        });
        QTimer::singleShot(500,&studio,[&studio]{studio.generateTrack("voice");});
    }
    if(args.contains("--download-model-test")){
        if(!qEnvironmentVariableIsSet("MOSS_MODEL_ROOT")||!qEnvironmentVariableIsSet("MOSS_SETTINGS_ROOT"))return 12;
        auto argument=[&](QString key,QString fallback){const int at=args.indexOf(key);return at>=0&&at+1<args.size()?args[at+1]:fallback;};
        studio.models()->setEndpoint(argument("--model-endpoint","https://huggingface.co"));
        QObject::connect(studio.models(),&ModelManager::completed,&app,[&](bool success){qInfo()<<studio.models()->status()<<studio.models()->error();app.exit(success?0:13);});
        const auto group=argument("--model-group","voice");
        QTimer::singleShot(100,&app,[&,group]{studio.models()->requestDownload(group);studio.models()->startDownload();});
    }
    return app.exec();
}
