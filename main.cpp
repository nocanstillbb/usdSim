#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <InspectorServer.h>
#include "UsdDocument.h"
#include "UsdViewportItem.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Basic");

    qmlRegisterType<UsdDocument>("UsdBrowser", 1, 0, "UsdDocument");
    qmlRegisterType<UsdViewportItem>("UsdBrowser", 1, 0, "UsdViewport");

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
