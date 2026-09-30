#include "installer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>

namespace AmneziaUpdater {
namespace {

bool copyFile(const QString &source, const QString &destination, QString &error)
{
    QDir().mkpath(QFileInfo(destination).absolutePath());
    QFile::remove(destination);
    if (!QFile::copy(source, destination)) {
        error = QStringLiteral("Cannot copy %1 to %2").arg(source, destination);
        return false;
    }
    const QFile::Permissions perms = QFile::permissions(source);
    QFile::setPermissions(destination, perms);
    return true;
}

bool copyManifestPackage(const QString &sourceDir, const QString &destinationDir,
                         QString &programPath, QString &error)
{
    QFile manifest(QDir(sourceDir).filePath(QStringLiteral("runtime.manifest")));
    if (!manifest.open(QIODevice::ReadOnly | QIODevice::Text)) {
        error = QStringLiteral(
            "This updater build is not packaged for installation (runtime.manifest is missing).");
        return false;
    }

    QDir(destinationDir).removeRecursively();
    QDir().mkpath(destinationDir);

    while (!manifest.atEnd()) {
        const QString relative = QString::fromUtf8(manifest.readLine()).trimmed();
        if (relative.isEmpty())
            continue;

        const QString clean = QDir::cleanPath(relative);
        if (clean.startsWith(QStringLiteral("../")) || QDir::isAbsolutePath(clean)) {
            error = QStringLiteral("Unsafe path in runtime.manifest: %1").arg(relative);
            return false;
        }

        const QString source = QDir(sourceDir).filePath(clean);
        const QString destination = QDir(destinationDir).filePath(clean);
        if (!QFileInfo::exists(source)) {
            error = QStringLiteral("Packaged runtime file is missing: %1").arg(clean);
            return false;
        }
        if (!copyFile(source, destination, error))
            return false;
    }

    // Keep the manifest too so the installed copy can be run interactively
    // and re-register itself later without depending on the original download.
    const QString manifestDestination =
        QDir(destinationDir).filePath(QStringLiteral("runtime.manifest"));
    if (!copyFile(manifest.fileName(), manifestDestination, error))
        return false;

    const QString exeName = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
    programPath = QDir(destinationDir).filePath(exeName);
    if (!QFileInfo::exists(programPath)) {
        error = QStringLiteral("Installed updater executable was not found after copying.");
        return false;
    }
    return true;
}

} // namespace

bool Installer::stageCurrentPackage(QString &installedProgramPath, QString &error)
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (root.isEmpty()) {
        error = QStringLiteral("Cannot resolve the per-user application data directory.");
        return false;
    }

#ifdef Q_OS_MACOS
    // macdeployqt produces a self-contained .app bundle. Copying the complete
    // bundle keeps its Frameworks and plugins next to the executable.
    QDir dir(QCoreApplication::applicationDirPath());
    if (dir.dirName() != QStringLiteral("MacOS")) {
        error = QStringLiteral("Expected to run from a packaged macOS .app bundle.");
        return false;
    }
    dir.cdUp(); // Contents
    dir.cdUp(); // *.app
    const QString sourceBundle = dir.absolutePath();
    const QString destinationBundle = QDir(root).filePath(QStringLiteral("Amnezia VPN Tunnel Update.app"));

    QDir(destinationBundle).removeRecursively();
    QDir().mkpath(QFileInfo(destinationBundle).absolutePath());

    QProcess ditto;
    ditto.start(QStringLiteral("/usr/bin/ditto"), {sourceBundle, destinationBundle});
    if (!ditto.waitForFinished(30000) || ditto.exitCode() != 0) {
        error = QStringLiteral("Failed to install the macOS app bundle: %1")
                    .arg(QString::fromLocal8Bit(ditto.readAllStandardError()));
        return false;
    }

    installedProgramPath =
        QDir(destinationBundle).filePath(QStringLiteral("Contents/MacOS/")
            + QFileInfo(QCoreApplication::applicationFilePath()).fileName());
    return QFileInfo::exists(installedProgramPath);
#elif defined(Q_OS_LINUX)
    // AppImage exposes its original file path through APPIMAGE even though the
    // running executable itself lives in a temporary mount.
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    if (!appImage.isEmpty() && QFileInfo::exists(appImage)) {
        const QString destinationDir = QDir(root).filePath(QStringLiteral("runtime"));
        QDir().mkpath(destinationDir);
        const QString destination =
            QDir(destinationDir).filePath(QStringLiteral("amnezia-vpn-tunnel-update.AppImage"));
        if (!copyFile(appImage, destination, error))
            return false;
        QFile::setPermissions(destination, QFile::permissions(destination)
            | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther);
        installedProgramPath = destination;
        return true;
    }

    return copyManifestPackage(QCoreApplication::applicationDirPath(),
                               QDir(root).filePath(QStringLiteral("runtime")),
                               installedProgramPath, error);
#else
    return copyManifestPackage(QCoreApplication::applicationDirPath(),
                               QDir(root).filePath(QStringLiteral("runtime")),
                               installedProgramPath, error);
#endif
}

} // namespace AmneziaUpdater
