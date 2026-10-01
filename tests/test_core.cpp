#include <QtTest>

#include "core.h"

#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QProcess>
#include <QCoreApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

using namespace AmneziaUpdater;

class UpdaterCoreTest : public QObject
{
    Q_OBJECT

private:
    static QByteArray listJson(std::initializer_list<QString> domains)
    {
        QJsonArray array;
        for (const QString &domain : domains) {
            QJsonObject o;
            o.insert(QStringLiteral("hostname"), domain);
            o.insert(QStringLiteral("ip"), QString());
            array.append(o);
        }
        return QJsonDocument(array).toJson(QJsonDocument::Compact);
    }

    static void initializeSettings(const QString &path, const QVariantMap &sites = {})
    {
        QSettings s(path, QSettings::IniFormat);
        s.setValue(QStringLiteral("Conf/routeMode"), 2);
        s.setValue(QStringLiteral("Conf/sitesSplitTunnelingEnabled"), true);
        s.setValue(QStringLiteral("Conf/ExceptSites"), sites);
        s.sync();
    }

private slots:
    void parseValidList()
    {
        ParsedList parsed;
        QString error;
        QVERIFY2(ListCodec::parse(listJson({QStringLiteral("example.ru"), QStringLiteral("bank.ru")}),
                                  parsed, error), qPrintable(error));
        QCOMPARE(parsed.sites.size(), 2);
        QVERIFY(parsed.sites.contains(QStringLiteral("example.ru")));
        QVERIFY(!parsed.sha256.isEmpty());
    }

    void parserRejectsBrokenAndEmptyInput()
    {
        ParsedList parsed;
        QString error;
        QVERIFY(!ListCodec::parse(QByteArrayLiteral("{broken"), parsed, error));
        QVERIFY(!error.isEmpty());

        error.clear();
        QVERIFY(!ListCodec::parse(QByteArrayLiteral("[]"), parsed, error));
        QVERIFY(error.contains(QStringLiteral("empty"), Qt::CaseInsensitive));
    }

    void parserMirrorsAmneziaHostnameRules()
    {
        const QByteArray json = R"([
          {"hostname":"localhost","ip":""},
          {"hostname":"valid.example","ip":""},
          {"hostname":"10.0.0.0/8","ip":""}
        ])";
        ParsedList parsed;
        QString error;
        QVERIFY2(ListCodec::parse(json, parsed, error), qPrintable(error));
        QVERIFY(!parsed.sites.contains(QStringLiteral("localhost")));
        QVERIFY(parsed.sites.contains(QStringLiteral("valid.example")));
        QVERIFY(parsed.sites.contains(QStringLiteral("10.0.0.0/8")));
    }

    void mergePreservesUserEntriesAndRemovesStaleManaged()
    {
        QVariantMap current;
        current.insert(QStringLiteral("old.ru"), QStringList{QStringLiteral("1.2.3.4")});
        current.insert(QStringLiteral("keep.ru"), QStringList{QStringLiteral("5.6.7.8")});
        current.insert(QStringLiteral("my-company.ru"), QStringList{QStringLiteral("9.9.9.9")});

        QMap<QString, QStringList> next;
        next.insert(QStringLiteral("keep.ru"), {});
        next.insert(QStringLiteral("new.ru"), {});

        const QVariantMap merged =
            ListCodec::merge(current,
                             {QStringLiteral("old.ru"), QStringLiteral("keep.ru")},
                             next);

        QVERIFY(!merged.contains(QStringLiteral("old.ru")));
        QVERIFY(merged.contains(QStringLiteral("new.ru")));
        QVERIFY(merged.contains(QStringLiteral("my-company.ru")));
        QCOMPARE(merged.value(QStringLiteral("keep.ru")).toStringList(),
                 QStringList{QStringLiteral("5.6.7.8")});
    }

    void backupRoundTrip()
    {
        QVariantMap original;
        original.insert(QStringLiteral("a.ru"),
                        QStringList{QStringLiteral("1.1.1.1"), QStringLiteral("2.2.2.2")});
        original.insert(QStringLiteral("custom.ru"), QStringList{});

        const QByteArray encoded = ListCodec::settingsBackupJson(original);
        QVariantMap decoded;
        QString error;
        QVERIFY2(ListCodec::settingsBackupFromJson(encoded, decoded, error), qPrintable(error));
        QCOMPARE(decoded.value(QStringLiteral("a.ru")).toStringList(),
                 original.value(QStringLiteral("a.ru")).toStringList());
        QVERIFY(decoded.contains(QStringLiteral("custom.ru")));
    }

    void updatePreservesUserDomainAndCreatesBackup()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString settingsPath = QDir(temp.path()).filePath(QStringLiteral("amnezia.ini"));
        QVariantMap current;
        current.insert(QStringLiteral("old.ru"), QStringList{QStringLiteral("1.2.3.4")});
        current.insert(QStringLiteral("personal.ru"), QStringList{QStringLiteral("8.8.8.8")});
        initializeSettings(settingsPath, current);

        StateStore store(QDir(temp.path()).filePath(QStringLiteral("state")));
        State state;
        state.managedDomains = {QStringLiteral("old.ru")};
        QString error;
        QVERIFY2(store.save(state, error), qPrintable(error));

        AmneziaSettings settings(settingsPath);
        Updater updater(settings, store);
        const UpdateResult result =
            updater.updateFromBytes(listJson({QStringLiteral("new.ru")}), false);

        QCOMPARE(result.status, UpdateStatus::Updated);
        const QVariantMap after = settings.exceptSites();
        QVERIFY(!after.contains(QStringLiteral("old.ru")));
        QVERIFY(after.contains(QStringLiteral("new.ru")));
        QVERIFY(after.contains(QStringLiteral("personal.ru")));

        const QStringList backups =
            QDir(store.backupDir()).entryList({QStringLiteral("*.json")}, QDir::Files);
        QCOMPARE(backups.size(), 1);
    }

    void unchangedHashDoesNotRewrite()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString settingsPath = QDir(temp.path()).filePath(QStringLiteral("amnezia.ini"));
        initializeSettings(settingsPath);

        const QByteArray source = listJson({QStringLiteral("same.ru")});
        ParsedList parsed;
        QString error;
        QVERIFY(ListCodec::parse(source, parsed, error));

        StateStore store(QDir(temp.path()).filePath(QStringLiteral("state")));
        State state;
        state.sourceSha256 = QString::fromLatin1(parsed.sha256);
        state.managedDomains = {QStringLiteral("same.ru")};
        QVERIFY(store.save(state, error));

        AmneziaSettings settings(settingsPath);
        Updater updater(settings, store);
        const UpdateResult result = updater.updateFromBytes(source, false);
        QCOMPARE(result.status, UpdateStatus::Unchanged);
        QVERIFY(!QDir(store.backupDir()).exists());
    }

    void runningAmneziaRequestsRestartWithoutWritingSettings()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString settingsPath = QDir(temp.path()).filePath(QStringLiteral("amnezia.ini"));
        QVariantMap current;
        current.insert(QStringLiteral("personal.ru"), QStringList{});
        initializeSettings(settingsPath, current);

        StateStore store(QDir(temp.path()).filePath(QStringLiteral("state")));
        AmneziaSettings settings(settingsPath);
        Updater updater(settings, store);

        const UpdateResult result =
            updater.updateFromBytes(listJson({QStringLiteral("new.ru")}), true, QStringLiteral("etag-1"));
        QCOMPARE(result.status, UpdateStatus::RestartRequired);
        QVERIFY(!result.sourceSha256.isEmpty());
        QCOMPARE(result.etag, QStringLiteral("etag-1"));
        QVERIFY(!result.sourceJson.isEmpty());
        QVERIFY(!QFile::exists(store.pendingPath()));
        QVERIFY(!settings.exceptSites().contains(QStringLiteral("new.ru")));
        QVERIFY(settings.exceptSites().contains(QStringLiteral("personal.ru")));
    }

    void skippedVersionStaysSkippedUntilHashChanges()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString settingsPath = QDir(temp.path()).filePath(QStringLiteral("amnezia.ini"));
        initializeSettings(settingsPath);

        StateStore store(QDir(temp.path()).filePath(QStringLiteral("state")));
        AmneziaSettings settings(settingsPath);
        Updater updater(settings, store);

        const QByteArray first = listJson({QStringLiteral("first.ru")});
        const UpdateResult available =
            updater.updateFromBytes(first, true, QStringLiteral("etag-1"));
        QCOMPARE(available.status, UpdateStatus::RestartRequired);

        QString error;
        QVERIFY2(updater.markSkipped(available.sourceSha256, available.etag, error), qPrintable(error));

        const UpdateResult same =
            updater.updateFromBytes(first, true, QStringLiteral("etag-1"));
        QCOMPARE(same.status, UpdateStatus::Skipped);

        const UpdateResult manualRetry =
            updater.updateFromBytes(first, true, QStringLiteral("etag-1"), false);
        QCOMPARE(manualRetry.status, UpdateStatus::RestartRequired);
        QCOMPARE(manualRetry.sourceSha256, available.sourceSha256);
        QVERIFY(!manualRetry.sourceJson.isEmpty());

        const UpdateResult newer =
            updater.updateFromBytes(listJson({QStringLiteral("second.ru")}), true, QStringLiteral("etag-2"));
        QCOMPARE(newer.status, UpdateStatus::RestartRequired);
        QVERIFY(newer.sourceSha256 != available.sourceSha256);
    }

    void suspiciousShrinkNeverTouchesSettings()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString settingsPath = QDir(temp.path()).filePath(QStringLiteral("amnezia.ini"));

        QVariantMap current;
        State state;
        for (int i = 0; i < 120; ++i) {
            const QString domain = QStringLiteral("managed-%1.example").arg(i);
            current.insert(domain, QStringList{});
            state.managedDomains.append(domain);
        }
        current.insert(QStringLiteral("personal.example"), QStringList{});
        initializeSettings(settingsPath, current);

        StateStore store(QDir(temp.path()).filePath(QStringLiteral("state")));
        QString error;
        QVERIFY2(store.save(state, error), qPrintable(error));

        QJsonArray tiny;
        for (int i = 0; i < 10; ++i) {
            QJsonObject o;
            o.insert(QStringLiteral("hostname"), QStringLiteral("new-%1.example").arg(i));
            o.insert(QStringLiteral("ip"), QString());
            tiny.append(o);
        }

        AmneziaSettings settings(settingsPath);
        Updater updater(settings, store);
        const UpdateResult result =
            updater.updateFromBytes(QJsonDocument(tiny).toJson(QJsonDocument::Compact), false);

        QCOMPARE(result.status, UpdateStatus::Error);
        QCOMPARE(settings.exceptSites(), current);
        QVERIFY(!QDir(store.backupDir()).exists());
    }

    void sourceDefaultsAndPersists()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString settingsPath = QDir(temp.path()).filePath(QStringLiteral("amnezia.ini"));
        initializeSettings(settingsPath);

        StateStore store(QDir(temp.path()).filePath(QStringLiteral("state")));
        AmneziaSettings settings(settingsPath);
        Updater updater(settings, store);

        QCOMPARE(updater.sourceUrl(), QString::fromLatin1(kDomainSourceUrl));

        QString error;
        QVERIFY2(updater.setSourceUrl(QString::fromLatin1(kIpLiteSourceUrl), error), qPrintable(error));

        State state;
        QVERIFY2(store.load(state, error), qPrintable(error));
        QCOMPARE(state.sourceUrl, QString::fromLatin1(kIpLiteSourceUrl));
        QVERIFY(state.sourceTransitionPending);
        QVERIFY(state.sourceSha256.isEmpty());
        QVERIFY(state.etag.isEmpty());
    }

    void sourceChangeClearsSkippedVersionButKeepsManagedOwnership()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString settingsPath = QDir(temp.path()).filePath(QStringLiteral("amnezia.ini"));
        initializeSettings(settingsPath);

        StateStore store(QDir(temp.path()).filePath(QStringLiteral("state")));
        State state;
        state.sourceUrl = QString::fromLatin1(kDomainSourceUrl);
        state.sourceSha256 = QStringLiteral("old-hash");
        state.etag = QStringLiteral("old-etag");
        state.skippedSha256 = QStringLiteral("skipped-hash");
        state.managedDomains = {QStringLiteral("old.example")};
        QString error;
        QVERIFY2(store.save(state, error), qPrintable(error));

        QDir().mkpath(store.rootPath());
        QFile legacyPending(store.pendingPath());
        QVERIFY(legacyPending.open(QIODevice::WriteOnly));
        legacyPending.write(listJson({QStringLiteral("legacy-pending.example")}));
        legacyPending.close();

        AmneziaSettings settings(settingsPath);
        Updater updater(settings, store);
        QVERIFY(!QFile::exists(store.pendingPath()));
        QVERIFY2(updater.setSourceUrl(QString::fromLatin1(kIpFullSourceUrl), error), qPrintable(error));

        State after;
        QVERIFY2(store.load(after, error), qPrintable(error));
        QCOMPARE(after.sourceUrl, QString::fromLatin1(kIpFullSourceUrl));
        QCOMPARE(after.managedDomains, QStringList{QStringLiteral("old.example")});
        QVERIFY(after.sourceSha256.isEmpty());
        QVERIFY(after.etag.isEmpty());
        QVERIFY(after.skippedSha256.isEmpty());
        QVERIFY(after.sourceTransitionPending);
    }

    void sourceTransitionReplacesManagedEntriesWithoutShrinkGuard()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString settingsPath = QDir(temp.path()).filePath(QStringLiteral("amnezia.ini"));

        QVariantMap current;
        State state;
        state.sourceUrl = QString::fromLatin1(kIpLiteSourceUrl);
        state.sourceTransitionPending = true;
        state.skippedSha256 = QStringLiteral("old-skipped-hash");
        for (int i = 0; i < 120; ++i) {
            const QString domain = QStringLiteral("old-%1.example").arg(i);
            current.insert(domain, QStringList{});
            state.managedDomains.append(domain);
        }
        current.insert(QStringLiteral("personal.example"), QStringList{});
        initializeSettings(settingsPath, current);

        StateStore store(QDir(temp.path()).filePath(QStringLiteral("state")));
        QString error;
        QVERIFY2(store.save(state, error), qPrintable(error));

        QJsonArray next;
        for (int i = 0; i < 10; ++i) {
            QJsonObject o;
            o.insert(QStringLiteral("hostname"), QStringLiteral("10.%1.0.0/16").arg(i));
            o.insert(QStringLiteral("ip"), QString());
            next.append(o);
        }

        AmneziaSettings settings(settingsPath);
        Updater updater(settings, store);
        const UpdateResult result =
            updater.updateFromBytes(QJsonDocument(next).toJson(QJsonDocument::Compact), false);

        QCOMPARE(result.status, UpdateStatus::Updated);
        const QVariantMap after = settings.exceptSites();
        QVERIFY(after.contains(QStringLiteral("personal.example")));
        QVERIFY(after.contains(QStringLiteral("10.0.0.0/16")));
        QVERIFY(!after.contains(QStringLiteral("old-0.example")));

        State saved;
        QVERIFY2(store.load(saved, error), qPrintable(error));
        QVERIFY(!saved.sourceTransitionPending);
        QVERIFY(saved.skippedSha256.isEmpty());
        QCOMPARE(saved.managedDomains.size(), 10);
    }

    void customSourceRequiresHttps()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString settingsPath = QDir(temp.path()).filePath(QStringLiteral("amnezia.ini"));
        initializeSettings(settingsPath);

        StateStore store(QDir(temp.path()).filePath(QStringLiteral("state")));
        AmneziaSettings settings(settingsPath);
        Updater updater(settings, store);

        QString error;
        QVERIFY(!updater.setSourceUrl(QStringLiteral("http://example.com/list.json"), error));
        QVERIFY(error.contains(QStringLiteral("HTTPS"), Qt::CaseInsensitive));

        error.clear();
        QVERIFY2(updater.setSourceUrl(QStringLiteral("https://example.com/list.json"), error), qPrintable(error));
        QCOMPARE(updater.sourceUrl(), QStringLiteral("https://example.com/list.json"));
    }

    void windowsDetectsRunningAmneziaProcess()
    {
#ifdef Q_OS_WIN
        const QString helper = QDir(QCoreApplication::applicationDirPath())
                                   .filePath(QStringLiteral("AmneziaVPN.exe"));
        QVERIFY2(QFile::exists(helper), qPrintable(helper));

        QProcess process;
        process.start(helper);
        QVERIFY2(process.waitForStarted(3000), qPrintable(process.errorString()));

        QTRY_VERIFY_WITH_TIMEOUT(Updater::isAmneziaRunning(), 3000);

        process.terminate();
        if (!process.waitForFinished(3000))
            process.kill();
        process.waitForFinished(3000);
        QTRY_VERIFY_WITH_TIMEOUT(!Updater::isAmneziaRunning(), 3000);
#else
        QSKIP("Windows-only process detection integration test");
#endif
    }

    void windowsGracefulStopAndRestartCycle()
    {
#ifdef Q_OS_WIN
        const QString helper = QDir(QCoreApplication::applicationDirPath())
                                   .filePath(QStringLiteral("AmneziaVPN.exe"));
        QVERIFY2(QFile::exists(helper), qPrintable(helper));

        QProcess process;
        process.start(helper);
        QVERIFY2(process.waitForStarted(3000), qPrintable(process.errorString()));
        QTRY_VERIFY_WITH_TIMEOUT(Updater::isAmneziaRunning(), 3000);

        QString restartTarget;
        QString error;
        QVERIFY2(AmneziaProcess::stopForRestart(restartTarget, error, 5000), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(!Updater::isAmneziaRunning(), 3000);
        QVERIFY(!restartTarget.isEmpty());
        QCOMPARE(QDir::cleanPath(restartTarget), QDir::cleanPath(helper));

        QVERIFY2(AmneziaProcess::startAfterRestart(restartTarget, error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(Updater::isAmneziaRunning(), 5000);

        QString cleanupTarget;
        QVERIFY2(AmneziaProcess::stopForRestart(cleanupTarget, error, 5000), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(!Updater::isAmneziaRunning(), 3000);
#else
        QSKIP("Windows-only graceful restart integration test");
#endif
    }

    void networkFetcherReturns200ExactlyOnce()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));

        QObject::connect(&server, &QTcpServer::newConnection, &server, [&]() {
            QTcpSocket *socket = server.nextPendingConnection();
            QVERIFY(socket);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket]() {
                socket->readAll();
                const QByteArray body = QByteArrayLiteral("[{\"hostname\":\"example.ru\",\"ip\":\"\"}]");
                const QByteArray response =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/json\r\n"
                    "ETag: test-etag\r\n"
                    "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
                    "Connection: close\r\n\r\n" + body;
                socket->write(response);
                socket->disconnectFromHost();
            });
        });

        NetworkFetcher fetcher;
        QSignalSpy spy(&fetcher, &NetworkFetcher::finished);
        fetcher.fetch(QStringLiteral("http://127.0.0.1:%1/list.json").arg(server.serverPort()), {}, 1000);

        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 2000);
        const QList<QVariant> args = spy.takeFirst();
        QCOMPARE(args.at(0).toBool(), true);
        QCOMPARE(args.at(1).toBool(), false);
        QVERIFY(!args.at(2).toByteArray().isEmpty());
        QCOMPARE(args.at(3).toString(), QStringLiteral("test-etag"));
        QVERIFY(args.at(4).toString().isEmpty());
    }

    void networkFetcherReturns304ExactlyOnce()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));

        QObject::connect(&server, &QTcpServer::newConnection, &server, [&]() {
            QTcpSocket *socket = server.nextPendingConnection();
            QVERIFY(socket);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket]() {
                socket->readAll();
                socket->write(
                    "HTTP/1.1 304 Not Modified\r\n"
                    "ETag: test-etag\r\n"
                    "Connection: close\r\n\r\n");
                socket->disconnectFromHost();
            });
        });

        NetworkFetcher fetcher;
        QSignalSpy spy(&fetcher, &NetworkFetcher::finished);
        fetcher.fetch(QStringLiteral("http://127.0.0.1:%1/list.json").arg(server.serverPort()),
                      QStringLiteral("test-etag"), 1000);

        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 2000);
        const QList<QVariant> args = spy.takeFirst();
        QCOMPARE(args.at(0).toBool(), true);
        QCOMPARE(args.at(1).toBool(), true);
        QVERIFY(args.at(2).toByteArray().isEmpty());
        QCOMPARE(args.at(3).toString(), QStringLiteral("test-etag"));
    }

    void networkFetcherTimesOutExactlyOnce()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));

        QList<QTcpSocket *> heldSockets;
        QObject::connect(&server, &QTcpServer::newConnection, &server, [&]() {
            while (server.hasPendingConnections()) {
                QTcpSocket *socket = server.nextPendingConnection();
                QVERIFY(socket);
                heldSockets.append(socket);
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket]() {
                    socket->readAll(); // Intentionally never respond.
                });
            }
        });

        NetworkFetcher fetcher;
        QSignalSpy spy(&fetcher, &NetworkFetcher::finished);
        fetcher.fetch(QStringLiteral("http://127.0.0.1:%1/hang").arg(server.serverPort()), {}, 100);

        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 1500);
        const QList<QVariant> args = spy.at(0);
        QCOMPARE(args.at(0).toBool(), false);
        QVERIFY(args.at(4).toString().contains(QStringLiteral("timed out"), Qt::CaseInsensitive));

        QTest::qWait(250);
        QCOMPARE(spy.count(), 1);

        for (QTcpSocket *socket : heldSockets)
            socket->deleteLater();
    }

    void invalidUpdateNeverTouchesSettings()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString settingsPath = QDir(temp.path()).filePath(QStringLiteral("amnezia.ini"));
        QVariantMap current;
        current.insert(QStringLiteral("personal.ru"), QStringList{});
        initializeSettings(settingsPath, current);

        StateStore store(QDir(temp.path()).filePath(QStringLiteral("state")));
        AmneziaSettings settings(settingsPath);
        Updater updater(settings, store);

        const UpdateResult result = updater.updateFromBytes(QByteArrayLiteral("not-json"), false);
        QCOMPARE(result.status, UpdateStatus::Error);
        QCOMPARE(settings.exceptSites(), current);
        QVERIFY(!QDir(store.backupDir()).exists());
    }

    void unknownAmneziaSettingsFailSafe()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString settingsPath = QDir(temp.path()).filePath(QStringLiteral("empty.ini"));

        StateStore store(QDir(temp.path()).filePath(QStringLiteral("state")));
        AmneziaSettings settings(settingsPath);
        Updater updater(settings, store);

        const UpdateResult result =
            updater.updateFromBytes(listJson({QStringLiteral("example.ru")}), false);
        QCOMPARE(result.status, UpdateStatus::Error);
        QVERIFY(!settings.exceptSites().contains(QStringLiteral("example.ru")));
    }
};

QTEST_GUILESS_MAIN(UpdaterCoreTest)
#include "test_core.moc"
