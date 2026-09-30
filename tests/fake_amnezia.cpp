#include <QCoreApplication>
#include <QTimer>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTimer::singleShot(30000, &app, &QCoreApplication::quit);
    return app.exec();
}
