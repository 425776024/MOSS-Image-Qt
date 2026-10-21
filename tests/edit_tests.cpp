#include "EditDocument.h"
#include <QtTest>
#include <QTemporaryDir>
class EditTest: public QObject {
    Q_OBJECT
private slots:
    void directOutputSkipsSourcePasteAndCropsPadding(){
        QImage source(32,30,QImage::Format_ARGB32),generated(48,48,QImage::Format_ARGB32);
        source.fill(Qt::blue);generated.fill(qRgba(200,30,40,170));
        QImage weights(source.size(),QImage::Format_Grayscale8);weights.fill(0);
        QCOMPARE(EditDocument::finishImage(source,generated,weights,"feather"),source);
        QCOMPARE(EditDocument::finishImage(source,generated,weights,""),source);
        const auto direct=EditDocument::finishImage(source,generated,{},"direct");
        QCOMPARE(direct.size(),source.size());QCOMPARE(direct.pixel(0,0),generated.pixel(0,0));
        QCOMPARE(direct.pixel(31,29),generated.pixel(31,29));
        QVERIFY(EditDocument::finishImage(source,QImage(8,8,QImage::Format_RGB32),{},"direct").isNull());
    }
    void blendModeSnapshotAndLegacyDefault(){
        EditDocument document;document.m_blendMode="direct";document.m_feather=24;
        const auto state=document.snapshot();EditDocument restored;
        QVERIFY(restored.restoreSnapshot(state).isEmpty());
        QCOMPARE(restored.m_blendMode,QString("direct"));QCOMPARE(restored.m_feather,24);
        auto legacy=state;legacy.remove("blendMode");restored.restoreSnapshot(legacy);
        QCOMPARE(restored.m_blendMode,QString("feather"));
        legacy["blendMode"]="unknown";restored.restoreSnapshot(legacy);
        QCOMPARE(restored.m_blendMode,QString("feather"));
    }
    void maskEditingAndUndo(){
        QTemporaryDir dir;QImage source(81,77,QImage::Format_ARGB32);source.fill(qRgba(40,80,120,175));
        QVERIFY(source.save(dir.path()+"/original.png"));
        EditDocument d;QVERIFY(d.load(dir.path()+"/original.png").isEmpty());
        QVERIFY(!d.hasMask());d.addRectangle(10,12,40,30);QVERIFY(d.hasMask());
        const auto selected=d.mask();d.beginStroke();d.paintLine({20,25},{40,25},8,true);d.endStroke();
        QCOMPARE(qRed(d.mask().pixel(30,25)),0);d.undo();QCOMPARE(d.mask(),selected);d.redo();
        QCOMPARE(qRed(d.mask().pixel(30,25)),0);d.clearMask();QVERIFY(!d.hasMask());d.undo();QVERIFY(d.hasMask());
        auto before=d.source();QVERIFY(!d.load(dir.path()+"/missing.png").isEmpty());QCOMPARE(d.source(),before);
        QString error;QVERIFY(d.prepare(dir.path(),&error));
        QCOMPARE(QImage(dir.path()+"/init.png").size(),QSize(256,256));
        QCOMPARE(QImage(dir.path()+"/source.png").size(),QSize(81,77));
        QCOMPARE(qRed(QImage(dir.path()+"/mask.png").pixel(150,150)),0);
    }
    void outsidePixelsAndAlphaStayExact(){
        QImage original(32,32,QImage::Format_ARGB32),generated(48,48,QImage::Format_ARGB32),mask(32,32,QImage::Format_ARGB32);
        mask.fill(Qt::black);generated.fill(qRgb(250,10,20));
        for(int y=0;y<32;++y)for(int x=0;x<32;++x)original.setPixel(x,y,qRgba(x*7,y*7,80,(x+y)*4));
        for(int y=8;y<24;++y)for(int x=8;x<24;++x)mask.setPixel(x,y,qRgb(255,255,255));
        const auto weights=EditDocument::blendWeights(mask,4);
        const auto result=EditDocument::composite(original,generated,weights);
        QCOMPARE(result.size(),original.size());
        int changed=0;
        for(int y=0;y<32;++y)for(int x=0;x<32;++x){
            if(qRed(mask.pixel(x,y))==0)QCOMPARE(result.pixel(x,y),original.pixel(x,y));
            else if(result.pixel(x,y)!=original.pixel(x,y))++changed;
            QCOMPARE(qAlpha(result.pixel(x,y)),qAlpha(original.pixel(x,y)));
        }
        QCOMPARE(changed,256);
        QCOMPARE(qRed(result.pixel(16,16)),250);
        QVERIFY(qRed(result.pixel(8,16))>qRed(original.pixel(8,16)));
        QVERIFY(qRed(result.pixel(8,16))<250);
        QVERIFY(EditDocument::composite(original,QImage(8,8,QImage::Format_RGB32),weights).isNull());
    }
    void noFeatherIsHardMask(){
        QImage mask(12,12,QImage::Format_RGB32);mask.fill(Qt::black);mask.setPixel(5,5,qRgb(255,255,255));
        const auto w=EditDocument::blendWeights(mask,0);QCOMPARE(w.constScanLine(5)[5],uchar(255));QCOMPARE(w.constScanLine(5)[4],uchar(0));
    }
};
QTEST_GUILESS_MAIN(EditTest)
#include "edit_tests.moc"
