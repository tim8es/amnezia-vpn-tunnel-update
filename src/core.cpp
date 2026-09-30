#include "core.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace AmneziaUpdater {
namespace {

QString normalizeHostname(QString hostname)
{
    hostname = hostname.trimmed();
    for (const QString &prefix : {QStringLiteral("https://"), QStringLiteral("http://"),
                                  QStringLiteral("ftp://")}) {
        if (hostname.startsWith(prefix, Qt::CaseInsensitive)) {
            hostname.remove(0, prefix.size());
            break;
        }
    }
    const int slash = hostname.indexOf('/');
    if (slash > 0 && !hostname.contains(QRegularExpression(R"(^\d{1,3}(?:\.\d{1,3}){3}/\d{1,2}$)"))) {
        hostname = hostname.left(slash);
    }
    return hostname.trimmed();
}

bool isIpv4OrCidr(const QString &value)
{
    const auto parts = value.split('/');
    if (parts.size() > 2)
        return false;
    const auto octets = parts[0].split('.');
    if (octets.size() != 4)
        return false;
    for (const auto &octet : octets) {
        bool ok = false;
        const int n = octet.toInt(&ok);
        if (!ok || n < 0 || n > 255)
            return false;
    }
    if (parts.size() == 2) {
        bool ok = false;
        const int prefix = parts[1].toInt(&ok);
        if (!ok || prefix < 0 || prefix > 32)
            return false;
    }
    return true;
}

bool looksLikeAmneziaSite(const QString &hostname)
{
    return !hostname.isEmpty() && (hostname.contains('.') || isIpv4OrCidr(hostname));
}

QStringList variantToStringList(const QVariant &value)
{
    QStringList list = value.toStringList();
    list.removeAll(QString());
    list.removeDuplicates();
    return list;
}

bool writeBytesAtomically(const QString &path, const QByteArray &bytes, QString &error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        error = QStringLiteral("Cannot open %1 for writing: %2").arg(path, file.errorString());
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        error = QStringLiteral("Cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    if (!file.commit()) {
        error = QStringLiteral("Cannot commit %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

QString xmlEscape(QString s)
{
    s.replace('&', "&amp;");
    s.replace('<', "&lt;");
    s.replace('>', "&gt;");
    s.replace('"', "&quot;");
    s.replace('\'', "&apos;");
    return s;
}

} // namespace

bool ListCodec::parse(const QByteArray &json, ParsedList &out, QString &error)
{
    out = {};
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        error = QStringLiteral("Invalid JSON: %1").arg(parseError.errorString());
        return false;
    }
    if (!doc.isArray()) {
        error = QStringLiteral("Expected a JSON array");
        return false;
    }

    const QJsonArray array = doc.array();
    if (array.isEmpty()) {
        error = QStringLiteral("Refusing to apply an empty domain list");
        return false;
    }
    if (array.size() > 100000) {
        error = QStringLiteral("Domain list is unexpectedly large");
        return false;
    }

    QMap<QString, QStringList> sites;
    for (const QJsonValue &value : array) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        const QString hostname = normalizeHostname(object.value(QStringLiteral("hostname")).toString());
        if (!looksLikeAmneziaSite(hostname))
            continue;

        QStringList ips;
        if (object.value(QStringLiteral("ips")).isArray()) {
            for (const QJsonValue &ip : object.value(QStringLiteral("ips")).toArray()) {
                const QString text = ip.toString().trimmed();
                if (isIpv4OrCidr(text))
                    ips.append(text);
            }
        }
        const QString singleIp = object.value(QStringLiteral("ip")).toString().trimmed();
        if (!singleIp.isEmpty() && isIpv4OrCidr(singleIp))
            ips.append(singleIp);
        ips.removeDuplicates();
        sites.insert(hostname, ips);
    }

    if (sites.isEmpty()) {
        error = QStringLiteral("No valid Amnezia hostnames were found");
        return false;
    }

    out.sites = sites;
    out.sha256 = QCryptographicHash::hash(json, QCryptographicHash::Sha256).toHex();
    return true;
}

QVariantMap ListCodec::merge(const QVariantMap &current,
                             const QStringList &previousManaged,
                             const QMap<QString, QStringList> &newManaged)
{
    QVariantMap result = current;

    for (const QString &domain : previousManaged)
        result.remove(domain);

    for (auto it = newManaged.cbegin(); it != newManaged.cend(); ++it) {
        QStringList value = it.value();

        // Domain-only upstream entries intentionally keep the IPs already resolved by
        // Amnezia. New domains are stored with an empty list and Amnezia resolves them
        // when it next builds routes.
        if (value.isEmpty() && current.contains(it.key()) && previousManaged.contains(it.key()))
            value = variantToStringList(current.value(it.key()));

        result.insert(it.key(), value);
    }
    return result;
}

QByteArray ListCodec::settingsBackupJson(const QVariantMap &sites)
{
    QJsonObject root;
    for (auto it = sites.cbegin(); it != sites.cend(); ++it) {
        QJsonArray ips;
        for (const QString &ip : variantToStringList(it.value()))
            ips.append(ip);
        root.insert(it.key(), ips);
    }
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool ListCodec::settingsBackupFromJson(const QByteArray &json, QVariantMap &sites, QString &error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        error = QStringLiteral("Invalid settings backup");
        return false;
    }

    QVariantMap result;
    const QJsonObject root = doc.object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        if (!it.value().isArray()) {
            error = QStringLiteral("Invalid backup value for %1").arg(it.key());
            return false;
        }
        QStringList ips;
        for (const auto &v : it.value().toArray())
            ips.append(v.toString());
        result.insert(it.key(), ips);
    }
    sites = result;
    return true;
}

StateStore::StateStore(QString rootPath)
{
    if (rootPath.isEmpty())
        rootPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    m_rootPath = QDir::cleanPath(rootPath);
}

QString StateStore::rootPath() const { return m_rootPath; }
QString StateStore::pendingPath() const { return QDir(m_rootPath).filePath(QStringLiteral("pending.json")); }
QString StateStore::backupDir() const { return QDir(m_rootPath).filePath(QStringLiteral("backups")); }

bool StateStore::load(State &state, QString &error) const
{
    state = {};
    const QString path = QDir(m_rootPath).filePath(QStringLiteral("state.json"));
    QFile file(path);
    if (!file.exists())
        return true;
    if (!file.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("Cannot read updater state: %1").arg(file.errorString());
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        error = QStringLiteral("Updater state is corrupted");
        return false;
    }
    const QJsonObject o = doc.object();
    state.sourceSha256 = o.value(QStringLiteral("sourceSha256")).toString();
    state.etag = o.value(QStringLiteral("etag")).toString();
    state.pendingSha256 = o.value(QStringLiteral("pendingSha256")).toString();
    for (const auto &v : o.value(QStringLiteral("managedDomains")).toArray())
        state.managedDomains.append(v.toString());
    state.managedDomains.removeDuplicates();
    return true;
}

bool StateStore::save(const State &state, QString &error) const
{
    QJsonObject o;
    o.insert(QStringLiteral("version"), 1);
    o.insert(QStringLiteral("sourceSha256"), state.sourceSha256);
    o.insert(QStringLiteral("etag"), state.etag);
    o.insert(QStringLiteral("pendingSha256"), state.pendingSha256);
    QJsonArray managed;
    for (const QString &domain : state.managedDomains)
        managed.append(domain);
    o.insert(QStringLiteral("managedDomains"), managed);
    return writeBytesAtomically(QDir(m_rootPath).filePath(QStringLiteral("state.json")),
                                QJsonDocument(o).toJson(QJsonDocument::Indented), error);
}

bool StateStore::savePending(const QByteArray &json, QString &error) const
{
    return writeBytesAtomically(pendingPath(), json, error);
}

bool StateStore::loadPending(QByteArray &json, QString &error) const
{
    QFile file(pendingPath());
    if (!file.exists()) {
        error = QStringLiteral("No pending update");
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("Cannot read pending update: %1").arg(file.errorString());
        return false;
    }
    json = file.readAll();
    return true;
}

void StateStore::clearPending() const
{
    QFile::remove(pendingPath());
}

bool StateStore::saveBackup(const QVariantMap &sites, QString &pathOut, QString &error) const
{
    QDir().mkpath(backupDir());
    const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    pathOut = QDir(backupDir()).filePath(QStringLiteral("except-sites-%1.json").arg(stamp));
    return writeBytesAtomically(pathOut, ListCodec::settingsBackupJson(sites), error);
}

AmneziaSettings::AmneziaSettings()
    : m_settings(std::make_unique<QSettings>(QStringLiteral("AmneziaVPN.ORG"),
                                             QStringLiteral("AmneziaVPN")))
{
}

AmneziaSettings::AmneziaSettings(const QString &iniPath)
    : m_settings(std::make_unique<QSettings>(iniPath, QSettings::IniFormat))
{
}

bool AmneziaSettings::isInitialized() const
{
    return m_settings->contains(QStringLiteral("Conf/encrypted"))
        || m_settings->contains(QStringLiteral("Conf/routeMode"))
        || m_settings->contains(QStringLiteral("Conf/ExceptSites"))
        || m_settings->contains(QStringLiteral("Servers/serversList"));
}

QVariantMap AmneziaSettings::exceptSites() const
{
    return m_settings->value(QStringLiteral("Conf/ExceptSites")).toMap();
}

bool AmneziaSettings::setExceptSites(const QVariantMap &sites, QString &error)
{
    m_settings->setValue(QStringLiteral("Conf/ExceptSites"), sites);
    m_settings->sync();
    if (m_settings->status() != QSettings::NoError) {
        error = QStringLiteral("QSettings failed while writing Amnezia configuration");
        return false;
    }
    return true;
}

int AmneziaSettings::routeMode() const
{
    return m_settings->value(QStringLiteral("Conf/routeMode"), 0).toInt();
}

bool AmneziaSettings::splitTunnelingEnabled() const
{
    return m_settings->value(QStringLiteral("Conf/sitesSplitTunnelingEnabled"), false).toBool();
}

Updater::Updater(AmneziaSettings &settings, StateStore &stateStore, QObject *parent)
    : QObject(parent), m_settings(settings), m_stateStore(stateStore)
{
}

UpdateResult Updater::applyParsed(const QByteArray &sourceJson,
                                  const ParsedList &parsed,
                                  State state,
                                  const QString &etag)
{
    if (!m_settings.isInitialized())
        return {UpdateStatus::Error,
                QStringLiteral("Amnezia VPN settings were not found. Open Amnezia at least once first."), 0};

    const State oldState = state;
    const QVariantMap current = m_settings.exceptSites();

    QString error;
    QString backupPath;
    if (!m_stateStore.saveBackup(current, backupPath, error))
        return {UpdateStatus::Error, QStringLiteral("Backup failed: %1").arg(error), 0};

    const QVariantMap merged = ListCodec::merge(current, state.managedDomains, parsed.sites);
    if (!m_settings.setExceptSites(merged, error))
        return {UpdateStatus::Error, QStringLiteral("Amnezia settings update failed: %1").arg(error), 0};

    state.sourceSha256 = QString::fromLatin1(parsed.sha256);
    if (!etag.isEmpty())
        state.etag = etag;
    state.managedDomains = parsed.sites.keys();
    state.pendingSha256.clear();

    if (!m_stateStore.save(state, error)) {
        QString rollbackError;
        m_settings.setExceptSites(current, rollbackError);
        QString ignored;
        m_stateStore.save(oldState, ignored);
        return {UpdateStatus::Error,
                QStringLiteral("State save failed; Amnezia settings were rolled back: %1").arg(error), 0};
    }

    Q_UNUSED(sourceJson);
    m_stateStore.clearPending();
    return {UpdateStatus::Updated,
            QStringLiteral("Updated %1 managed entries. Backup: %2")
                .arg(parsed.sites.size()).arg(backupPath),
            static_cast<int>(parsed.sites.size())};
}

UpdateResult Updater::updateFromBytes(const QByteArray &json, bool amneziaRunning, const QString &etag)
{
    ParsedList parsed;
    QString error;
    if (!ListCodec::parse(json, parsed, error))
        return {UpdateStatus::Error, error, 0};

    State state;
    if (!m_stateStore.load(state, error))
        return {UpdateStatus::Error, error, 0};

    // Guard against a valid-but-broken upstream release that would otherwise
    // remove a large portion of the previously managed list automatically.
    if (state.managedDomains.size() >= 100
        && parsed.sites.size() * 2 < state.managedDomains.size()) {
        return {UpdateStatus::Error,
                QStringLiteral("Refusing suspicious update: managed list would shrink from %1 to %2 entries.")
                    .arg(state.managedDomains.size()).arg(parsed.sites.size()),
                static_cast<int>(parsed.sites.size())};
    }

    const QString hash = QString::fromLatin1(parsed.sha256);
    if (hash == state.sourceSha256 && state.pendingSha256.isEmpty())
        return {UpdateStatus::Unchanged, QStringLiteral("The domain list is already up to date."),
                static_cast<int>(parsed.sites.size())};

    if (amneziaRunning) {
        if (!m_stateStore.savePending(json, error))
            return {UpdateStatus::Error, error, 0};
        state.pendingSha256 = hash;
        if (!etag.isEmpty())
            state.etag = etag;
        if (!m_stateStore.save(state, error))
            return {UpdateStatus::Error, error, 0};
        return {UpdateStatus::Pending,
                QStringLiteral("A new list is ready and will be applied after Amnezia VPN exits."),
                static_cast<int>(parsed.sites.size())};
    }

    return applyParsed(json, parsed, state, etag);
}

UpdateResult Updater::applyPendingIfPossible(bool amneziaRunning)
{
    QString error;
    State state;
    if (!m_stateStore.load(state, error))
        return {UpdateStatus::Error, error, 0};
    if (state.pendingSha256.isEmpty())
        return {UpdateStatus::Unchanged, QStringLiteral("No pending update."), 0};
    if (amneziaRunning)
        return {UpdateStatus::Pending, QStringLiteral("Amnezia VPN is still running."), 0};

    QByteArray json;
    if (!m_stateStore.loadPending(json, error))
        return {UpdateStatus::Error, error, 0};

    ParsedList parsed;
    if (!ListCodec::parse(json, parsed, error))
        return {UpdateStatus::Error, QStringLiteral("Pending update is invalid: %1").arg(error), 0};

    if (QString::fromLatin1(parsed.sha256) != state.pendingSha256)
        return {UpdateStatus::Error, QStringLiteral("Pending update checksum does not match state."), 0};

    return applyParsed(json, parsed, state, state.etag);
}

UpdateResult Updater::updateFromNetwork(bool amneziaRunning, int timeoutMs)
{
    const UpdateResult pending = applyPendingIfPossible(amneziaRunning);
    if (pending.status == UpdateStatus::Error)
        return pending;

    QString error;
    State state;
    if (!m_stateStore.load(state, error))
        return {UpdateStatus::Error, error, 0};

    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl(QString::fromLatin1(kSourceUrl)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("User-Agent", "amnezia-vpn-tunnel-update/0.1");
    if (!state.etag.isEmpty())
        request.setRawHeader("If-None-Match", state.etag.toUtf8());

    QNetworkReply *reply = manager.get(request);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();

    if (!reply->isFinished()) {
        reply->abort();
        reply->deleteLater();
        return {UpdateStatus::Error, QStringLiteral("Download timed out."), 0};
    }

    const int httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (httpStatus == 304) {
        reply->deleteLater();
        return pending.status == UpdateStatus::Updated
            ? pending
            : UpdateResult{UpdateStatus::Unchanged, QStringLiteral("The domain list is already up to date."), 0};
    }

    if (reply->error() != QNetworkReply::NoError || httpStatus != 200) {
        const QString message = QStringLiteral("Download failed (HTTP %1): %2")
                                    .arg(httpStatus).arg(reply->errorString());
        reply->deleteLater();
        return {UpdateStatus::Error, message, 0};
    }

    const QByteArray body = reply->readAll();
    const QString etag = QString::fromUtf8(reply->rawHeader("ETag"));
    reply->deleteLater();
    return updateFromBytes(body, amneziaRunning, etag);
}

bool Updater::isAmneziaRunning()
{
    QProcess process;
#ifdef Q_OS_WIN
    process.start(QStringLiteral("tasklist"),
                  {QStringLiteral("/FI"), QStringLiteral("IMAGENAME eq AmneziaVPN.exe"),
                   QStringLiteral("/NH")});
    if (!process.waitForFinished(3000))
        return true; // conservative: never write when process state is unknown
    return QString::fromLocal8Bit(process.readAllStandardOutput())
        .contains(QStringLiteral("AmneziaVPN.exe"), Qt::CaseInsensitive);
#else
    process.start(QStringLiteral("pgrep"), {QStringLiteral("-x"), QStringLiteral("AmneziaVPN")});
    if (!process.waitForFinished(3000))
        return true;
    return process.exitCode() == 0;
#endif
}

bool Scheduler::install(const QString &programPath, QString &error)
{
#ifdef Q_OS_WIN
    const QString command = QStringLiteral("\"%1\" --update --silent")
                                .arg(QDir::toNativeSeparators(programPath));
    QProcess process;
    process.start(QStringLiteral("schtasks"),
                  {QStringLiteral("/Create"), QStringLiteral("/F"),
                   QStringLiteral("/SC"), QStringLiteral("HOURLY"),
                   QStringLiteral("/MO"), QStringLiteral("6"),
                   QStringLiteral("/TN"), QStringLiteral("Amnezia VPN Tunnel Update"),
                   QStringLiteral("/TR"), command});
    if (!process.waitForFinished(10000) || process.exitCode() != 0) {
        error = QStringLiteral("Task Scheduler registration failed: %1")
                    .arg(QString::fromLocal8Bit(process.readAllStandardError()));
        return false;
    }
    return true;
#elif defined(Q_OS_MACOS)
    const QString dir = QDir::home().filePath(QStringLiteral("Library/LaunchAgents"));
    QDir().mkpath(dir);
    const QString plist = QDir(dir).filePath(QStringLiteral("io.github.tim8es.amnezia-vpn-tunnel-update.plist"));
    const QByteArray content = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\"><dict>\n"
        "<key>Label</key><string>io.github.tim8es.amnezia-vpn-tunnel-update</string>\n"
        "<key>ProgramArguments</key><array><string>%1</string><string>--update</string>"
        "<string>--silent</string></array>\n"
        "<key>RunAtLoad</key><true/>\n"
        "<key>StartInterval</key><integer>21600</integer>\n"
        "</dict></plist>\n").arg(xmlEscape(programPath)).toUtf8();
    if (!writeBytesAtomically(plist, content, error))
        return false;

    const QString domain = QStringLiteral("gui/%1").arg(static_cast<qulonglong>(geteuid()));

    // Idempotent reinstall: unload an older registration first. Failure is
    // harmless when the job was not loaded.
    QProcess::execute(QStringLiteral("launchctl"),
                      {QStringLiteral("bootout"), domain, plist});

    QProcess process;
    process.start(QStringLiteral("launchctl"),
                  {QStringLiteral("bootstrap"), domain, plist});
    if (!process.waitForFinished(10000)) {
        error = QStringLiteral("launchctl did not finish");
        return false;
    }
    // "already bootstrapped" is harmless on reinstall.
    return process.exitCode() == 0
        || QString::fromLocal8Bit(process.readAllStandardError()).contains(QStringLiteral("already"), Qt::CaseInsensitive);
#else
    const QString userDir = QDir::home().filePath(QStringLiteral(".config/systemd/user"));
    QDir().mkpath(userDir);
    const QString service = QDir(userDir).filePath(QStringLiteral("amnezia-vpn-tunnel-update.service"));
    const QString timer = QDir(userDir).filePath(QStringLiteral("amnezia-vpn-tunnel-update.timer"));
    const QByteArray serviceBody = QStringLiteral(
        "[Unit]\nDescription=Update Amnezia split-tunneling list\n\n"
        "[Service]\nType=oneshot\nExecStart=\"%1\" --update --silent\n")
        .arg(programPath).toUtf8();
    const QByteArray timerBody = QByteArray(
        "[Unit]\nDescription=Periodically update Amnezia split-tunneling list\n\n"
        "[Timer]\nOnBootSec=2min\nOnUnitActiveSec=6h\nPersistent=true\n\n"
        "[Install]\nWantedBy=timers.target\n");
    if (!writeBytesAtomically(service, serviceBody, error)
        || !writeBytesAtomically(timer, timerBody, error))
        return false;

    if (QProcess::execute(QStringLiteral("systemctl"),
                          {QStringLiteral("--user"), QStringLiteral("daemon-reload")}) != 0
        || QProcess::execute(QStringLiteral("systemctl"),
                             {QStringLiteral("--user"), QStringLiteral("enable"),
                              QStringLiteral("--now"), QStringLiteral("amnezia-vpn-tunnel-update.timer")}) != 0) {
        error = QStringLiteral("systemd user timer registration failed");
        return false;
    }
    return true;
#endif
}

bool Scheduler::uninstall(QString &error)
{
#ifdef Q_OS_WIN
    QProcess process;
    process.start(QStringLiteral("schtasks"),
                  {QStringLiteral("/Delete"), QStringLiteral("/F"),
                   QStringLiteral("/TN"), QStringLiteral("Amnezia VPN Tunnel Update")});
    if (!process.waitForFinished(10000) || (process.exitCode() != 0
        && !QString::fromLocal8Bit(process.readAllStandardError()).contains(QStringLiteral("cannot find"), Qt::CaseInsensitive))) {
        error = QStringLiteral("Unable to remove scheduled task");
        return false;
    }
    return true;
#elif defined(Q_OS_MACOS)
    const QString plist = QDir::home().filePath(
        QStringLiteral("Library/LaunchAgents/io.github.tim8es.amnezia-vpn-tunnel-update.plist"));
    const QString domain = QStringLiteral("gui/%1").arg(static_cast<qulonglong>(geteuid()));
    QProcess::execute(QStringLiteral("launchctl"),
                      {QStringLiteral("bootout"), domain, plist});
    QFile::remove(plist);
    return true;
#else
    QProcess::execute(QStringLiteral("systemctl"),
                      {QStringLiteral("--user"), QStringLiteral("disable"),
                       QStringLiteral("--now"), QStringLiteral("amnezia-vpn-tunnel-update.timer")});
    const QString userDir = QDir::home().filePath(QStringLiteral(".config/systemd/user"));
    QFile::remove(QDir(userDir).filePath(QStringLiteral("amnezia-vpn-tunnel-update.service")));
    QFile::remove(QDir(userDir).filePath(QStringLiteral("amnezia-vpn-tunnel-update.timer")));
    if (QProcess::execute(QStringLiteral("systemctl"),
                          {QStringLiteral("--user"), QStringLiteral("daemon-reload")}) != 0) {
        error = QStringLiteral("systemd daemon-reload failed");
        return false;
    }
    return true;
#endif
}

QString Scheduler::description()
{
#ifdef Q_OS_WIN
    return QStringLiteral("Windows Task Scheduler, every 6 hours");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macOS LaunchAgent, every 6 hours");
#else
    return QStringLiteral("systemd user timer, every 6 hours");
#endif
}

} // namespace AmneziaUpdater
