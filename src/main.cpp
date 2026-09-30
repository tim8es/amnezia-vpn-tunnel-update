#include "core.h"
#include "installer.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QWidget>

using namespace AmneziaUpdater;

namespace {

int statusExitCode(UpdateStatus status)
{
    return status == UpdateStatus::Error ? 1 : 0;
}

QString statusText(const State &state, const AmneziaSettings &settings)
{
    QStringList lines;
    lines << (settings.isInitialized()
                  ? QStringLiteral("✓ Amnezia VPN найдена")
                  : QStringLiteral("✗ Настройки Amnezia VPN не найдены"));

    if (!state.sourceSha256.isEmpty())
        lines << QStringLiteral("✓ Автообновляемый список уже применялся");
    else
        lines << QStringLiteral("• Список ещё не синхронизирован");

    if (!state.pendingSha256.isEmpty())
        lines << QStringLiteral("• Есть обновление, ожидающее закрытия Amnezia VPN");

    if (settings.isInitialized()) {
        if (settings.routeMode() == 2 && settings.splitTunnelingEnabled())
            lines << QStringLiteral("✓ Режим: всё через VPN, кроме списка");
        else
            lines << QStringLiteral("• Updater не меняет режим split tunneling автоматически");
    }

    lines << QStringLiteral("• Проверка: %1").arg(Scheduler::description());
    return lines.join('\n');
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("tim8es"));
    QCoreApplication::setApplicationName(QStringLiteral("amnezia-vpn-tunnel-update"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Independent automatic split-tunneling list updater for Amnezia VPN"));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption updateOpt(QStringLiteral("update"), QStringLiteral("Check and apply an update"));
    const QCommandLineOption statusOpt(QStringLiteral("status"), QStringLiteral("Print updater status"));
    const QCommandLineOption installOpt(QStringLiteral("install"), QStringLiteral("Enable scheduled updates"));
    const QCommandLineOption uninstallOpt(QStringLiteral("uninstall"), QStringLiteral("Disable scheduled updates"));
    const QCommandLineOption silentOpt(QStringLiteral("silent"), QStringLiteral("Do not show UI messages"));
    const QCommandLineOption validateOpt(QStringLiteral("validate-file"),
                                         QStringLiteral("Validate an Amnezia JSON list"),
                                         QStringLiteral("path"));
    parser.addOptions({updateOpt, statusOpt, installOpt, uninstallOpt, silentOpt, validateOpt});
    parser.process(app);

    AmneziaSettings settings;
    StateStore stateStore;
    Updater updater(settings, stateStore);

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
        const UpdateResult result = updater.updateFromNetwork(Updater::isAmneziaRunning());
        if (!parser.isSet(silentOpt) || result.status == UpdateStatus::Error)
            qInfo().noquote() << result.message;
        return statusExitCode(result.status);
    }

    if (parser.isSet(installOpt)) {
        const UpdateResult result = updater.updateFromNetwork(Updater::isAmneziaRunning());
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
    window.setMinimumWidth(440);

    auto *layout = new QVBoxLayout(&window);
    auto *title = new QLabel(QStringLiteral("<b>Amnezia VPN Tunnel Update</b>"));
    auto *description = new QLabel(
        QStringLiteral("Независимая утилита. Источник списка: lib4u/amnezia-tunneling-ru.<br>"
                       "Updater не меняет VPN-сервер, протокол или режим маршрутизации."));
    description->setWordWrap(true);
    auto *status = new QLabel;
    status->setWordWrap(true);
    auto *enable = new QPushButton(QStringLiteral("Включить автообновление"));
    auto *updateNow = new QPushButton(QStringLiteral("Обновить сейчас"));

    layout->addWidget(title);
    layout->addWidget(description);
    layout->addSpacing(8);
    layout->addWidget(status);
    layout->addSpacing(8);
    layout->addWidget(enable);
    layout->addWidget(updateNow);

    const auto refreshStatus = [&]() {
        State state;
        QString error;
        if (stateStore.load(state, error))
            status->setText(statusText(state, settings));
        else
            status->setText(QStringLiteral("Ошибка: %1").arg(error));
    };

    QObject::connect(updateNow, &QPushButton::clicked, &window, [&]() {
        updateNow->setEnabled(false);
        const UpdateResult result = updater.updateFromNetwork(Updater::isAmneziaRunning());
        updateNow->setEnabled(true);
        refreshStatus();
        QMessageBox::information(&window,
                                 result.status == UpdateStatus::Error ? QStringLiteral("Ошибка")
                                                                      : QStringLiteral("Готово"),
                                 result.message);
    });

    QObject::connect(enable, &QPushButton::clicked, &window, [&]() {
        enable->setEnabled(false);
        const UpdateResult result = updater.updateFromNetwork(Updater::isAmneziaRunning());
        if (result.status == UpdateStatus::Error) {
            enable->setEnabled(true);
            QMessageBox::critical(&window, QStringLiteral("Ошибка"), result.message);
            return;
        }

        QString error;
        QString installedProgram;
        if (!Installer::stageCurrentPackage(installedProgram, error)) {
            enable->setEnabled(true);
            QMessageBox::critical(&window, QStringLiteral("Ошибка"),
                                  QStringLiteral("Список обновлён, но не удалось установить updater:\n%1")
                                      .arg(error));
            return;
        }
        if (!Scheduler::install(installedProgram, error)) {
            enable->setEnabled(true);
            QMessageBox::critical(&window, QStringLiteral("Ошибка"),
                                  QStringLiteral("Список обновлён, но не удалось включить расписание:\n%1")
                                      .arg(error));
            return;
        }
        enable->setText(QStringLiteral("Автообновление включено"));
        refreshStatus();
        QMessageBox::information(
            &window, QStringLiteral("Готово"),
            QStringLiteral("%1\n\nДальше список проверяется автоматически.")
                .arg(result.message));
    });

    refreshStatus();
    window.show();
    return app.exec();
}
