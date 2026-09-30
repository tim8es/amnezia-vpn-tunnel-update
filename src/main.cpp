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

QString statusText(const State &state, const AmneziaSettings &settings)
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

    if (Scheduler::isInstalled())
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

UpdateResult runNetworkUpdate()
{
    AmneziaSettings settings;
    StateStore stateStore;
    Updater updater(settings, stateStore);
    return updater.updateFromNetwork();
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
        updater.updateFromBytes(available.sourceJson, false, available.etag);

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

UpdateResult runUpdateWithPrompt(QWidget *parent)
{
    UpdateResult result = runNetworkUpdate();
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
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.1"));

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
        qInfo().noquote() << statusText(state, settings);
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
        const UpdateResult result = runUpdateWithPrompt(nullptr);
        if (!parser.isSet(silentOpt) || result.status == UpdateStatus::Error)
            qInfo().noquote() << result.message;
        return statusExitCode(result.status);
    }

    if (parser.isSet(installOpt)) {
        const UpdateResult result = runUpdateWithPrompt(nullptr);
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

    const auto refreshStatus = [&]() {
        State state;
        QString error;
        if (stateStore.load(state, error))
            status->setText(statusText(state, settings));
        else
            status->setText(QStringLiteral("Ошибка: %1").arg(error));

        autoUpdate->setText(Scheduler::isInstalled()
                                ? QStringLiteral("Выключить автообновление")
                                : QStringLiteral("Включить автообновление"));
    };

    const auto setBusy = [&](bool busy) {
        sourceCombo->setEnabled(!busy);
        customUrl->setEnabled(!busy);
        applySource->setEnabled(!busy);
        updateNow->setEnabled(!busy);
        autoUpdate->setEnabled(!busy);
    };

    std::function<void(QPushButton *, const QString &,
                       std::function<void(const UpdateResult &)>)> runGuiUpdate;

    runGuiUpdate = [&](QPushButton *button, const QString &busyText,
                       std::function<void(const UpdateResult &)> completion) {
        const QString originalText = button->text();
        setBusy(true);
        button->setText(busyText);

        auto finish = [&, button, originalText, completion](const UpdateResult &result) {
            button->setText(originalText);
            setBusy(false);
            refreshStatus();
            completion(result);
        };

        auto *watcher = new QFutureWatcher<UpdateResult>(&window);
        QObject::connect(watcher, &QFutureWatcher<UpdateResult>::finished, &window,
                         [&, watcher, finish, button]() {
            const UpdateResult result = watcher->result();
            watcher->deleteLater();

            if (result.status != UpdateStatus::RestartRequired) {
                finish(result);
                return;
            }

            if (!askToRestartAmnezia(&window)) {
                finish(markUpdateSkipped(result));
                return;
            }

            button->setText(QStringLiteral("Перезапускаю Amnezia VPN…"));
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

        watcher->setFuture(QtConcurrent::run(runNetworkUpdate));
    };

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
        if (Scheduler::isInstalled()) {
            autoUpdate->setEnabled(false);
            QString error;
            if (!Scheduler::uninstall(error)) {
                autoUpdate->setEnabled(true);
                QMessageBox::critical(&window, QStringLiteral("Ошибка"), error);
                return;
            }

            autoUpdate->setEnabled(true);
            refreshStatus();
            QMessageBox::information(&window, QStringLiteral("Готово"),
                                     QStringLiteral("Автообновление выключено."));
            return;
        }

        if (!persistSelectedSource())
            return;

        runGuiUpdate(autoUpdate, QStringLiteral("Включаю автообновление…"),
                     [&](const UpdateResult &result) {
            if (result.status == UpdateStatus::Error) {
                QMessageBox::critical(&window, QStringLiteral("Ошибка"), result.message);
                return;
            }

            QString error;
            QString installedProgram;
            if (!Installer::stageCurrentPackage(installedProgram, error)) {
                QMessageBox::critical(&window, QStringLiteral("Ошибка"),
                                      QStringLiteral("Не удалось установить updater:\n%1").arg(error));
                return;
            }
            if (!Scheduler::install(installedProgram, error)) {
                QMessageBox::critical(&window, QStringLiteral("Ошибка"),
                                      QStringLiteral("Не удалось включить расписание:\n%1").arg(error));
                return;
            }

            refreshStatus();
            QMessageBox::information(
                &window, QStringLiteral("Готово"),
                QStringLiteral("Автообновление включено. Список проверяется каждые 6 часов."));
        });
    });

    refreshSourceUi();
    refreshStatus();
    window.show();
    return app.exec();
}
