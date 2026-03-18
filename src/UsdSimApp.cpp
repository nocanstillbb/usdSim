#include "UsdSimApp.h"
#include <QApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <InspectorServer.h>
#include "UsdDocument.h"
#include "UsdViewportItem.h"

UsdSimApp::UsdSimApp(QObject *parent)
    : QObject(parent)
{
}

UsdSimApp::~UsdSimApp()
{
    uninit();
}

void UsdSimApp::register_types()
{
    qmlRegisterType<UsdDocument>("UsdBrowser", 1, 0, "UsdDocument");
    qmlRegisterType<UsdViewportItem>("UsdBrowser", 1, 0, "UsdViewport");
}

void UsdSimApp::init(int argc, char *argv[])
{
    // Copy argv into stable storage so QGuiApplication's references remain valid
    m_argc = argc;
    m_argStorage.clear();
    m_argStorage.reserve(argc);
    for (int i = 0; i < argc; ++i)
        m_argStorage.emplace_back(argv[i]);

    m_argv = std::make_unique<char*[]>(argc + 1);
    for (int i = 0; i < argc; ++i)
        m_argv[i] = m_argStorage[i].data();
    m_argv[argc] = nullptr;

    m_app = new QApplication(m_argc, m_argv.get());
    QQuickStyle::setStyle("Basic");

    m_engine = new QQmlApplicationEngine();
    const QUrl url(QStringLiteral("qrc:/usdSim/main.qml"));
    QObject::connect(
        m_engine,
        &QQmlApplicationEngine::objectCreated,
        m_app,
        [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection);
    m_engine->load(url);

    m_inspector = new InspectorServer(m_engine);
    const quint16 port = []() -> quint16 {
        const QByteArray env = qgetenv("QML_INSPECTOR_PORT");
        if (!env.isEmpty()) {
            bool ok = false;
            int p = env.toInt(&ok);
            if (ok && p > 0 && p < 65536)
                return static_cast<quint16>(p);
        }
        return 37521;
    }();
    if (!m_inspector->start(port))
        qWarning("InspectorServer failed to start.");
}

int UsdSimApp::exec_app()
{
    if (!m_app)
        return -1;
    return m_app->exec();
}

void UsdSimApp::unregister_types()
{
    // Reserved for future use — Qt has no explicit unregister API
}

UsdDocument *UsdSimApp::findDocument()
{
    if (!m_engine) return nullptr;
    for (QObject *root : m_engine->rootObjects()) {
        auto *doc = root->findChild<UsdDocument *>();
        if (doc) return doc;
    }
    return nullptr;
}

void UsdSimApp::processEvents()
{
    if (m_app)
        m_app->processEvents();
}

void UsdSimApp::uninit()
{
    delete m_inspector;
    m_inspector = nullptr;

    delete m_engine;
    m_engine = nullptr;

    delete m_app;
    m_app = nullptr;
}
