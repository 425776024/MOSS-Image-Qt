#include "Document.h"
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
class DocumentTest:public QObject{
    Q_OBJECT
private slots:
    void centeredCoordinates(){
        Document d;d.setPrompt("海报");d.setLayout(true);d.addRegion(10.2,20.6,100.3,80.1,"text");
        d.updateRegion(0,"caption","Title");d.updateRegion(0,"text","你好\nMOSS");
        auto j=QJsonDocument::fromJson(d.compiledPrompt().toUtf8()).object();
        auto item=j["items"].toArray()[0].toObject();
        QCOMPARE(item["bbox"].toArray(),(QJsonArray{-492,-502,-411,-401}));
        QCOMPARE(item["text"].toString(),QString("你好\nMOSS"));
        QVERIFY(!j.contains("width"));QVERIFY(d.validationError().isEmpty());
    }
    void boundsUndoAndResize(){
        Document d;d.example();auto original=d.json();
        d.moveRegion(0,5000,-50,200,300);
        auto r=d.regions()[0].toMap();QCOMPARE(r["x"].toDouble(),824.);QCOMPARE(r["y"].toDouble(),0.);
        d.undo();QCOMPARE(d.json(),original);d.redo();QCOMPARE(d.regions()[0].toMap()["x"].toDouble(),824.);
        d.setCanvasWidth(512);QCOMPARE(d.regions()[0].toMap()["x"].toDouble(),412.);
    }
    void rejectBadProject(){
        Document d;d.example();auto original=d.json();auto bad=original;bad["width"]=1023;
        QString err;QVERIFY(!d.load(bad,&err));QCOMPARE(d.json(),original);
        bad=original;auto a=bad["regions"].toArray();auto r=a[0].toObject();r["x"]=-1;a[0]=r;bad["regions"]=a;
        QVERIFY(!d.load(bad,&err));QCOMPARE(d.json(),original);
    }
    void modeAndValidation(){
        Document d;QVERIFY(!d.validationError().isEmpty());d.setPrompt("a cat");QCOMPARE(d.compiledPrompt(),QString("a cat"));
        d.setLayout(true);QVERIFY(!d.validationError().isEmpty());d.example();QVERIFY(d.validationError().isEmpty());
    }
    void longTextAndLayoutRoundtrip(){
        Document d;d.loadTemplate("poster");
        const QString text=QString("第一行：\"引号\"与中文\n第二行：保留标点！\n")+QString(5000,QChar('a'));
        d.updateRegion(0,"text",text);d.setNegativePrompt("blurry text\nwatermark");
        d.setStyle("Watercolor.");d.setLayoutMeta("poster, illustration");
        auto input=d.compiledPrompt();auto parsed=QJsonDocument::fromJson(input.toUtf8()).object();
        QCOMPARE(parsed["items"].toArray()[0].toObject()["text"].toString(),text);
        QVERIFY(parsed["summary"].toObject()["caption"].toString().endsWith("Visual style: Watercolor."));
        Document loaded;QString err;QVERIFY(loaded.load(d.json(),&err));QCOMPARE(loaded.json(),d.json());
        QCOMPARE(loaded.negativePrompt(),QString("blurry text\nwatermark"));
        Document imported;QVERIFY(imported.importLayout(input,1024,1280).isEmpty());
        QCOMPARE(QJsonDocument::fromJson(imported.compiledPrompt().toUtf8()).object(),parsed);
    }
    void failedImportIsAtomic(){
        Document d;d.example();const auto before=d.json();
        QVERIFY(!d.importLayout("{broken",1024,1024).isEmpty());QCOMPARE(d.json(),before);
        auto input=QJsonDocument::fromJson(d.compiledPrompt().toUtf8()).object();
        auto items=input["items"].toArray();auto first=items[0].toObject();
        first["bbox"]=QJsonArray{-600,-500,100,100};items[0]=first;input["items"]=items;
        QVERIFY(!d.importLayout(QJsonDocument(input).toJson(),1024,1024).isEmpty());QCOMPARE(d.json(),before);
        d.loadTemplate("poster");d.undo();QCOMPARE(d.json(),before);
    }
    void atomicResizeAndCopy(){
        Document d;d.loadTemplate("poster");const auto before=d.json();
        d.setCanvasSize(768,1344);d.undo();QCOMPARE(d.json(),before);
        d.setSelected(0);d.duplicateSelected();QCOMPARE(d.regions().size(),4);
        QCOMPARE(d.regions().last().toMap()["text"],d.regions().first().toMap()["text"]);
        QVERIFY(d.regions().last().toMap()["id"]!=d.regions().first().toMap()["id"]);
        d.undo();QCOMPARE(d.json(),before);
        d.setCanvasSize(1000,1000);QCOMPARE(d.json(),before);
    }
};
QTEST_GUILESS_MAIN(DocumentTest)
#include "document_tests.moc"
