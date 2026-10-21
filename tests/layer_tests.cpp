#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonArray>
#include "LayerDocument.h"

class LayerTests:public QObject {
    Q_OBJECT
    QUrl image(QTemporaryDir &dir,QString name,QColor color){
        const auto path=dir.filePath(name+".png");QImage image(32,32,QImage::Format_ARGB32);image.fill(color);
        if(!image.save(path))return {};return QUrl::fromLocalFile(path);
    }
    QUrl svg(QTemporaryDir &dir){
        const auto path=dir.filePath("vector.svg");QFile f(path);if(!f.open(QIODevice::WriteOnly))return {};
        f.write("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\" viewBox=\"0 0 100 100\"><rect width=\"50\" height=\"100\" fill=\"#00ff00\"/></svg>");
        f.close();return QUrl::fromLocalFile(path);
    }
private slots:
    void alphaAndStack(){
        QTemporaryDir dir;LayerDocument d;d.setCanvasSize(256,256);
        QVERIFY(d.importFiles({image(dir,"red",Qt::red),image(dir,"blue",Qt::blue)}));
        QCOMPARE(d.composite().pixelColor(128,128),QColor(Qt::blue));
        d.updateLayer(1,"opacity",.5);auto c=d.composite().pixelColor(128,128);
        // Qt's 8-bit raster opacity path rounds through a 256-level factor.
        QVERIFY2(std::abs(c.red()-127)<=2,qPrintable(c.name(QColor::HexArgb)));QVERIFY(std::abs(c.blue()-128)<=2);QCOMPARE(c.alpha(),255);
        d.updateLayer(1,"visible",false);QCOMPARE(d.composite().pixelColor(128,128),QColor(Qt::red));
        d.updateLayer(1,"visible",true);d.moveSelected(-1);QCOMPARE(d.composite().pixelColor(128,128),QColor(Qt::red));
        d.undo();QVERIFY(d.composite().pixelColor(128,128).blue()>100);
    }
    void vectorAndPortableProject(){
        QTemporaryDir dir;LayerDocument d;d.setCanvasSize(512,512);auto source=svg(dir);
        QVERIFY(d.importFiles({source}));QCOMPARE(d.layers()[0].toMap()["kind"].toString(),QString("svg"));
        QCOMPARE(d.composite().pixelColor(100,200),QColor(Qt::green));
        QCOMPARE(d.composite().pixelColor(400,200).alpha(),0);
        d.updateLayer(0,"rotation",180);
        QCOMPARE(d.composite().pixelColor(100,200).alpha(),0);
        QCOMPARE(d.composite().pixelColor(400,200),QColor(Qt::green));
        const auto before=d.composite();const auto project=QUrl::fromLocalFile(dir.filePath("portable.mosslayers.json"));
        QVERIFY(d.saveProject(project));QVERIFY(QFile::remove(source.toLocalFile()));
        LayerDocument restored;QVERIFY(restored.loadProject(project));QCOMPARE(restored.composite(),before);
        const auto png=QUrl::fromLocalFile(dir.filePath("out.png"));QVERIFY(restored.exportPng(png));
        QCOMPARE(QImage(png.toLocalFile()).convertToFormat(before.format()),before);
    }
    void transformUndoAndAtomicFailure(){
        QTemporaryDir dir;LayerDocument d;QVERIFY(d.importFiles({image(dir,"red",Qt::red)}));
        const auto before=d.entries()[0].rect;
        d.beginTransform();d.transformSelected({25,30,200,100});d.transformSelected({35,40,150,200});d.endTransform();
        d.undo();QCOMPARE(d.entries()[0].rect,before);
        d.redo();QCOMPARE(d.entries()[0].rect,QRectF(35,40,150,200));
        auto snapshot=d.composite();
        QVERIFY(!d.importFiles({image(dir,"blue",Qt::blue),QUrl::fromLocalFile(dir.filePath("missing.svg"))}));
        QCOMPARE(d.entries().size(),1);QCOMPARE(d.composite(),snapshot);
        auto invalid=d.json();auto layers=invalid["layers"].toArray();auto l=layers[0].toObject();l["width"]=-1;layers[0]=l;invalid["layers"]=layers;
        QVERIFY(!d.load(invalid));QCOMPARE(d.composite(),snapshot);
        d.setCanvasSize(9000,9000);QCOMPARE(d.canvasWidth(),1024);
    }
    void duplicateSharesAssetAndBackground(){
        QTemporaryDir dir;LayerDocument d;d.setCanvasSize(256,256);QVERIFY(d.importFiles({svg(dir)}));
        d.duplicateSelected();QCOMPARE(d.entries().size(),2);QCOMPARE(d.json()["assets"].toArray().size(),1);
        d.updateLayer(0,"visible",false);d.updateLayer(1,"visible",false);QVERIFY(!d.hasContent());
        d.setBackground("#ffffff");QCOMPARE(d.composite().pixelColor(200,200),QColor(Qt::white));
        d.updateLayer(1,"visible",true);QVERIFY(d.hasContent());
        d.removeSelected();QCOMPARE(d.entries().size(),1);d.undo();QCOMPARE(d.entries().size(),2);
    }
    void svgOpacityIsForWholeLayer(){
        QTemporaryDir dir;QFile f(dir.filePath("overlap.svg"));QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\"><rect width=\"100\" height=\"100\" fill=\"red\"/><rect width=\"50\" height=\"100\" fill=\"blue\"/></svg>");
        f.close();LayerDocument d;d.setCanvasSize(256,256);QVERIFY(d.importFiles({QUrl::fromLocalFile(f.fileName())}));
        d.updateLayer(0,"opacity",.5);auto im=d.composite();
        QVERIFY2(std::abs(im.pixelColor(10,10).alpha()-128)<=2,qPrintable(im.pixelColor(10,10).name(QColor::HexArgb)));
        QCOMPARE(im.pixelColor(10,10).alpha(),im.pixelColor(200,10).alpha());
    }
};
QTEST_GUILESS_MAIN(LayerTests)
#include "layer_tests.moc"
