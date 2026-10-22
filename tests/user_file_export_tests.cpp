#include "UserSelectedFileExport.h"
#include <QTemporaryDir>
#include <QtTest>

class UserFileExportTests : public QObject {
    Q_OBJECT
private slots:
    void exportsAndReplacesCompleteFile() {
        QTemporaryDir dir;
        const auto source=dir.filePath("source.wav"), destination=dir.filePath("export.wav");
        QByteArray expected(3*1024*1024+17, 'x');
        QFile input(source);QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write(expected),expected.size());input.close();
        QString error;
        QVERIFY2(copyToUserSelectedFile(source,destination,&error),qPrintable(error));
        QFile result(destination);QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(result.readAll(),expected);result.close();
        QVERIFY(input.open(QIODevice::WriteOnly));input.close();
        QVERIFY2(copyToUserSelectedFile(source,destination,&error),qPrintable(error));
        QCOMPARE(QFileInfo(destination).size(),0);
    }
    void writesUtf8ProjectAndReportsFailure() {
        QTemporaryDir dir;QString error;
        const QByteArray data=QString::fromUtf8("{\"name\":\"测试工程\"}").toUtf8();
        const auto path=dir.filePath("project.moss.json");
        QVERIFY2(writeToUserSelectedFile(data,path,&error),qPrintable(error));
        QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),data);
        QVERIFY(!writeToUserSelectedFile(data,dir.filePath("missing/project.moss.json"),&error));
        QVERIFY(!error.isEmpty());
    }
    void missingSourceDoesNotTruncateDestination() {
        QTemporaryDir dir;
        const auto destination=dir.filePath("existing.wav");
        QFile file(destination);QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("keep");file.close();
        QString error;
        QVERIFY(!copyToUserSelectedFile(dir.filePath("missing"),destination,&error));
        QVERIFY(!error.isEmpty());QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(),QByteArray("keep"));
    }
    void invalidDestinationReturnsError() {
        QTemporaryDir dir;QFile source(dir.filePath("source"));
        QVERIFY(source.open(QIODevice::WriteOnly));source.write("content");source.close();
        QString error;
        QVERIFY(!copyToUserSelectedFile(source.fileName(),dir.filePath("missing/export"),&error));
        QVERIFY(!error.isEmpty());
    }
};
QTEST_GUILESS_MAIN(UserFileExportTests)
#include "user_file_export_tests.moc"
