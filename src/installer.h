#pragma once

#include <QString>

namespace AmneziaUpdater {

class Installer {
public:
    // Copies the packaged updater to a stable user-owned location and returns
    // the executable path that should be registered with the OS scheduler.
    static bool stageCurrentPackage(QString &installedProgramPath, QString &error);
};

} // namespace AmneziaUpdater
