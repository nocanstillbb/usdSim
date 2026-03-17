#pragma once
#include <QObject>
#include <memory>

class QGuiApplication;
class QQmlApplicationEngine;
class InspectorServer;
class UsdDocument;

class UsdSimApp : public QObject
{
    Q_OBJECT
public:
    explicit UsdSimApp(QObject *parent = nullptr);
    ~UsdSimApp();

    void register_types();
    void init(int argc, char *argv[]);
    int  exec_app();
    void unregister_types();
    void uninit();

    UsdDocument *findDocument();
    void processEvents();

private:
    QGuiApplication *m_app = nullptr;
    QQmlApplicationEngine *m_engine = nullptr;
    InspectorServer *m_inspector = nullptr;

    // Store argc/argv with stable lifetime
    int m_argc = 0;
    std::unique_ptr<char*[]> m_argv;
    std::vector<std::string> m_argStorage;
};
