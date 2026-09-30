#include "core.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
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
#include <QThread>
#include <QTimer>
#include <QUrl>

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#endif

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
        error = QStringLiteral("Refusing to apply an empty list");
        return false;
    }
    if (array.size() > 100000) {
        error = QStringLiteral("List is unexpectedly large");
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
        error = QStringLiteral("No valid Amnezia entries were found");
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
    state.sourceUrl = o.value(QStringLiteral("sourceUrl")).toString();
    if (state.sourceUrl.isEmpty())
        state.sourceUrl = QString::fromLatin1(kDomainSourceUrl);
    state.sourceSha256 = o.value(QStringLiteral("sourceSha256")).toString();
    state.etag = o.value(QStringLiteral("etag")).toString();
    state.skippedSha256 = o.value(QStringLiteral("skippedSha256")).toString();
    state.sourceTransitionPending = o.value(QStringLiteral("sourceTransitionPending")).toBool(false);
    for (const auto &v : o.value(QStringLiteral("managedDomains")).toArray())
        state.managedDomains.append(v.toString());
    state.managedDomains.removeDuplicates();
    return true;
}

bool StateStore::save(const State &state, QString &error) const
{
    QJsonObject o;
    o.insert(QStringLiteral("version"), 3);
    o.insert(QStringLiteral("sourceUrl"),
             state.sourceUrl.isEmpty() ? QString::fromLatin1(kDomainSourceUrl) : state.sourceUrl);
    o.insert(QStringLiteral("sourceSha256"), state.sourceSha256);
    o.insert(QStringLiteral("etag"), state.etag);
    o.insert(QStringLiteral("skippedSha256"), state.skippedSha256);
    o.insert(QStringLiteral("sourceTransitionPending"), state.sourceTransitionPending);
    QJsonArray managed;
    for (const QString &domain : state.managedDomains)
        managed.append(domain);
    o.insert(QStringLiteral("managedDomains"), managed);
    return writeBytesAtomically(QDir(m_rootPath).filePath(QStringLiteral("state.json")),
                                QJsonDocument(o).toJson(QJsonDocument::Indented), error);
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
    // v0.2.x used pending.json while waiting for Amnezia to exit. That behavior
    // was removed: updates are now either applied immediately after an approved
    // restart or explicitly skipped.
    m_stateStore.clearPending();
}

QString Updater::sourceUrl() const
{
    State state;
    QString error;
    if (!m_stateStore.load(state, error) || state.sourceUrl.isEmpty())
        return QString::fromLatin1(kDomainSourceUrl);
    return state.sourceUrl;
}

bool Updater::validateSourceUrl(const QString &url, QString &normalizedUrl, QString &error)
{
    const QUrl parsed = QUrl::fromUserInput(url.trimmed());
    if (!parsed.isValid() || parsed.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0
        || parsed.host().isEmpty()) {
        error = QStringLiteral("Source must be a valid HTTPS URL.");
        return false;
    }

    normalizedUrl = parsed.adjusted(QUrl::NormalizePathSegments).toString(QUrl::FullyEncoded);
    return true;
}

bool Updater::setSourceUrl(const QString &url, QString &error)
{
    QString normalized;
    if (!validateSourceUrl(url, normalized, error))
        return false;

    State state;
    if (!m_stateStore.load(state, error))
        return false;

    const QString current = state.sourceUrl.isEmpty()
        ? QString::fromLatin1(kDomainSourceUrl)
        : state.sourceUrl;
    if (current == normalized)
        return true;

    state.sourceUrl = normalized;
    state.sourceSha256.clear();
    state.etag.clear();
    state.skippedSha256.clear();
    state.sourceTransitionPending = true;
    m_stateStore.clearPending();
    return m_stateStore.save(state, error);
}

bool Updater::markSkipped(const QString &sha256, const QString &etag, QString &error)
{
    if (sha256.isEmpty()) {
        error = QStringLiteral("Cannot skip an update without a source checksum.");
        return false;
    }

    State state;
    if (!m_stateStore.load(state, error))
        return false;

    state.skippedSha256 = sha256;
    if (!etag.isEmpty())
        state.etag = etag;
    m_stateStore.clearPending();
    return m_stateStore.save(state, error);
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
    state.skippedSha256.clear();
    state.sourceTransitionPending = false;

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

UpdateResult Updater::updateFromBytes(const QByteArray &json, bool amneziaRunning,
                                      const QString &etag, bool respectSkipped)
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
    // An intentional source switch is exempt for its first successful apply.
    if (!state.sourceTransitionPending
        && state.managedDomains.size() >= 100
        && parsed.sites.size() * 2 < state.managedDomains.size()) {
        return {UpdateStatus::Error,
                QStringLiteral("Refusing suspicious update: managed list would shrink from %1 to %2 entries.")
                    .arg(state.managedDomains.size()).arg(parsed.sites.size()),
                static_cast<int>(parsed.sites.size())};
    }

    const QString hash = QString::fromLatin1(parsed.sha256);
    if (hash == state.sourceSha256)
        return {UpdateStatus::Unchanged, QStringLiteral("The list is already up to date."),
                static_cast<int>(parsed.sites.size()), hash, etag};

    if (respectSkipped && hash == state.skippedSha256)
        return {UpdateStatus::Skipped, QStringLiteral("This list version was previously skipped."),
                static_cast<int>(parsed.sites.size()), hash, etag};

    if (amneziaRunning)
        return {UpdateStatus::RestartRequired,
                QStringLiteral("Amnezia VPN must be restarted before applying this list."),
                static_cast<int>(parsed.sites.size()), hash, etag, json};

    return applyParsed(json, parsed, state, etag);
}

UpdateResult Updater::updateFromNetwork(bool respectSkipped, int timeoutMs)
{
    QString error;
    State state;
    if (!m_stateStore.load(state, error))
        return {UpdateStatus::Error, error, 0};

    if (state.sourceUrl.isEmpty())
        state.sourceUrl = QString::fromLatin1(kDomainSourceUrl);

    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl(state.sourceUrl));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("User-Agent", "amnezia-vpn-tunnel-update/0.2.3");
    const bool skippedVersionPending =
        !state.skippedSha256.isEmpty() && state.skippedSha256 != state.sourceSha256;
    if (!state.etag.isEmpty() && (respectSkipped || !skippedVersionPending))
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
        if (!state.skippedSha256.isEmpty() && state.skippedSha256 != state.sourceSha256)
            return {UpdateStatus::Skipped, QStringLiteral("This list version was previously skipped."), 0,
                    state.skippedSha256, state.etag};
        return {UpdateStatus::Unchanged, QStringLiteral("The list is already up to date."), 0,
                state.sourceSha256, state.etag};
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
    return updateFromBytes(body, isAmneziaRunning(), etag, respectSkipped);
}

bool Updater::isAmneziaRunning()
{
#ifdef Q_OS_WIN
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return true; // conservative: never write when process state is unknown

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool running = false;

    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (QString::fromWCharArray(entry.szExeFile)
                    .compare(QStringLiteral("AmneziaVPN.exe"), Qt::CaseInsensitive) == 0) {
                running = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return running;
#else
    QProcess process;
    process.start(QStringLiteral("pgrep"), {QStringLiteral("-x"), QStringLiteral("AmneziaVPN")});
    if (!process.waitForFinished(3000))
        return true;
    return process.exitCode() == 0;
#endif
}

bool AmneziaProcess::stopForRestart(QString &restartTarget, QString &error, int timeoutMs)
{
#ifdef Q_OS_WIN
    QProcess pathLookup;
    pathLookup.start(QStringLiteral("powershell.exe"),
                     {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                      QStringLiteral("-Command"),
                      QStringLiteral("(Get-Process -Name AmneziaVPN -ErrorAction SilentlyContinue | "
                                     "Select-Object -First 1 -ExpandProperty Path)")});
    if (pathLookup.waitForFinished(4000) && pathLookup.exitCode() == 0)
        restartTarget = QString::fromLocal8Bit(pathLookup.readAllStandardOutput()).trimmed();

    if (restartTarget.isEmpty()) {
        const QStringList candidates = {
            qEnvironmentVariable("ProgramFiles") + QStringLiteral("/AmneziaVPN/AmneziaVPN.exe"),
            qEnvironmentVariable("ProgramFiles(x86)") + QStringLiteral("/AmneziaVPN/AmneziaVPN.exe")
        };
        for (const QString &candidate : candidates) {
            if (!candidate.startsWith('/') && QFileInfo::exists(candidate)) {
                restartTarget = QDir::cleanPath(candidate);
                break;
            }
        }
    }

    if (restartTarget.isEmpty()) {
        error = QStringLiteral("Could not determine the Amnezia VPN executable path.");
        return false;
    }

    QProcess quit;
    quit.start(QStringLiteral("taskkill"),
               {QStringLiteral("/IM"), QStringLiteral("AmneziaVPN.exe")});
    if (!quit.waitForFinished(5000) && Updater::isAmneziaRunning()) {
        error = QStringLiteral("Timed out while asking Amnezia VPN to close.");
        return false;
    }
#elif defined(Q_OS_MACOS)
    restartTarget = QStringLiteral("AmneziaVPN");
    QProcess quit;
    quit.start(QStringLiteral("/usr/bin/osascript"),
               {QStringLiteral("-e"), QStringLiteral("tell application \"AmneziaVPN\" to quit")});
    if (!quit.waitForFinished(5000) && Updater::isAmneziaRunning()) {
        error = QStringLiteral("Timed out while asking Amnezia VPN to quit.");
        return false;
    }
#else
    QProcess pidLookup;
    pidLookup.start(QStringLiteral("pgrep"),
                    {QStringLiteral("-x"), QStringLiteral("AmneziaVPN")});
    if (pidLookup.waitForFinished(3000) && pidLookup.exitCode() == 0) {
        const QString pid = QString::fromLocal8Bit(pidLookup.readLine()).trimmed();
        if (!pid.isEmpty())
            restartTarget = QFileInfo(QStringLiteral("/proc/%1/exe").arg(pid)).symLinkTarget();
    }

    if (restartTarget.isEmpty())
        restartTarget = QStandardPaths::findExecutable(QStringLiteral("AmneziaVPN"));

    if (restartTarget.isEmpty()) {
        for (const QString &candidate : {
                 QStringLiteral("/usr/local/bin/AmneziaVPN"),
                 QStringLiteral("/opt/AmneziaVPN/bin/AmneziaVPN")}) {
            if (QFileInfo::exists(candidate)) {
                restartTarget = candidate;
                break;
            }
        }
    }

    if (restartTarget.isEmpty()) {
        error = QStringLiteral("Could not determine the Amnezia VPN executable path.");
        return false;
    }

    QProcess quit;
    quit.start(QStringLiteral("pkill"),
               {QStringLiteral("-TERM"), QStringLiteral("-x"), QStringLiteral("AmneziaVPN")});
    if (!quit.waitForFinished(5000) && Updater::isAmneziaRunning()) {
        error = QStringLiteral("Timed out while asking Amnezia VPN to close.");
        return false;
    }
#endif

    QElapsedTimer timer;
    timer.start();
    while (Updater::isAmneziaRunning()) {
        if (timer.elapsed() >= timeoutMs) {
            error = QStringLiteral("Amnezia VPN did not close in time. The update was not applied.");
            return false;
        }
        QThread::msleep(250);
    }
    return true;
}

bool AmneziaProcess::startAfterRestart(const QString &restartTarget, QString &error)
{
    bool started = false;
#ifdef Q_OS_MACOS
    started = QProcess::startDetached(QStringLiteral("/usr/bin/open"),
                                      {QStringLiteral("-a"), restartTarget});
#else
    started = QProcess::startDetached(restartTarget, {});
#endif
    if (!started) {
        error = QStringLiteral("The list was processed, but Amnezia VPN could not be started again.");
        return false;
    }
    return true;
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

bool Scheduler::isInstalled()
{
#ifdef Q_OS_WIN
    QProcess process;
    process.start(QStringLiteral("schtasks"),
                  {QStringLiteral("/Query"), QStringLiteral("/TN"),
                   QStringLiteral("Amnezia VPN Tunnel Update")});
    if (!process.waitForFinished(5000))
        return false;
    return process.exitCode() == 0;
#elif defined(Q_OS_MACOS)
    return QFile::exists(QDir::home().filePath(
        QStringLiteral("Library/LaunchAgents/io.github.tim8es.amnezia-vpn-tunnel-update.plist")));
#else
    return QFile::exists(QDir(QDir::home().filePath(QStringLiteral(".config/systemd/user")))
                             .filePath(QStringLiteral("amnezia-vpn-tunnel-update.timer")));
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
