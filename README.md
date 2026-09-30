<p align="center">
  <img src="assets/amnezia-vpn-tunnel-update.svg" width="96" alt="Amnezia VPN Tunnel Update icon">
</p>

<h1 align="center">Amnezia VPN Tunnel Update</h1>

<p align="center">
  Автоматическое обновление списка раздельного туннелирования Amnezia VPN без изменения клиента Amnezia.
</p>

<p align="center">
  <a href="https://github.com/tim8es/amnezia-vpn-tunnel-update/actions/workflows/ci.yml"><img src="https://github.com/tim8es/amnezia-vpn-tunnel-update/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="MIT License"></a>
  <img src="https://img.shields.io/badge/C%2B%2B-17-00599C.svg" alt="C++17">
  <img src="https://img.shields.io/badge/Qt-6.5%2B-41CD52.svg" alt="Qt 6.5+">
</p>

<p align="center">
  Русский · <a href="README.en.md">English</a>
</p>

> [!IMPORTANT]
> Это независимый неофициальный проект. Он не является частью Amnezia VPN и не связан с командой Amnezia VPN. Подробнее: [NOTICE.md](NOTICE.md).

## Что это

**Amnezia VPN Tunnel Update** поддерживает список исключений split tunneling в актуальном состоянии автоматически.

По умолчанию используется публичный автообновляемый список `amnezia.json`. В интерфейсе можно выбрать один из трёх готовых источников или подписаться на совместимый JSON по своей HTTPS-ссылке.

| Список | Содержимое | Для чего |
| --- | --- | --- |
| `amnezia.json` | 2000+ доменов и локальные зоны | Точечный обход известных сервисов |
| `amnezia-ip-lite.json` | ~650 IPv4/CIDR-подсетей | Компактный IP-список |
| `amnezia-ip.json` | 12 800+ IPv4/CIDR-подсетей | Максимальное IP-покрытие |

Утилита не модифицирует Amnezia VPN, не устанавливает драйверы и не работает постоянным фоновым процессом.

## Как это работает

1. Утилита скачивает выбранный JSON-список.
2. Проверяет формат и защитные условия.
3. Сравнивает SHA-256 с уже применённой или пропущенной версией.
4. Если Amnezia VPN закрыта — применяет обновление сразу.
5. Если Amnezia VPN открыта — предлагает **«Перезапустить сейчас» / «Пропустить»**.
6. При подтверждении корректно перезапускает Amnezia VPN, создаёт backup и обновляет `Conf/ExceptSites`.
7. При **«Пропустить»** эта версия больше не показывается; вопрос появится снова только для нового SHA-256.
8. ОС запускает короткую проверку раз в 6 часов.

| ОС | Автообновление |
| --- | --- |
| Windows | Task Scheduler |
| macOS | LaunchAgent |
| Linux | systemd user timer |

**Admin/root не требуется.** Updater не ждёт будущего закрытия Amnezia и не держит отдельный фоновый процесс.

## Установка

### Стабильные версии

Используйте страницу [Releases](https://github.com/tim8es/amnezia-vpn-tunnel-update/releases) для опубликованных версий.

### Windows

Пакет: portable `.zip`.

1. Распакуйте архив.
2. Запустите `amnezia-vpn-tunnel-update.exe`.
3. При необходимости выберите источник списка.
4. Нажмите **«Включить автообновление»**.

Эта же кнопка затем превращается в **«Выключить автообновление»**.

Пока сборки не подписаны, SmartScreen может показать предупреждение.

### macOS

Пакет: `.dmg`.

1. Откройте DMG.
2. Запустите **Amnezia VPN Tunnel Update**.
3. При необходимости выберите источник списка.
4. Нажмите **«Включить автообновление»**.

Пока сборки не подписаны и не notarized, Gatekeeper может потребовать ручное подтверждение запуска.

### Linux

Пакет: x86_64 `.AppImage`.

```bash
chmod +x amnezia-vpn-tunnel-update-linux-x86_64.AppImage
./amnezia-vpn-tunnel-update-linux-x86_64.AppImage
```

## CLI

```text
amnezia-vpn-tunnel-update --install
amnezia-vpn-tunnel-update --update --silent
amnezia-vpn-tunnel-update --source https://example.com/list.json --update
amnezia-vpn-tunnel-update --status
amnezia-vpn-tunnel-update --uninstall
amnezia-vpn-tunnel-update --validate-file amnezia.json
```

Без аргументов открывается GUI.

## Пользовательские записи

Updater хранит идентичность доменов и IP/CIDR-записей, которыми управляет сам:

```text
user entries = current Amnezia entries - previous managed set
result       = user entries + new managed set
```

Поэтому обычные пользовательские исключения не должны исчезать после обновления выбранного источника.

Текущее ограничение модели: hostname является единицей владения. Если пользователь вручную изменил IP-список у hostname, который также управляется upstream, этот hostname всё равно может считаться updater-managed.

## Сборка из исходников

Требования:

- CMake 3.21+;
- C++17;
- Qt 6.5+ — Core, Network, Widgets, Test.

```bash
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build -C Release --output-on-failure
```

Проверка совместимости с форматом списка:

```bash
./build/amnezia-vpn-tunnel-update --validate-file amnezia.json
```

## CI и релизы

CI собирает и тестирует проект на Windows, macOS и Linux. Linux-job дополнительно скачивает и валидирует реальным бинарником все три встроенных upstream-списка.

Release workflow запускается изменением `.github/RELEASE`: он проверяет версию, создаёт тег, собирает платформенные пакеты и SHA-256 checksums. Процесс описан в [docs/RELEASING.md](docs/RELEASING.md).

## Документация проекта

| Документ | Назначение |
| --- | --- |
| [PRIVACY.md](PRIVACY.md) | Какие данные читает/отправляет приложение |
| [SUPPORT.md](SUPPORT.md) | Куда обращаться с проблемами |
| [CHANGELOG.md](CHANGELOG.md) | История изменений |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Архитектура и trust boundaries |
| [docs/RELEASING.md](docs/RELEASING.md) | Процесс релиза |

## Участие в разработке

Issues и pull requests приветствуются.

Для проблем с **самим содержимым списка доменов** используйте upstream-репозиторий: [lib4u/amnezia-tunneling-ru](https://github.com/lib4u/amnezia-tunneling-ru).

## Privacy

В проекте нет намеренно добавленной аналитики или телеметрии. Updater обращается к GitHub для загрузки актуального списка. Подробности: [PRIVACY.md](PRIVACY.md).

## Лицензия

Код распространяется по лицензии [MIT](LICENSE).

Copyright © 2026 [tim8es](https://github.com/tim8es).
