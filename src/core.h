#pragma once

#include <QByteArray>
#include <QMap>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>

#include <memory>

namespace AmneziaUpdater {

inline constexpr const char *kDomainSourceUrl =
    "https://github.com/lib4u/amnezia-tunneling-ru/releases/download/latest/amnezia.json";
inline constexpr const char *kIpLiteSourceUrl =
    "https://github.com/lib4u/amnezia-tunneling-ru/releases/download/latest/amnezia-ip-lite.json";
inline constexpr const char *kIpFullSourceUrl =
    "https://github.com/lib4u/amnezia-tunneling-ru/releases/download/latest/amnezia-ip.json";

struct ParsedList {
    QMap<QString, QStringList> sites;
    QByteArray sha256;
};

struct State {
    QString sourceUrl;
    QString sourceSha256;
    QString etag;
    QString skippedSha256;
    QStringList managedDomains;
    bool sourceTransitionPending = false;
};

enum class UpdateStatus {
    Updated,
    Unchanged,
    RestartRequired,
    Skipped,
    Error
};

struct UpdateResult {
    UpdateStatus status = UpdateStatus::Error;
    QString message;
    int managedCount = 0;
    QString sourceSha256;
    QString etag;
    QByteArray sourceJson;
};

class ListCodec {
public:
    static bool parse(const QByteArray &json, ParsedList &out, QString &error);
    static QVariantMap merge(const QVariantMap &current,
                             const QStringList &previousManaged,
                             const QMap<QString, QStringList> &newManaged);
    static QByteArray settingsBackupJson(const QVariantMap &sites);
    static bool settingsBackupFromJson(const QByteArray &json, QVariantMap &sites, QString &error);
};

class StateStore {
public:
    explicit StateStore(QString rootPath = {});

    QString rootPath() const;
    QString pendingPath() const;
    QString backupDir() const;

    bool load(State &state, QString &error) const;
    bool save(const State &state, QString &error) const;
    void clearPending() const;
    bool saveBackup(const QVariantMap &sites, QString &pathOut, QString &error) const;

private:
    QString m_rootPath;
};

class AmneziaSettings {
public:
    AmneziaSettings();
    explicit AmneziaSettings(const QString &iniPath);

    bool isInitialized() const;
    QVariantMap exceptSites() const;
    bool setExceptSites(const QVariantMap &sites, QString &error);
    int routeMode() const;
    bool splitTunnelingEnabled() const;

private:
    std::unique_ptr<QSettings> m_settings;
};

class Updater : public QObject {
    Q_OBJECT
public:
    explicit Updater(AmneziaSettings &settings,
                     StateStore &stateStore,
                     QObject *parent = nullptr);

    QString sourceUrl() const;
    bool setSourceUrl(const QString &url, QString &error);
    bool markSkipped(const QString &sha256, const QString &etag, QString &error);
    static bool validateSourceUrl(const QString &url, QString &normalizedUrl, QString &error);

    UpdateResult updateFromBytes(const QByteArray &json, bool amneziaRunning,
                                 const QString &etag = {});
    UpdateResult updateFromNetwork(int timeoutMs = 20000);

    static bool isAmneziaRunning();

private:
    UpdateResult applyParsed(const QByteArray &sourceJson,
                             const ParsedList &parsed,
                             State state,
                             const QString &etag);
    AmneziaSettings &m_settings;
    StateStore &m_stateStore;
};

class AmneziaProcess {
public:
    static bool stopForRestart(QString &restartTarget, QString &error, int timeoutMs = 15000);
    static bool startAfterRestart(const QString &restartTarget, QString &error);
};

class Scheduler {
public:
    static bool install(const QString &programPath, QString &error);
    static bool uninstall(QString &error);
    static bool isInstalled();
    static QString description();
};

} // namespace AmneziaUpdater
