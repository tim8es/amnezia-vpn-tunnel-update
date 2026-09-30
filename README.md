# Amnezia VPN Tunnel Update

Независимая кроссплатформенная утилита, которая автоматически поддерживает список
раздельного туннелирования **Amnezia VPN** в актуальном состоянии.

> Это отдельный неофициальный проект. Он не является частью Amnezia VPN и не требует
> изменений клиента Amnezia.

Проект полностью независим по коду, сборкам и обновлению самой утилиты.
Текущий **источник данных** — публичный автообновляемый релиз
`lib4u/amnezia-tunneling-ru` (его можно будет заменить без переноса проекта):

`https://github.com/lib4u/amnezia-tunneling-ru/releases/download/latest/amnezia.json`

## Для пользователя

1. Скачайте сборку для своей ОС.
2. Запустите **Amnezia VPN Tunnel Update**.
3. Нажмите **«Включить автообновление»**.
4. Готово.

После установки утилита не висит в фоне. ОС запускает короткую проверку раз в 6 часов:

- Windows — Task Scheduler;
- macOS — LaunchAgent;
- Linux — systemd user timer.

Установка работает без admin/root.

## Что делает утилита

- скачивает актуальный список из upstream;
- валидирует JSON до любых изменений;
- сравнивает SHA-256 и не переписывает настройки без необходимости;
- обновляет `Conf/ExceptSites` через Qt `QSettings`, тем же форматом, который использует Amnezia;
- сохраняет домены, добавленные пользователем вручную;
- удаляет домены, которые были удалены из upstream;
- блокирует подозрительное массовое сокращение upstream-списка;
- создаёт backup перед каждым применением;
- не меняет VPN-сервер, протокол, `routeMode` или включение split tunneling;
- если Amnezia запущена, ничего не пишет в её настройки — сохраняет pending update и применяет его после закрытия клиента;
- при неизвестной/неинициализированной конфигурации работает fail-safe: ничего не меняет.

## CLI

```text
amnezia-vpn-tunnel-update --install
amnezia-vpn-tunnel-update --update --silent
amnezia-vpn-tunnel-update --status
amnezia-vpn-tunnel-update --uninstall
amnezia-vpn-tunnel-update --validate-file amnezia.json
```

Без аргументов открывается минимальный GUI с одной основной кнопкой.

## Логика сохранения пользовательских доменов

Updater хранит список записей, которыми управляет сам:

```text
user entries = current Amnezia list - previous managed set
result       = user entries + new managed set
```

Поэтому пользовательские исключения не пропадают при очередном обновлении upstream.

## Сборка

Требуются CMake 3.21+ и Qt 6.5+ (Core, Network, Widgets, Test).

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

GitHub Actions собирает и тестирует Windows, macOS и Linux. Linux job дополнительно
скачивает текущий upstream `amnezia.json` и проверяет его реальным бинарником.

## Статус

Это самостоятельный репозиторий утилиты. Он не зависит от структуры upstream-проекта.
Репозиторий `lib4u/amnezia-tunneling-ru` используется только как внешний источник актуального списка.

## Подпись релизов

Тестовые артефакты GitHub Actions пока не подписаны. Для публичного standalone-релиза
желательно добавить Windows code signing и macOS signing/notarization, чтобы убрать
SmartScreen/Gatekeeper-предупреждения и сохранить максимально простой UX.
