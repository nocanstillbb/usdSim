#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QModelIndex>
#include <memory>

class UndoStack;

class UsdDocument : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList primPaths READ primPaths NOTIFY primPathsChanged)
    Q_PROPERTY(QObject* primModel READ primModel NOTIFY primPathsChanged)
    Q_PROPERTY(QObject* primTreeModel READ primTreeModel NOTIFY primPathsChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY filePathChanged)
    Q_PROPERTY(bool isOpen READ isOpen NOTIFY isOpenChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
    Q_PROPERTY(QObject* attrModel READ attrModel CONSTANT)
    Q_PROPERTY(UndoStack* undoStack READ undoStack CONSTANT)

public:
    explicit UsdDocument(QObject *parent = nullptr);
    ~UsdDocument();

    QStringList primPaths() const { return m_primPaths; }
    QObject *primModel() const;
    QObject *primTreeModel() const;
    QString filePath() const { return m_filePath; }
    bool isOpen() const { return m_isOpen; }
    QString errorString() const { return m_errorString; }
    QObject *attrModel() const;
    UndoStack *undoStack() const { return m_undoStack; }

    Q_INVOKABLE bool open(const QString &path);
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QString &path);
    Q_INVOKABLE void close();

    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    // 获取某个 prim 的所有属性（返回 [{name, typeName, value, isCustom}, ...]）
    Q_INVOKABLE QVariantList getAttributes(const QString &primPath);

    // 获取多个 prim 的共有属性，值不同时显示 "mixed"
    Q_INVOKABLE QVariantList getCommonAttributes(const QStringList &primPaths);

    // 设置属性值（value 为字符串，由 C++ 端转换）— pushes undo command
    Q_INVOKABLE bool setAttribute(const QString &primPath,
                                  const QString &attrName,
                                  const QString &value);

    // 批量设置多个 prim 的同一属性 — pushes undo command
    Q_INVOKABLE bool setAttributeMulti(const QStringList &primPaths,
                                       const QString &attrName,
                                       const QString &value);

    // 获取 prim 的基本信息
    Q_INVOKABLE QVariantMap getPrimInfo(const QString &primPath);

    // 添加 / 删除 prim — pushes undo command
    Q_INVOKABLE bool addPrim(const QString &parentPath,
                             const QString &name,
                             const QString &typeName);
    Q_INVOKABLE bool removePrim(const QString &primPath);

    // 设置 prim 的 visibility — pushes undo command
    Q_INVOKABLE bool setPrimVisibility(const QString &primPath, bool visible);

    // 属性模型操作（全量加载 / 增量刷新 / 清空）
    Q_INVOKABLE void loadAttributes(const QStringList &primPaths);
    Q_INVOKABLE void refreshAttributes(const QStringList &primPaths);
    Q_INVOKABLE void clearAttributes();

    // 查找 prim 在树模型中的 QModelIndex（用于 TreeView 展开+选中）
    Q_INVOKABLE QModelIndex findPrimModelIndex(const QString &path) const;

    // 内部使用：返回 USD Stage 指针（调用方须包含 USD 头文件再强转）
    void *stagePtr() const;

    // ── Internal methods (bypass undo stack, called by undo commands) ──
    bool setAttributeInternal(const QString &primPath,
                              const QString &attrName,
                              const QString &value);
    bool setAttributeMultiInternal(const QStringList &primPaths,
                                   const QString &attrName,
                                   const QString &value);
    bool addPrimInternal(const QString &parentPath,
                         const QString &name,
                         const QString &typeName);
    bool removePrimInternal(const QString &primPath);
    bool setPrimVisibilityInternal(const QString &primPath, bool visible);

    // Low-level: set USD visibility attr only, no signals/tree update
    bool setVisibilityRaw(const QString &primPath, bool visible);
    // Refresh the entire tree model's visibility state
    void refreshVisibilityTree();

    // Read current attribute value as string (for capturing "before" state)
    QString readAttributeValue(const QString &primPath, const QString &attrName);

    // Expose refreshPrimPaths for undo commands (e.g. RemovePrimCommand::undo)
    void refreshPrimPaths();

signals:
    void primPathsChanged();
    void stageModified();   // 属性值改变，mesh 需重建
    void filePathChanged();
    void isOpenChanged();
    void errorStringChanged();

private:
    void setError(const QString &err);

    struct Impl;
    std::unique_ptr<Impl> m_impl;

    UndoStack *m_undoStack = nullptr;

    QStringList m_primPaths;
    QString m_filePath;
    bool m_isOpen = false;
    QString m_errorString;
};
