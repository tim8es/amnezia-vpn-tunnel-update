#include <QApplication>
#include <QLabel>
#include <QWidget>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QWidget window;
    window.setWindowTitle(QStringLiteral("AmneziaVPN test process"));
    auto *label = new QLabel(QStringLiteral("Test Amnezia process"), &window);
    label->move(20, 20);
    window.resize(240, 100);
    window.show();

    return app.exec();
}
