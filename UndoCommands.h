#pragma once
#include "UndoStack.h"
#include <QString>
#include <QStringList>
#include <QHash>

class UsdDocument;
class UsdViewportItem;

// Merge IDs for command types
enum UndoCommandId {
    CmdIdSetAttribute      = 1,
    CmdIdSetAttributeMulti = 2,
    CmdIdSelection         = 3,
};

// ── SetAttributeCommand ──────────────────────────────────────
// Undo/redo a single prim attribute change.
class SetAttributeCommand : public UndoCommand
{
public:
    SetAttributeCommand(UsdDocument *doc,
                        const QString &primPath,
                        const QString &attrName,
                        const QString &oldValue,
                        const QString &newValue);

    void undo() override;
    void redo() override;
    int id() const override { return CmdIdSetAttribute; }
    bool mergeWith(const UndoCommand *other) override;
    QString text() const override;

private:
    UsdDocument *m_doc;
    QString m_primPath;
    QString m_attrName;
    QString m_oldValue;
    QString m_newValue;
};

// ── SetAttributeMultiCommand ─────────────────────────────────
// Undo/redo setting the same attribute on multiple prims.
class SetAttributeMultiCommand : public UndoCommand
{
public:
    SetAttributeMultiCommand(UsdDocument *doc,
                             const QStringList &primPaths,
                             const QString &attrName,
                             const QHash<QString, QString> &oldValues,
                             const QString &newValue);

    void undo() override;
    void redo() override;
    int id() const override { return CmdIdSetAttributeMulti; }
    bool mergeWith(const UndoCommand *other) override;
    QString text() const override;

private:
    UsdDocument *m_doc;
    QStringList m_primPaths;
    QString m_attrName;
    QHash<QString, QString> m_oldValues; // primPath -> old value
    QString m_newValue;
};

// ── AddPrimCommand ───────────────────────────────────────────
class AddPrimCommand : public UndoCommand
{
public:
    AddPrimCommand(UsdDocument *doc,
                   const QString &parentPath,
                   const QString &name,
                   const QString &typeName);

    void undo() override;
    void redo() override;
    QString text() const override;

private:
    UsdDocument *m_doc;
    QString m_parentPath;
    QString m_name;
    QString m_typeName;
    QString m_fullPath; // computed: parentPath + "/" + name
};

// ── RemovePrimCommand ────────────────────────────────────────
// Backs up the subtree to an anonymous SdfLayer before removal.
class RemovePrimCommand : public UndoCommand
{
public:
    RemovePrimCommand(UsdDocument *doc,
                      const QString &primPath);

    void undo() override;
    void redo() override;
    QString text() const override;

private:
    UsdDocument *m_doc;
    QString m_primPath;
    void *m_backupLayer = nullptr; // SdfLayerRefPtr* allocated on heap
};

// ── SelectionCommand ─────────────────────────────────────────
class SelectionCommand : public UndoCommand
{
public:
    SelectionCommand(UsdViewportItem *viewport,
                     const QStringList &oldSelection,
                     const QStringList &newSelection);

    void undo() override;
    void redo() override;
    int id() const override { return CmdIdSelection; }
    bool mergeWith(const UndoCommand *other) override;
    QString text() const override;

private:
    UsdViewportItem *m_viewport;
    QStringList m_oldSelection;
    QStringList m_newSelection;
};

// ── GizmoTransformCommand ────────────────────────────────────
// Captures pre-drag and post-drag transform attribute values.
// Used with pushNoRedo() since the drag already applied changes.
class GizmoTransformCommand : public UndoCommand
{
public:
    struct Entry {
        QString primPath;
        QString attrName;
        QString oldValue;
        QString newValue;
    };

    GizmoTransformCommand(UsdDocument *doc,
                          const QVector<Entry> &entries);

    void undo() override;
    void redo() override;
    QString text() const override;

private:
    UsdDocument *m_doc;
    QVector<Entry> m_entries;
};
