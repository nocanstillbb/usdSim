#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <InspectorServer.h>   // from the submodule

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;
    const QUrl url(QStringLiteral("qrc:/test_qmlcmp/main.qml"));
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreated,
        &app,
        [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection);
    engine.load(url);


    InspectorServer inspector(&engine);
    const quint16 port = []() -> quint16 {
        const QByteArray env = qgetenv("QML_INSPECTOR_PORT");
        if (!env.isEmpty()) {
            bool ok = false; int p = env.toInt(&ok);
            if (ok && p > 0 && p < 65536) return static_cast<quint16>(p);
        }
        return 37521;
    }();
    if (!inspector.start(port))
        qWarning("InspectorServer failed to start.");

    return app.exec();
}
