#include "core.h"
#include "installer.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QComboBox>
#include <QCoreApplication>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QWidget>

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

    if (!state.pendingSha256.isEmpty())
        lines << QStringLiteral("• Есть обновление, ожидающее закрытия Amnezia VPN");

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

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("tim8es"));
    QCoreApplication::setApplicationName(QStringLiteral("amnezia-vpn-tunnel-update"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.0"));

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

    QObject::connect(sourceCombo, &QComboBox::currentIndexChanged, &window, [&](int) {
        refreshSourceUi();
    });

    QObject::connect(applySource, &QPushButton::clicked, &window, [&]() {
        if (!persistSelectedSource())
            return;

        applySource->setEnabled(false);
        const UpdateResult result = updater.updateFromNetwork(Updater::isAmneziaRunning());
        applySource->setEnabled(true);
        refreshStatus();

        if (result.status == UpdateStatus::Error)
            QMessageBox::critical(&window, QStringLiteral("Ошибка"), result.message);
        else
            QMessageBox::information(&window, QStringLiteral("Источник применён"), result.message);
    });

    QObject::connect(updateNow, &QPushButton::clicked, &window, [&]() {
        if (!persistSelectedSource())
            return;

        updateNow->setEnabled(false);
        const UpdateResult result = updater.updateFromNetwork(Updater::isAmneziaRunning());
        updateNow->setEnabled(true);
        refreshStatus();

        if (result.status == UpdateStatus::Error)
            QMessageBox::critical(&window, QStringLiteral("Ошибка"), result.message);
        else
            QMessageBox::information(&window, QStringLiteral("Готово"), result.message);
    });

    QObject::connect(autoUpdate, &QPushButton::clicked, &window, [&]() {
        autoUpdate->setEnabled(false);

        if (Scheduler::isInstalled()) {
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

        if (!persistSelectedSource()) {
            autoUpdate->setEnabled(true);
            return;
        }

        const UpdateResult result = updater.updateFromNetwork(Updater::isAmneziaRunning());
        if (result.status == UpdateStatus::Error) {
            autoUpdate->setEnabled(true);
            QMessageBox::critical(&window, QStringLiteral("Ошибка"), result.message);
            return;
        }

        QString error;
        QString installedProgram;
        if (!Installer::stageCurrentPackage(installedProgram, error)) {
            autoUpdate->setEnabled(true);
            QMessageBox::critical(&window, QStringLiteral("Ошибка"),
                                  QStringLiteral("Список обновлён, но не удалось установить updater:\n%1")
                                      .arg(error));
            return;
        }
        if (!Scheduler::install(installedProgram, error)) {
            autoUpdate->setEnabled(true);
            QMessageBox::critical(&window, QStringLiteral("Ошибка"),
                                  QStringLiteral("Список обновлён, но не удалось включить расписание:\n%1")
                                      .arg(error));
            return;
        }

        autoUpdate->setEnabled(true);
        refreshStatus();
        QMessageBox::information(
            &window, QStringLiteral("Готово"),
            QStringLiteral("%1\n\nДальше выбранный список проверяется автоматически.")
                .arg(result.message));
    });

    refreshSourceUi();
    refreshStatus();
    window.show();
    return app.exec();
}
