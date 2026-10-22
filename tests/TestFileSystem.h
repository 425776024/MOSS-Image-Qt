#pragma once

#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <filesystem>

inline bool createTestSymlink(const QString &target, const QString &link)
{
    // QFile::link creates .lnk shortcuts on Windows, not filesystem symlinks.
    std::error_code error;
    const auto source = QFile(target).filesystemFileName();
    const auto destination = QFile(link).filesystemFileName();
    if (QFileInfo(target).isDir())
        std::filesystem::create_directory_symlink(source, destination, error);
    else
        std::filesystem::create_symlink(source, destination, error);
    if (error)
        qWarning() << "Cannot create test symlink:" << QString::fromStdString(error.message());
    return !error && std::filesystem::is_symlink(destination);
}
