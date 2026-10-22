#pragma once

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <cerrno>

// Only for destinations explicitly chosen in a save dialog. Internal state
// files must continue using QSaveFile without a non-atomic fallback.
inline bool streamToUserSelectedFile(QIODevice &input, const QString &destination,
                                    QString *error)
{
    QSaveFile atomic(destination);
    QFile direct(destination);
    QIODevice *output = &atomic;
    if (!atomic.open(QIODevice::WriteOnly)) {
        const int openError = errno;
#ifdef Q_OS_MACOS
        // A save-panel sandbox extension permits the selected file, but not a
        // temporary sibling. Qt 6.10's direct fallback handles EACCES only;
        // macOS returns EPERM here. Retain atomic writes whenever possible.
        if ((openError == EPERM || openError == EACCES)
            && direct.open(QIODevice::WriteOnly)) {
            output = &direct;
        } else
#else
        Q_UNUSED(openError);
#endif
        {
            *error = atomic.errorString();
            return false;
        }
    }
    while (!input.atEnd()) {
        const QByteArray data = input.read(1024 * 1024);
        if (data.isEmpty()) {
            *error = input.errorString();
            return false;
        }
        qint64 offset = 0;
        while (offset < data.size()) {
            const auto written = output->write(data.constData() + offset, data.size() - offset);
            if (written <= 0) {
                *error = output->errorString();
                return false;
            }
            offset += written;
        }
    }
    const bool saved = output == &atomic ? atomic.commit() : direct.flush();
    if (!saved) *error = output->errorString();
    return saved;
}

inline bool copyToUserSelectedFile(const QString &source, const QString &destination,
                                  QString *error)
{
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly)) {
        *error = input.errorString();
        return false;
    }
    return streamToUserSelectedFile(input, destination, error);
}

inline bool writeToUserSelectedFile(const QByteArray &bytes, const QString &destination,
                                   QString *error)
{
    QBuffer input;
    input.setData(bytes);
    input.open(QIODevice::ReadOnly);
    return streamToUserSelectedFile(input, destination, error);
}
