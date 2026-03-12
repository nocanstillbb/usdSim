#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <memory>


class UsdDocument : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList primPaths READ primPaths NOTIFY primPathsChanged)
    Q_PROPERTY(QObject* primModel READ primModel NOTIFY primPathsChanged)
    Q_PROPERTY(QObject* primTreeModel READ primTreeModel NOTIFY primPathsChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY filePathChanged)
    Q_PROPERTY(bool isOpen READ isOpen NOTIFY isOpenChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)

public:
    explicit UsdDocument(QObject *parent = nullptr);
    ~UsdDocument();

    QStringList primPaths() const { return m_primPaths; }
    QObject *primModel() const;
    QObject *primTreeModel() const;
    QString filePath() const { return m_filePath; }
    bool isOpen() const { return m_isOpen; }
    QString errorString() const { return m_errorString; }

    Q_INVOKABLE bool open(const QString &path);
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QString &path);
    Q_INVOKABLE void close();

    // 获取某个 prim 的所有属性（返回 [{name, typeName, value, isCustom}, ...]）
    Q_INVOKABLE QVariantList getAttributes(const QString &primPath);

    // 设置属性值（value 为字符串，由 C++ 端转换）
    Q_INVOKABLE bool setAttribute(const QString &primPath,
                                  const QString &attrName,
                                  const QString &value);

    // 获取 prim 的基本信息
    Q_INVOKABLE QVariantMap getPrimInfo(const QString &primPath);

    // 添加 / 删除 prim
    Q_INVOKABLE bool addPrim(const QString &parentPath,
                             const QString &name,
                             const QString &typeName);
    Q_INVOKABLE bool removePrim(const QString &primPath);

    // 内部使用：返回 USD Stage 指针（调用方须包含 USD 头文件再强转）
    void *stagePtr() const;

signals:
    void primPathsChanged();
    void stageModified();   // 属性值改变，mesh 需重建
    void filePathChanged();
    void isOpenChanged();
    void errorStringChanged();

private:
    void refreshPrimPaths();
    void setError(const QString &err);

    struct Impl;
    std::unique_ptr<Impl> m_impl;

    QStringList m_primPaths;
    QString m_filePath;
    bool m_isOpen = false;
    QString m_errorString;
};
