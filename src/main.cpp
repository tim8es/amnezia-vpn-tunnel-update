#include "core.h"
#include "installer.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QComboBox>
#include <QCoreApplication>
#include <QFile>
#include <QFutureWatcher>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrent>

#include <functional>
#include <optional>

using namespace AmneziaUpdater;

namespace {

int statusExitCode(UpdateStatus status)
{
    return status == UpdateStatus::Error ? 1 : 0;
}

QString sourceDisplayName(const QString &url)
{
    if (url == QString::fromLatin1(kDomainSourceUrl))
        return QStringLiteral("Домены — amnezia.json");
    if (url == QString::fromLatin1(kIpLiteSourceUrl))
        return QStringLiteral("IP Lite — amnezia-ip-lite.json");
    if (url == QString::fromLatin1(kIpFullSourceUrl))
        return QStringLiteral("IP Full — amnezia-ip.json");
    return url;
}

QString sourceDescription(int index)
{
    switch (index) {
    case 0:
        return QStringLiteral("Домены российских сервисов и локальные зоны. "
                              "Точечный вариант для обычного использования.");
    case 1:
        return QStringLiteral("Компактный IPv4/CIDR-список. "
                              "Подходит, если нужен IP-маршрутинг без полного списка.");
    case 2:
        return QStringLiteral("Полный IPv4/CIDR-список российского сегмента. "
                              "Максимальное покрытие, но заметно больше записей.");
    default:
        return QStringLiteral("HTTPS-ссылка на совместимый JSON-массив Amnezia "
                              "с полями hostname и, при необходимости, ips/ip.");
    }
}

QString statusText(const State &state, const AmneziaSettings &settings,
                   bool schedulerStateKnown, bool schedulerInstalled)
{
    QStringList lines;
    lines << (settings.isInitialized()
                  ? QStringLiteral("✓ Amnezia VPN найдена")
                  : QStringLiteral("✗ Настройки Amnezia VPN не найдены"));

    const QString source = state.sourceUrl.isEmpty()
        ? QString::fromLatin1(kDomainSourceUrl)
        : state.sourceUrl;
    lines << QStringLiteral("• Источник: %1").arg(sourceDisplayName(source));

    if (!state.sourceSha256.isEmpty())
        lines << QStringLiteral("✓ Список уже синхронизировался");
    else
        lines << QStringLiteral("• Список ещё не синхронизирован");

    if (!state.skippedSha256.isEmpty() && state.skippedSha256 != state.sourceSha256)
        lines << QStringLiteral("• Последняя найденная версия списка пропущена");

    if (settings.isInitialized()) {
        if (settings.routeMode() == 2 && settings.splitTunnelingEnabled())
            lines << QStringLiteral("✓ Режим: всё через VPN, кроме списка");
        else
            lines << QStringLiteral("• Updater не меняет режим split tunneling автоматически");
    }

    if (!schedulerStateKnown)
        lines << QStringLiteral("• Проверяю состояние автообновления…");
    else if (schedulerInstalled)
        lines << QStringLiteral("✓ Автообновление включено: %1").arg(Scheduler::description());
    else
        lines << QStringLiteral("• Автообновление выключено");

    return lines.join('\n');
}

bool askToRestartAmnezia(QWidget *parent)
{
    QMessageBox box(parent);
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle(QStringLiteral("Обновление списка туннелирования"));
    box.setText(QStringLiteral("Обнаружено обновление списка туннелирования для Amnezia VPN."));
    box.setInformativeText(
        QStringLiteral("Для применения нового списка необходимо перезапустить Amnezia VPN. "
                       "Текущее VPN-соединение будет временно прервано."));

    auto *restartButton =
        box.addButton(QStringLiteral("Перезапустить сейчас"), QMessageBox::AcceptRole);
    box.addButton(QStringLiteral("Пропустить"), QMessageBox::RejectRole);
    box.setDefaultButton(qobject_cast<QPushButton *>(restartButton));
    box.exec();

    return box.clickedButton() == restartButton;
}

UpdateResult runNetworkUpdate(bool respectSkipped)
{
    AmneziaSettings settings;
    StateStore stateStore;
    Updater updater(settings, stateStore);
    return updater.updateFromNetwork(respectSkipped);
}

UpdateResult processFetchedList(const QByteArray &body, const QString &etag, bool respectSkipped)
{
    AmneziaSettings settings;
    StateStore stateStore;
    Updater updater(settings, stateStore);
    return updater.updateFromBytes(body, Updater::isAmneziaRunning(), etag, respectSkipped);
}

UpdateResult markUpdateSkipped(const UpdateResult &available)
{
    AmneziaSettings settings;
    StateStore stateStore;
    Updater updater(settings, stateStore);

    QString error;
    if (!updater.markSkipped(available.sourceSha256, available.etag, error))
        return {UpdateStatus::Error, error, 0};

    return {UpdateStatus::Skipped,
            QStringLiteral("Эта версия списка пропущена."),
            available.managedCount,
            available.sourceSha256,
            available.etag};
}

UpdateResult restartAmneziaAndApply(const UpdateResult &available)
{
    if (available.sourceJson.isEmpty())
        return {UpdateStatus::Error, QStringLiteral("Проверенный список для применения недоступен."), 0};

    QString restartTarget;
    QString error;
    if (!AmneziaProcess::stopForRestart(restartTarget, error))
        return {UpdateStatus::Error, error, 0};

    AmneziaSettings settings;
    StateStore stateStore;
    Updater updater(settings, stateStore);
    UpdateResult result =
        updater.updateFromBytes(available.sourceJson, false, available.etag, false);

    QString restartError;
    if (!AmneziaProcess::startAfterRestart(restartTarget, restartError)) {
        if (result.status == UpdateStatus::Error) {
            result.message += QStringLiteral("\n\nКроме того, не удалось снова запустить Amnezia VPN: %1")
                                  .arg(restartError);
            return result;
        }
        return {UpdateStatus::Error,
                QStringLiteral("%1\n\nСписок обработан, но не удалось снова запустить Amnezia VPN: %2")
                    .arg(result.message, restartError),
                result.managedCount};
    }

    return result;
}


UpdateResult enableAutomaticUpdates()
{
    QString error;
    QString installedProgram;
    if (!Installer::stageCurrentPackage(installedProgram, error))
        return {UpdateStatus::Error, QStringLiteral("Не удалось установить updater:\n%1").arg(error), 0};

    if (!Scheduler::install(installedProgram, error))
        return {UpdateStatus::Error, QStringLiteral("Не удалось включить расписание:\n%1").arg(error), 0};

    return {UpdateStatus::Updated,
            QStringLiteral("Автообновление включено. Список проверяется каждые 6 часов."),
            0};
}

UpdateResult disableAutomaticUpdates()
{
    QString error;
    if (!Scheduler::uninstall(error))
        return {UpdateStatus::Error, error, 0};

    return {UpdateStatus::Updated, QStringLiteral("Автообновление выключено."), 0};
}

UpdateResult runUpdateWithPrompt(QWidget *parent, bool respectSkipped)
{
    UpdateResult result = runNetworkUpdate(respectSkipped);
    if (result.status != UpdateStatus::RestartRequired)
        return result;

    if (!askToRestartAmnezia(parent))
        return markUpdateSkipped(result);

    return restartAmneziaAndApply(result);
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("tim8es"));
    QCoreApplication::setApplicationName(QStringLiteral("amnezia-vpn-tunnel-update"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.3"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Independent automatic split-tunneling list updater for Amnezia VPN"));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption updateOpt(QStringLiteral("update"), QStringLiteral("Check and apply an update"));
    const QCommandLineOption statusOpt(QStringLiteral("status"), QStringLiteral("Print updater status"));
    const QCommandLineOption installOpt(QStringLiteral("install"), QStringLiteral("Enable scheduled updates"));
    const QCommandLineOption uninstallOpt(QStringLiteral("uninstall"), QStringLiteral("Disable scheduled updates"));
    const QCommandLineOption silentOpt(QStringLiteral("silent"), QStringLiteral("Suppress routine console output"));
    const QCommandLineOption sourceOpt(QStringLiteral("source"),
                                       QStringLiteral("Use and persist an HTTPS JSON source"),
                                       QStringLiteral("url"));
    const QCommandLineOption validateOpt(QStringLiteral("validate-file"),
                                         QStringLiteral("Validate an Amnezia JSON list"),
                                         QStringLiteral("path"));
    parser.addOptions({updateOpt, statusOpt, installOpt, uninstallOpt, silentOpt, sourceOpt, validateOpt});
    parser.process(app);

    AmneziaSettings settings;
    StateStore stateStore;
    Updater updater(settings, stateStore);

    if (parser.isSet(sourceOpt)) {
        QString error;
        if (!updater.setSourceUrl(parser.value(sourceOpt), error)) {
            qCritical().noquote() << error;
            return 1;
        }
    }

    if (parser.isSet(validateOpt)) {
        QFile file(parser.value(validateOpt));
        if (!file.open(QIODevice::ReadOnly)) {
            qCritical().noquote() << "Cannot read" << file.fileName();
            return 1;
        }
        ParsedList parsed;
        QString error;
        if (!ListCodec::parse(file.readAll(), parsed, error)) {
            qCritical().noquote() << error;
            return 1;
        }
        qInfo().noquote() << QStringLiteral("Valid: %1 entries").arg(parsed.sites.size());
        return 0;
    }

    if (parser.isSet(statusOpt)) {
        State state;
        QString error;
        if (!stateStore.load(state, error)) {
            qCritical().noquote() << error;
            return 1;
        }
        qInfo().noquote() << statusText(state, settings, true, Scheduler::isInstalled());
        return 0;
    }

    if (parser.isSet(uninstallOpt)) {
        QString error;
        if (!Scheduler::uninstall(error)) {
            qCritical().noquote() << error;
            return 1;
        }
        qInfo() << "Automatic updates disabled.";
        return 0;
    }

    if (parser.isSet(updateOpt)) {
        const UpdateResult result = runUpdateWithPrompt(nullptr, parser.isSet(silentOpt));
        if (!parser.isSet(silentOpt) || result.status == UpdateStatus::Error)
            qInfo().noquote() << result.message;
        return statusExitCode(result.status);
    }

    if (parser.isSet(installOpt)) {
        const UpdateResult result = runUpdateWithPrompt(nullptr, false);
        if (result.status == UpdateStatus::Error) {
            qCritical().noquote() << result.message;
            return 1;
        }

        QString error;
        QString installedProgram;
        if (!Installer::stageCurrentPackage(installedProgram, error)) {
            qCritical().noquote() << error;
            return 1;
        }
        if (!Scheduler::install(installedProgram, error)) {
            qCritical().noquote() << error;
            return 1;
        }
        qInfo().noquote() << result.message;
        qInfo().noquote() << QStringLiteral("Automatic updates enabled: %1").arg(Scheduler::description());
        return 0;
    }

    QWidget window;
    window.setWindowTitle(QStringLiteral("Amnezia VPN Tunnel Update"));
    window.setMinimumWidth(520);

    auto *layout = new QVBoxLayout(&window);
    auto *title = new QLabel(QStringLiteral("<b>Amnezia VPN Tunnel Update</b>"));
    auto *description = new QLabel(
        QStringLiteral("Автоматически поддерживает список split tunneling Amnezia VPN в актуальном состоянии."));
    description->setWordWrap(true);

    auto *sourceTitle = new QLabel(QStringLiteral("<b>Источник списка</b>"));
    auto *sourceCombo = new QComboBox;
    sourceCombo->addItem(QStringLiteral("Домены — amnezia.json"), QString::fromLatin1(kDomainSourceUrl));
    sourceCombo->addItem(QStringLiteral("IP Lite — amnezia-ip-lite.json"), QString::fromLatin1(kIpLiteSourceUrl));
    sourceCombo->addItem(QStringLiteral("IP Full — amnezia-ip.json"), QString::fromLatin1(kIpFullSourceUrl));
    sourceCombo->addItem(QStringLiteral("Своя JSON-ссылка"), QString());

    auto *sourceHelp = new QLabel;
    sourceHelp->setWordWrap(true);
    auto *customUrl = new QLineEdit;
    customUrl->setPlaceholderText(QStringLiteral("https://example.com/amnezia.json"));
    customUrl->setClearButtonEnabled(true);
    auto *applySource = new QPushButton(QStringLiteral("Применить источник"));

    auto *status = new QLabel;
    status->setWordWrap(true);
    auto *operationStatus = new QLabel;
    operationStatus->setWordWrap(true);
    operationStatus->hide();

    auto *restartPrompt = new QLabel;
    restartPrompt->setWordWrap(true);
    restartPrompt->setTextFormat(Qt::RichText);
    restartPrompt->setText(
        QStringLiteral("<b>Обнаружено обновление списка туннелирования для Amnezia VPN.</b><br><br>"
                       "Для применения нового списка необходимо перезапустить Amnezia VPN. "
                       "Текущее VPN-соединение будет временно прервано."));
    restartPrompt->hide();

    auto *restartNow = new QPushButton(QStringLiteral("Перезапустить сейчас"));
    auto *skipRestart = new QPushButton(QStringLiteral("Пропустить"));
    restartNow->hide();
    skipRestart->hide();

    auto *autoUpdate = new QPushButton;
    auto *updateNow = new QPushButton(QStringLiteral("Обновить сейчас"));

    layout->addWidget(title);
    layout->addWidget(description);
    layout->addSpacing(10);
    layout->addWidget(sourceTitle);
    layout->addWidget(sourceCombo);
    layout->addWidget(sourceHelp);
    layout->addWidget(customUrl);
    layout->addWidget(applySource);
    layout->addSpacing(10);
    layout->addWidget(status);
    layout->addWidget(operationStatus);
    layout->addWidget(restartPrompt);
    layout->addWidget(restartNow);
    layout->addWidget(skipRestart);
    layout->addSpacing(8);
    layout->addWidget(autoUpdate);
    layout->addWidget(updateNow);

    const QString savedSource = updater.sourceUrl();
    int sourceIndex = sourceCombo->findData(savedSource);
    if (sourceIndex < 0) {
        sourceIndex = 3;
        customUrl->setText(savedSource);
    }
    sourceCombo->setCurrentIndex(sourceIndex);

    const auto refreshSourceUi = [&]() {
        const bool custom = sourceCombo->currentIndex() == 3;
        customUrl->setVisible(custom);
        sourceHelp->setText(sourceDescription(sourceCombo->currentIndex()));
    };

    const auto selectedSource = [&]() {
        if (sourceCombo->currentIndex() == 3)
            return customUrl->text().trimmed();
        return sourceCombo->currentData().toString();
    };

    const auto persistSelectedSource = [&]() -> bool {
        QString error;
        if (!updater.setSourceUrl(selectedSource(), error)) {
            QMessageBox::critical(&window, QStringLiteral("Некорректный источник"), error);
            return false;
        }
        return true;
    };

    bool operationBusy = false;
    bool schedulerStateKnown = false;
    bool schedulerInstalled = false;

    const auto refreshStatus = [&]() {
        State state;
        QString error;
        if (stateStore.load(state, error))
            status->setText(statusText(state, settings, schedulerStateKnown, schedulerInstalled));
        else
            status->setText(QStringLiteral("Ошибка: %1").arg(error));

        if (!schedulerStateKnown)
            autoUpdate->setText(QStringLiteral("Проверяю автообновление…"));
        else
            autoUpdate->setText(schedulerInstalled
                                    ? QStringLiteral("Выключить автообновление")
                                    : QStringLiteral("Включить автообновление"));

        autoUpdate->setEnabled(!operationBusy && schedulerStateKnown);
    };

    const auto setBusy = [&](bool busy, const QString &message = {}) {
        operationBusy = busy;
        applySource->setEnabled(!busy);
        updateNow->setEnabled(!busy);
        autoUpdate->setEnabled(!busy && schedulerStateKnown);

        if (busy && !message.isEmpty()) {
            operationStatus->setText(message);
            operationStatus->show();
        } else if (!busy) {
            operationStatus->clear();
            operationStatus->hide();
        }
    };

    std::optional<UpdateResult> pendingRestartResult;
    std::function<void(const UpdateResult &)> pendingRestartFinish;

    const auto hideRestartPrompt = [&]() {
        restartPrompt->hide();
        restartNow->hide();
        skipRestart->hide();
    };

    std::function<void(QPushButton *, const QString &,
                       std::function<void(const UpdateResult &)>)> runGuiUpdate;

    runGuiUpdate = [&](QPushButton *button, const QString &busyText,
                       std::function<void(const UpdateResult &)> completion) {
        Q_UNUSED(button);
        setBusy(true, busyText);

        auto finish = [&, completion](const UpdateResult &result) {
            setBusy(false);
            refreshStatus();
            completion(result);
        };

        auto handleResult = [&, finish](const UpdateResult &result) {
            if (result.status != UpdateStatus::RestartRequired) {
                finish(result);
                return;
            }

            operationStatus->setText(QStringLiteral("Требуется перезапуск Amnezia VPN."));
            operationStatus->show();
            pendingRestartResult = result;
            pendingRestartFinish = finish;
            restartPrompt->show();
            restartNow->show();
            skipRestart->show();
            restartNow->setFocus(Qt::OtherFocusReason);
        };

        State fetchState;
        QString stateError;
        if (!stateStore.load(fetchState, stateError)) {
            finish({UpdateStatus::Error, stateError, 0});
            return;
        }

        const QString sourceUrl = fetchState.sourceUrl.isEmpty()
            ? QString::fromLatin1(kDomainSourceUrl)
            : fetchState.sourceUrl;
        const bool skippedVersionPending =
            !fetchState.skippedSha256.isEmpty()
            && fetchState.skippedSha256 != fetchState.sourceSha256;
        const QString requestEtag = skippedVersionPending ? QString() : fetchState.etag;

        auto *fetcher = new NetworkFetcher(&window);
        QObject::connect(fetcher, &NetworkFetcher::finished, &window,
                         [&, fetcher, fetchState, handleResult]
                         (bool ok, bool notModified, const QByteArray &body,
                          const QString &responseEtag, const QString &error) {
            fetcher->deleteLater();

            if (!ok) {
                handleResult({UpdateStatus::Error, error, 0});
                return;
            }

            if (notModified) {
                handleResult({UpdateStatus::Unchanged,
                              QStringLiteral("The list is already up to date."),
                              0, fetchState.sourceSha256,
                              responseEtag.isEmpty() ? fetchState.etag : responseEtag});
                return;
            }

            auto *processWatcher = new QFutureWatcher<UpdateResult>(&window);
            QObject::connect(processWatcher, &QFutureWatcher<UpdateResult>::finished, &window,
                             [processWatcher, handleResult]() {
                const UpdateResult result = processWatcher->result();
                processWatcher->deleteLater();
                handleResult(result);
            });
            processWatcher->setFuture(QtConcurrent::run([body, responseEtag]() {
                return processFetchedList(body, responseEtag, false);
            }));
        });

        fetcher->fetch(sourceUrl, requestEtag, 15000);
    };

    QObject::connect(skipRestart, &QPushButton::clicked, &window, [&]() {
        if (!pendingRestartResult || !pendingRestartFinish)
            return;

        const UpdateResult result = *pendingRestartResult;
        const auto finish = pendingRestartFinish;
        pendingRestartResult.reset();
        pendingRestartFinish = {};
        hideRestartPrompt();
        finish(markUpdateSkipped(result));
    });

    QObject::connect(restartNow, &QPushButton::clicked, &window, [&]() {
        if (!pendingRestartResult || !pendingRestartFinish)
            return;

        const UpdateResult result = *pendingRestartResult;
        const auto finish = pendingRestartFinish;
        pendingRestartResult.reset();
        pendingRestartFinish = {};
        hideRestartPrompt();

        operationStatus->setText(QStringLiteral("Перезапускаю Amnezia VPN…"));
        operationStatus->show();

        auto *restartWatcher = new QFutureWatcher<UpdateResult>(&window);
        QObject::connect(restartWatcher, &QFutureWatcher<UpdateResult>::finished, &window,
                         [restartWatcher, finish]() {
            const UpdateResult restarted = restartWatcher->result();
            restartWatcher->deleteLater();
            finish(restarted);
        });
        restartWatcher->setFuture(QtConcurrent::run([result]() {
            return restartAmneziaAndApply(result);
        }));
    });

    QObject::connect(sourceCombo, &QComboBox::currentIndexChanged, &window, [&](int) {
        refreshSourceUi();
    });

    QObject::connect(applySource, &QPushButton::clicked, &window, [&]() {
        if (!persistSelectedSource())
            return;

        runGuiUpdate(applySource, QStringLiteral("Проверяю список…"),
                     [&](const UpdateResult &result) {
            if (result.status == UpdateStatus::Error) {
                QMessageBox::critical(&window, QStringLiteral("Ошибка"), result.message);
            } else if (result.status != UpdateStatus::Skipped) {
                QMessageBox::information(&window, QStringLiteral("Источник применён"), result.message);
            }
        });
    });

    QObject::connect(updateNow, &QPushButton::clicked, &window, [&]() {
        if (!persistSelectedSource())
            return;

        runGuiUpdate(updateNow, QStringLiteral("Проверяю обновление…"),
                     [&](const UpdateResult &result) {
            if (result.status == UpdateStatus::Error) {
                QMessageBox::critical(&window, QStringLiteral("Ошибка"), result.message);
            } else if (result.status != UpdateStatus::Skipped) {
                QMessageBox::information(&window, QStringLiteral("Готово"), result.message);
            }
        });
    });

    QObject::connect(autoUpdate, &QPushButton::clicked, &window, [&]() {
        if (!schedulerStateKnown || operationBusy)
            return;

        if (schedulerInstalled) {
            setBusy(true, QStringLiteral("Выключаю автообновление…"));
            auto *watcher = new QFutureWatcher<UpdateResult>(&window);
            QObject::connect(watcher, &QFutureWatcher<UpdateResult>::finished, &window, [&, watcher]() {
                const UpdateResult result = watcher->result();
                watcher->deleteLater();

                if (result.status != UpdateStatus::Error)
                    schedulerInstalled = false;

                setBusy(false);
                refreshStatus();

                if (result.status == UpdateStatus::Error)
                    QMessageBox::critical(&window, QStringLiteral("Ошибка"), result.message);
                else
                    QMessageBox::information(&window, QStringLiteral("Готово"), result.message);
            });
            watcher->setFuture(QtConcurrent::run(disableAutomaticUpdates));
            return;
        }

        if (!persistSelectedSource())
            return;

        runGuiUpdate(autoUpdate, QStringLiteral("Проверяю список…"),
                     [&](const UpdateResult &result) {
            if (result.status == UpdateStatus::Error) {
                QMessageBox::critical(&window, QStringLiteral("Ошибка"), result.message);
                return;
            }

            setBusy(true, QStringLiteral("Включаю автообновление…"));
            auto *watcher = new QFutureWatcher<UpdateResult>(&window);
            QObject::connect(watcher, &QFutureWatcher<UpdateResult>::finished, &window, [&, watcher]() {
                const UpdateResult installResult = watcher->result();
                watcher->deleteLater();

                if (installResult.status != UpdateStatus::Error)
                    schedulerInstalled = true;

                setBusy(false);
                refreshStatus();

                if (installResult.status == UpdateStatus::Error)
                    QMessageBox::critical(&window, QStringLiteral("Ошибка"), installResult.message);
                else
                    QMessageBox::information(&window, QStringLiteral("Готово"), installResult.message);
            });
            watcher->setFuture(QtConcurrent::run(enableAutomaticUpdates));
        });
    });

    refreshSourceUi();
    refreshStatus();

    auto *schedulerWatcher = new QFutureWatcher<bool>(&window);
    QObject::connect(schedulerWatcher, &QFutureWatcher<bool>::finished, &window, [&, schedulerWatcher]() {
        schedulerInstalled = schedulerWatcher->result();
        schedulerStateKnown = true;
        schedulerWatcher->deleteLater();
        refreshStatus();
    });
    schedulerWatcher->setFuture(QtConcurrent::run([]() {
        return Scheduler::isInstalled();
    }));

    window.show();
    return app.exec();
}
