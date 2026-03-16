#include "UndoCommands.h"
#include "UsdDocument.h"
#include "UsdViewportItem.h"

#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/copyUtils.h>
#include <pxr/usd/usd/stage.h>

PXR_NAMESPACE_USING_DIRECTIVE

// ══════════════════════════════════════════════════════════════
//  SetAttributeCommand
// ══════════════════════════════════════════════════════════════

SetAttributeCommand::SetAttributeCommand(UsdDocument *doc,
                                         const QString &primPath,
                                         const QString &attrName,
                                         const QString &oldValue,
                                         const QString &newValue)
    : m_doc(doc), m_primPath(primPath), m_attrName(attrName),
      m_oldValue(oldValue), m_newValue(newValue)
{
}

void SetAttributeCommand::undo()
{
    m_doc->setAttributeInternal(m_primPath, m_attrName, m_oldValue);
}

void SetAttributeCommand::redo()
{
    m_doc->setAttributeInternal(m_primPath, m_attrName, m_newValue);
}

bool SetAttributeCommand::mergeWith(const UndoCommand *other)
{
    auto *o = static_cast<const SetAttributeCommand *>(other);
    if (o->m_primPath != m_primPath || o->m_attrName != m_attrName)
        return false;
    // Keep our old value, take new value from the newer command
    m_newValue = o->m_newValue;
    return true;
}

QString SetAttributeCommand::text() const
{
    // Show short prim name instead of full path
    QString name = m_primPath.section('/', -1);
    return QStringLiteral("设置 %1.%2").arg(name, m_attrName);
}

// ══════════════════════════════════════════════════════════════
//  SetAttributeMultiCommand
// ══════════════════════════════════════════════════════════════

SetAttributeMultiCommand::SetAttributeMultiCommand(UsdDocument *doc,
                                                   const QStringList &primPaths,
                                                   const QString &attrName,
                                                   const QHash<QString, QString> &oldValues,
                                                   const QString &newValue)
    : m_doc(doc), m_primPaths(primPaths), m_attrName(attrName),
      m_oldValues(oldValues), m_newValue(newValue)
{
}

void SetAttributeMultiCommand::undo()
{
    for (const QString &path : m_primPaths) {
        if (m_oldValues.contains(path))
            m_doc->setAttributeInternal(path, m_attrName, m_oldValues[path]);
    }
    emit m_doc->stageModified();
}

void SetAttributeMultiCommand::redo()
{
    m_doc->setAttributeMultiInternal(m_primPaths, m_attrName, m_newValue);
}

bool SetAttributeMultiCommand::mergeWith(const UndoCommand *other)
{
    auto *o = static_cast<const SetAttributeMultiCommand *>(other);
    if (o->m_attrName != m_attrName || o->m_primPaths != m_primPaths)
        return false;
    m_newValue = o->m_newValue;
    return true;
}

QString SetAttributeMultiCommand::text() const
{
    return QStringLiteral("设置 %1 (%2 个 Prim)").arg(m_attrName).arg(m_primPaths.size());
}

// ══════════════════════════════════════════════════════════════
//  AddPrimCommand
// ══════════════════════════════════════════════════════════════

AddPrimCommand::AddPrimCommand(UsdDocument *doc,
                               const QString &parentPath,
                               const QString &name,
                               const QString &typeName)
    : m_doc(doc), m_parentPath(parentPath), m_name(name), m_typeName(typeName)
{
    m_fullPath = m_parentPath + QStringLiteral("/") + m_name;
}

void AddPrimCommand::undo()
{
    m_doc->removePrimInternal(m_fullPath);
}

void AddPrimCommand::redo()
{
    m_doc->addPrimInternal(m_parentPath, m_name, m_typeName);
}

QString AddPrimCommand::text() const
{
    return QStringLiteral("添加 %1").arg(m_fullPath);
}

// ══════════════════════════════════════════════════════════════
//  RemovePrimCommand
// ══════════════════════════════════════════════════════════════

RemovePrimCommand::RemovePrimCommand(UsdDocument *doc,
                                     const QString &primPath)
    : m_doc(doc), m_primPath(primPath)
{
    // Back up the subtree to an anonymous SdfLayer
    auto *stageRef = static_cast<UsdStageRefPtr *>(m_doc->stagePtr());
    if (stageRef && *stageRef) {
        SdfLayerRefPtr rootLayer = (*stageRef)->GetRootLayer();
        SdfLayerRefPtr backup = SdfLayer::CreateAnonymous();
        SdfPath sdfPath(m_primPath.toStdString());

        // Ensure parent path exists in backup
        SdfPath parentPath = sdfPath.GetParentPath();
        if (!parentPath.IsEmpty() && parentPath != SdfPath::AbsoluteRootPath()) {
            SdfCreatePrimInLayer(backup, parentPath);
        }

        SdfCopySpec(rootLayer, sdfPath, backup, sdfPath);
        m_backupLayer = new SdfLayerRefPtr(backup);
    }
}

void RemovePrimCommand::undo()
{
    // Restore subtree from backup layer
    if (!m_backupLayer) return;
    auto *backup = static_cast<SdfLayerRefPtr *>(m_backupLayer);
    auto *stageRef = static_cast<UsdStageRefPtr *>(m_doc->stagePtr());
    if (stageRef && *stageRef) {
        SdfLayerRefPtr rootLayer = (*stageRef)->GetRootLayer();
        SdfPath sdfPath(m_primPath.toStdString());
        SdfCopySpec(*backup, sdfPath, rootLayer, sdfPath);
    }
    m_doc->refreshPrimPaths();
    emit m_doc->stageModified();
}

void RemovePrimCommand::redo()
{
    m_doc->removePrimInternal(m_primPath);
}

QString RemovePrimCommand::text() const
{
    return QStringLiteral("删除 %1").arg(m_primPath);
}

// ══════════════════════════════════════════════════════════════
//  SetPrimActiveCommand
// ══════════════════════════════════════════════════════════════

SetPrimActiveCommand::SetPrimActiveCommand(UsdDocument *doc,
                                           const QVector<Entry> &entries)
    : m_doc(doc), m_entries(entries)
{
}

void SetPrimActiveCommand::undo()
{
    for (const Entry &e : m_entries)
        m_doc->setVisibilityRaw(e.primPath, e.oldVisible);
    m_doc->refreshVisibilityTree();
    emit m_doc->stageModified();
}

void SetPrimActiveCommand::redo()
{
    for (const Entry &e : m_entries)
        m_doc->setVisibilityRaw(e.primPath, e.newVisible);
    m_doc->refreshVisibilityTree();
    emit m_doc->stageModified();
}

QString SetPrimActiveCommand::text() const
{
    if (m_entries.isEmpty())
        return QStringLiteral("切换可见性");
    // Target prim is the last entry (ancestors come first)
    QString name = m_entries.last().primPath.section('/', -1);
    return m_entries.last().newVisible ? QStringLiteral("显示 %1").arg(name)
                                      : QStringLiteral("隐藏 %1").arg(name);
}

// ══════════════════════════════════════════════════════════════
//  SelectionCommand
// ══════════════════════════════════════════════════════════════

SelectionCommand::SelectionCommand(UsdViewportItem *viewport,
                                   const QStringList &oldSelection,
                                   const QStringList &newSelection)
    : m_viewport(viewport), m_oldSelection(oldSelection), m_newSelection(newSelection)
{
}

void SelectionCommand::undo()
{
    m_viewport->selectPrimPaths(m_oldSelection);
}

void SelectionCommand::redo()
{
    m_viewport->selectPrimPaths(m_newSelection);
}

bool SelectionCommand::mergeWith(const UndoCommand *other)
{
    auto *o = static_cast<const SelectionCommand *>(other);
    // Keep our old selection, take new selection from the newer command
    m_newSelection = o->m_newSelection;
    return true;
}

QString SelectionCommand::text() const
{
    if (m_newSelection.isEmpty())
        return QStringLiteral("取消选择");
    return QStringLiteral("选择 %1 个 Prim").arg(m_newSelection.size());
}

// ══════════════════════════════════════════════════════════════
//  AddAttributeCommand
// ══════════════════════════════════════════════════════════════

AddAttributeCommand::AddAttributeCommand(UsdDocument *doc,
                                         const QStringList &primPaths,
                                         const QString &attrName,
                                         const QString &typeName,
                                         bool custom,
                                         const QString &variability)
    : m_doc(doc), m_primPaths(primPaths), m_attrName(attrName),
      m_typeName(typeName), m_custom(custom), m_variability(variability)
{
}

void AddAttributeCommand::undo()
{
    for (const QString &path : m_primPaths)
        m_doc->removeAttributeInternal(path, m_attrName);
}

void AddAttributeCommand::redo()
{
    for (const QString &path : m_primPaths)
        m_doc->addAttributeInternal(path, m_attrName, m_typeName, m_custom, m_variability);
}

QString AddAttributeCommand::text() const
{
    if (m_primPaths.size() == 1) {
        QString name = m_primPaths.first().section('/', -1);
        return QStringLiteral("添加属性 %1.%2").arg(name, m_attrName);
    }
    return QStringLiteral("添加属性 %1 (%2 个 Prim)").arg(m_attrName).arg(m_primPaths.size());
}

// ══════════════════════════════════════════════════════════════
//  RemoveAttributeCommand
// ══════════════════════════════════════════════════════════════

RemoveAttributeCommand::RemoveAttributeCommand(UsdDocument *doc,
                                               const QString &attrName,
                                               const QVector<Entry> &entries)
    : m_doc(doc), m_attrName(attrName), m_entries(entries)
{
}

void RemoveAttributeCommand::undo()
{
    for (const Entry &e : m_entries) {
        m_doc->addAttributeInternal(e.primPath, m_attrName, e.typeName, e.custom, e.variability);
        if (!e.oldValue.isEmpty())
            m_doc->setAttributeInternal(e.primPath, m_attrName, e.oldValue);
    }
}

void RemoveAttributeCommand::redo()
{
    for (const Entry &e : m_entries)
        m_doc->removeAttributeInternal(e.primPath, m_attrName);
}

QString RemoveAttributeCommand::text() const
{
    if (m_entries.size() == 1) {
        QString name = m_entries.first().primPath.section('/', -1);
        return QStringLiteral("删除属性 %1.%2").arg(name, m_attrName);
    }
    return QStringLiteral("删除属性 %1 (%2 个 Prim)").arg(m_attrName).arg(m_entries.size());
}

// ══════════════════════════════════════════════════════════════
//  ApplyApiSchemaCommand
// ══════════════════════════════════════════════════════════════

ApplyApiSchemaCommand::ApplyApiSchemaCommand(UsdDocument *doc,
                                             const QStringList &primPaths,
                                             const QString &schemaIdentifier)
    : m_doc(doc), m_primPaths(primPaths), m_schemaIdentifier(schemaIdentifier)
{
}

void ApplyApiSchemaCommand::undo()
{
    for (const QString &path : m_primPaths)
        m_doc->removeApiSchemaInternal(path, m_schemaIdentifier);
}

void ApplyApiSchemaCommand::redo()
{
    for (const QString &path : m_primPaths)
        m_doc->applyApiSchemaInternal(path, m_schemaIdentifier);
}

QString ApplyApiSchemaCommand::text() const
{
    if (m_primPaths.size() == 1)
        return QStringLiteral("应用 Schema %1").arg(m_schemaIdentifier);
    return QStringLiteral("应用 Schema %1 (%2 个 Prim)").arg(m_schemaIdentifier).arg(m_primPaths.size());
}

// ══════════════════════════════════════════════════════════════
//  RemoveApiSchemaCommand
// ══════════════════════════════════════════════════════════════

RemoveApiSchemaCommand::RemoveApiSchemaCommand(UsdDocument *doc,
                                               const QStringList &primPaths,
                                               const QString &schemaIdentifier)
    : m_doc(doc), m_primPaths(primPaths), m_schemaIdentifier(schemaIdentifier)
{
}

void RemoveApiSchemaCommand::undo()
{
    for (const QString &path : m_primPaths)
        m_doc->applyApiSchemaInternal(path, m_schemaIdentifier);
}

void RemoveApiSchemaCommand::redo()
{
    for (const QString &path : m_primPaths)
        m_doc->removeApiSchemaInternal(path, m_schemaIdentifier);
}

QString RemoveApiSchemaCommand::text() const
{
    if (m_primPaths.size() == 1)
        return QStringLiteral("移除 Schema %1").arg(m_schemaIdentifier);
    return QStringLiteral("移除 Schema %1 (%2 个 Prim)").arg(m_schemaIdentifier).arg(m_primPaths.size());
}

// ══════════════════════════════════════════════════════════════
//  GizmoTransformCommand
// ══════════════════════════════════════════════════════════════

GizmoTransformCommand::GizmoTransformCommand(UsdDocument *doc,
                                             const QVector<Entry> &entries)
    : m_doc(doc), m_entries(entries)
{
}

void GizmoTransformCommand::undo()
{
    for (const Entry &e : m_entries)
        m_doc->setAttributeInternal(e.primPath, e.attrName, e.oldValue);
    emit m_doc->stageModified();
}

void GizmoTransformCommand::redo()
{
    for (const Entry &e : m_entries)
        m_doc->setAttributeInternal(e.primPath, e.attrName, e.newValue);
    emit m_doc->stageModified();
}

QString GizmoTransformCommand::text() const
{
    if (m_entries.isEmpty())
        return QStringLiteral("变换");
    QString name = m_entries.first().primPath.section('/', -1);
    if (m_entries.size() == 1)
        return QStringLiteral("变换 %1.%2").arg(name, m_entries.first().attrName);
    return QStringLiteral("变换 %1 (%2 项)").arg(name).arg(m_entries.size());
}
