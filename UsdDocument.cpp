#include "UsdDocument.h"
#include "UndoStack.h"
#include "UndoCommands.h"
#include "PrimInfo.h"
#include "AttrInfo.h"
#include <prism/qt/core/hpp/prismModelListProxy.hpp>
#include <prism/qt/core/hpp/prismTreeModelProxy.hpp>
#include <prism/qt/core/hpp/prismTreeNodeProxy.hpp>

#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usd/schemaRegistry.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/base/tf/type.h>
#include <pxr/base/vt/value.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/vec2d.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/tf/token.h>

#include <QQmlEngine>
#include <map>
#include <set>

PXR_NAMESPACE_USING_DIRECTIVE

// ---- PrimTreeModel ----
// prismTreeModelProxy<T>::data() calls field_do(*node, fname) on the node itself,
// but prismTreeNodeProxy<T> has no PRISM_FIELDS, so we override data() here to
// use node->get() which properly delegates to the underlying T instance.
class PrimTreeModel : public prism::qt::core::prismTreeModelProxy<PrimInfo>
{
public:
    using NodeProxy = prism::qt::core::prismTreeNodeProxy<PrimInfo>;

    explicit PrimTreeModel(QObject *p = nullptr)
        : prism::qt::core::prismTreeModelProxy<PrimInfo>(p) {}

    int columnCount(const QModelIndex & = {}) const override { return 1; }

    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid()) return {};
        auto *item = static_cast<NodeProxy *>(index.internalPointer());
        if (!item) return {};
        const QByteArray fname = roleNames().value(role);
        if (fname.isEmpty()) return {};
        // get() accesses m_instance fields via reflection and converts
        // std::string → QString etc., which is what QML delegates expect.
        return const_cast<NodeProxy *>(item)->get(fname);
    }

    QModelIndex indexForPath(const QString &path) const {
        return findPath(QModelIndex(), path);
    }

    // Incremental update: recompute visibility for a subtree rooted at `path`
    void updateVisibility(const QString &path, const UsdStageRefPtr &stage) {
        QModelIndex idx = indexForPath(path);
        if (!idx.isValid()) return;
        updateVisibilityRecursive(idx, stage);
    }

    // Refresh visibility for the entire tree
    void updateVisibilityAll(const UsdStageRefPtr &stage) {
        const int rc = rowCount(QModelIndex());
        for (int r = 0; r < rc; ++r)
            updateVisibilityRecursive(index(r, 0, QModelIndex()), stage);
    }

    void rebuild(const UsdStageRefPtr &stage) {
        auto root = std::make_shared<NodeProxy>();
        if (stage) {
            std::map<std::string, std::shared_ptr<NodeProxy>> nodeMap;
            for (const UsdPrim &prim : stage->Traverse()) {
                auto info = std::make_shared<PrimInfo>();
                info->name     = prim.GetName().GetString();
                info->path     = prim.GetPath().GetString();
                info->typeName = prim.GetTypeName().GetString();

                UsdGeomImageable imageable(prim);
                info->isActive = !imageable
                    || imageable.ComputeVisibility() != UsdGeomTokens->invisible;

                auto node = std::make_shared<NodeProxy>(info);
                nodeMap[info->path] = node;

                const std::string parentPath = prim.GetPath().GetParentPath().GetString();
                auto it = nodeMap.find(parentPath);
                if (it != nodeMap.end())
                    it->second->appendChild(node);
                else
                    root->appendChild(node);
            }
        }
        setRootNode(root);
    }

private:
    QModelIndex findPath(const QModelIndex &parent, const QString &path) const {
        const int rc = rowCount(parent);
        for (int r = 0; r < rc; ++r) {
            QModelIndex idx = index(r, 0, parent);
            auto *node = static_cast<NodeProxy *>(idx.internalPointer());
            if (node && node->get("path").toString() == path)
                return idx;
            QModelIndex found = findPath(idx, path);
            if (found.isValid())
                return found;
        }
        return {};
    }

    void updateVisibilityRecursive(const QModelIndex &idx, const UsdStageRefPtr &stage) {
        auto *node = static_cast<NodeProxy *>(idx.internalPointer());
        if (node && node->instance()) {
            SdfPath sp(node->instance()->path);
            UsdPrim prim = stage->GetPrimAtPath(sp);
            if (prim.IsValid()) {
                UsdGeomImageable img(prim);
                node->instance()->isActive = !img
                    || img.ComputeVisibility() != UsdGeomTokens->invisible;
            }
            emit dataChanged(idx, idx);
        }
        const int rc = rowCount(idx);
        for (int r = 0; r < rc; ++r)
            updateVisibilityRecursive(index(r, 0, idx), stage);
    }
};

// ---- 内部实现结构 ----
using PrimModel = prism::qt::core::prismModelListProxy<PrimInfo>;
using AttrModel = prism::qt::core::prismModelListProxy<AttrInfo>;

struct UsdDocument::Impl {
    UsdStageRefPtr  stage;
    PrimModel      *primModel     = nullptr;
    PrimTreeModel  *primTreeModel = nullptr;
    AttrModel      *attrModel     = nullptr;
    ~Impl() { delete primModel; delete primTreeModel; delete attrModel; }
};

// ---- 工具函数：VtValue → QString ----
static QString vtValueToString(const VtValue &val)
{
    if (val.IsEmpty()) return QString();

    if (val.IsHolding<bool>())         return val.Get<bool>() ? "true" : "false";
    if (val.IsHolding<int>())          return QString::number(val.Get<int>());
    if (val.IsHolding<float>())        return QString::number((double)val.Get<float>(), 'g', 6);
    if (val.IsHolding<double>())       return QString::number(val.Get<double>(), 'g', 10);
    if (val.IsHolding<std::string>())  return QString::fromStdString(val.Get<std::string>());
    if (val.IsHolding<TfToken>())      return QString::fromStdString(val.Get<TfToken>().GetString());

    if (val.IsHolding<GfVec2f>()) {
        auto v = val.Get<GfVec2f>();
        return QString("(%1, %2)").arg(v[0]).arg(v[1]);
    }
    if (val.IsHolding<GfVec3f>()) {
        auto v = val.Get<GfVec3f>();
        return QString("(%1, %2, %3)").arg(v[0]).arg(v[1]).arg(v[2]);
    }
    if (val.IsHolding<GfVec4f>()) {
        auto v = val.Get<GfVec4f>();
        return QString("(%1, %2, %3, %4)").arg(v[0]).arg(v[1]).arg(v[2]).arg(v[3]);
    }
    if (val.IsHolding<GfVec3d>()) {
        auto v = val.Get<GfVec3d>();
        return QString("(%1, %2, %3)").arg(v[0]).arg(v[1]).arg(v[2]);
    }

    // 其他类型：转成流输出
    std::ostringstream oss;
    oss << val;
    return QString::fromStdString(oss.str());
}

// ---- 工具函数：判断类型是否支持编辑 ----
static bool isEditableType(const std::string &typeStr)
{
    static const std::set<std::string> editable = {
        "bool", "int", "float", "double", "string", "token",
        "float3", "color3f", "normal3f", "point3f", "vector3f",
        "double3", "point3d", "vector3d",
    };
    return editable.count(typeStr) > 0;
}

// ---- 工具函数：字符串 → 写回 USD 属性 ----
static bool setAttrFromString(UsdAttribute &attr, const QString &str)
{
    const SdfValueTypeName typeName = attr.GetTypeName();
    const std::string typeStr = typeName.GetAsToken().GetString();

    bool ok = false;
    if (typeStr == "bool") {
        attr.Set(str.toLower() == "true" || str == "1");
        ok = true;
    } else if (typeStr == "int") {
        attr.Set(str.toInt(&ok));
    } else if (typeStr == "float") {
        attr.Set((float)str.toDouble(&ok));
    } else if (typeStr == "double") {
        attr.Set(str.toDouble(&ok));
    } else if (typeStr == "string") {
        attr.Set(str.toStdString());
        ok = true;
    } else if (typeStr == "token") {
        attr.Set(TfToken(str.toStdString()));
        ok = true;
    } else if (typeStr == "float3" || typeStr == "color3f" || typeStr == "normal3f"
               || typeStr == "point3f" || typeStr == "vector3f") {
        // 解析 "(x, y, z)" 格式
        QString s = str;
        s.remove('(').remove(')');
        auto parts = s.split(',');
        if (parts.size() == 3) {
            float x = parts[0].trimmed().toFloat(&ok);
            float y = parts[1].trimmed().toFloat();
            float z = parts[2].trimmed().toFloat();
            if (ok) attr.Set(GfVec3f(x, y, z));
        }
    } else if (typeStr == "double3" || typeStr == "point3d" || typeStr == "vector3d") {
        QString s = str;
        s.remove('(').remove(')');
        auto parts = s.split(',');
        if (parts.size() == 3) {
            double x = parts[0].trimmed().toDouble(&ok);
            double y = parts[1].trimmed().toDouble();
            double z = parts[2].trimmed().toDouble();
            if (ok) attr.Set(GfVec3d(x, y, z));
        }
    } else {
        // 其他类型暂不支持直接编辑
        return false;
    }
    return ok;
}

// ---- 构造 / 析构 ----
UsdDocument::UsdDocument(QObject *parent)
    : QObject(parent), m_impl(std::make_unique<Impl>())
{
    m_impl->primModel     = new PrimModel();
    m_impl->primTreeModel = new PrimTreeModel();
    m_impl->attrModel     = new AttrModel();
    m_undoStack = new UndoStack(this);
}

QObject *UsdDocument::primModel() const
{
    return m_impl->primModel;
}

QObject *UsdDocument::primTreeModel() const
{
    return m_impl->primTreeModel;
}

QObject *UsdDocument::attrModel() const
{
    return m_impl->attrModel;
}

UsdDocument::~UsdDocument() = default;

// ---- 私有方法 ----
void UsdDocument::setError(const QString &err)
{
    m_errorString = err;
    emit errorStringChanged();
}

void UsdDocument::refreshPrimPaths()
{
    m_primPaths.clear();

    // 重建列表模型
    m_impl->primModel->pub_beginResetModel();
    m_impl->primModel->removeAllItemsNotNotify();

    if (m_impl->stage) {
        for (const UsdPrim &prim : m_impl->stage->Traverse()) {
            QString path = QString::fromStdString(prim.GetPath().GetString());
            m_primPaths << path;

            auto info = std::make_shared<PrimInfo>();
            info->name     = prim.GetName().GetString();
            info->path     = prim.GetPath().GetString();
            info->typeName = prim.GetTypeName().GetString();
            info->isActive = true; // list model doesn't need visibility
            m_impl->primModel->appendItemNotNotify(info);
        }
    }

    m_impl->primModel->pub_endResetModel();

    // 重建树形模型（prismTreeModelProxy + prismTreeNodeProxy）
    m_impl->primTreeModel->rebuild(m_impl->stage);

    emit primPathsChanged();
}

// ---- 公开方法 ----
bool UsdDocument::open(const QString &path)
{
    QString localPath = path;
    // 去掉 file:// 前缀
    if (localPath.startsWith("file://"))
        localPath = localPath.mid(7);

    auto stage = UsdStage::Open(localPath.toStdString());
    if (!stage) {
        setError(QString("无法打开文件: %1").arg(localPath));
        return false;
    }

    m_impl->stage = stage;
    m_filePath = localPath;
    m_isOpen = true;
    m_errorString.clear();
    m_undoStack->clear();

    emit filePathChanged();
    emit isOpenChanged();
    emit errorStringChanged();
    refreshPrimPaths();
    return true;
}

bool UsdDocument::save()
{
    if (!m_impl->stage) { setError("没有打开的文件"); return false; }
    m_impl->stage->Save();
    return true;
}

bool UsdDocument::saveAs(const QString &path)
{
    if (!m_impl->stage) { setError("没有打开的文件"); return false; }
    m_impl->stage->Export(path.toStdString());
    return true;
}

void UsdDocument::close()
{
    m_impl->stage.Reset();
    m_filePath.clear();
    m_isOpen = false;
    m_undoStack->clear();
    refreshPrimPaths();     // clears m_primPaths, resets list+tree models, emits primPathsChanged
    emit filePathChanged();
    emit isOpenChanged();
}

QVariantList UsdDocument::getAttributes(const QString &primPath)
{
    QVariantList result;
    if (!m_impl->stage) return result;

    SdfPath sdfPath(primPath.toStdString());
    UsdPrim prim = m_impl->stage->GetPrimAtPath(sdfPath);
    if (!prim.IsValid()) return result;

    for (const UsdAttribute &attr : prim.GetAttributes()) {
        VtValue val;
        attr.Get(&val);

        QVariantMap entry;
        std::string typeStr = attr.GetTypeName().GetAsToken().GetString();
        entry["name"]     = QString::fromStdString(attr.GetName().GetString());
        entry["typeName"] = QString::fromStdString(typeStr);
        entry["value"]    = vtValueToString(val);
        entry["isCustom"] = attr.IsCustom();
        entry["readOnly"] = !isEditableType(typeStr);
        result << entry;
    }
    return result;
}

QVariantList UsdDocument::getCommonAttributes(const QStringList &primPaths)
{
    QVariantList result;
    if (!m_impl->stage || primPaths.isEmpty()) return result;

    // Single prim: fall back to normal getAttributes
    if (primPaths.size() == 1)
        return getAttributes(primPaths.first());

    // Collect attributes for each prim: name -> {typeName, value, isCustom}
    // First prim determines the candidate set; intersect with subsequent prims.
    struct AttrInfo {
        QString typeName;
        QString value;
        bool isCustom;
        bool mixed;
    };

    // Build map from first prim
    QMap<QString, AttrInfo> common;
    {
        SdfPath sdfPath(primPaths[0].toStdString());
        UsdPrim prim = m_impl->stage->GetPrimAtPath(sdfPath);
        if (!prim.IsValid()) return result;

        for (const UsdAttribute &attr : prim.GetAttributes()) {
            VtValue val;
            attr.Get(&val);
            QString name = QString::fromStdString(attr.GetName().GetString());
            AttrInfo info;
            info.typeName = QString::fromStdString(attr.GetTypeName().GetAsToken().GetString());
            info.value    = vtValueToString(val);
            info.isCustom = attr.IsCustom();
            info.mixed    = false;
            common[name]  = info;
        }
    }

    // Intersect with remaining prims
    for (int i = 1; i < primPaths.size(); ++i) {
        SdfPath sdfPath(primPaths[i].toStdString());
        UsdPrim prim = m_impl->stage->GetPrimAtPath(sdfPath);
        if (!prim.IsValid()) return {};

        // Collect this prim's attribute names and values
        QSet<QString> thisNames;
        for (const UsdAttribute &attr : prim.GetAttributes()) {
            VtValue val;
            attr.Get(&val);
            QString name = QString::fromStdString(attr.GetName().GetString());
            QString typeName = QString::fromStdString(attr.GetTypeName().GetAsToken().GetString());
            thisNames.insert(name);

            auto it = common.find(name);
            if (it != common.end()) {
                // Check type match
                if (it->typeName != typeName) {
                    common.erase(it);
                } else {
                    // Check value match
                    QString thisValue = vtValueToString(val);
                    if (it->value != thisValue)
                        it->mixed = true;
                }
            }
        }

        // Remove attributes not present in this prim
        for (auto it = common.begin(); it != common.end(); ) {
            if (!thisNames.contains(it.key()))
                it = common.erase(it);
            else
                ++it;
        }
    }

    // Build result list
    for (auto it = common.constBegin(); it != common.constEnd(); ++it) {
        QVariantMap entry;
        entry["name"]     = it.key();
        entry["typeName"] = it->typeName;
        entry["value"]    = it->mixed ? QStringLiteral("mixed") : it->value;
        entry["isCustom"] = it->isCustom;
        entry["readOnly"] = !isEditableType(it->typeName.toStdString());
        result << entry;
    }
    return result;
}

bool UsdDocument::setAttribute(const QString &primPath,
                               const QString &attrName,
                               const QString &value)
{
    if (!m_impl->stage) return false;

    // Capture old value for undo
    QString oldValue = readAttributeValue(primPath, attrName);

    auto cmd = std::make_unique<SetAttributeCommand>(this, primPath, attrName, oldValue, value);
    m_undoStack->push(std::move(cmd));
    return true;
}

bool UsdDocument::setAttributeInternal(const QString &primPath,
                                        const QString &attrName,
                                        const QString &value)
{
    if (!m_impl->stage) return false;

    SdfPath sdfPath(primPath.toStdString());
    UsdPrim prim = m_impl->stage->GetPrimAtPath(sdfPath);
    if (!prim.IsValid()) return false;

    UsdAttribute attr = prim.GetAttribute(TfToken(attrName.toStdString()));
    if (!attr.IsValid()) return false;

    bool ok = setAttrFromString(attr, value);
    if (ok) {
        // Sync tree eye icon when visibility attribute changes
        if (attrName == QStringLiteral("visibility"))
            m_impl->primTreeModel->updateVisibility(primPath, m_impl->stage);
        emit stageModified();
    }
    return ok;
}

bool UsdDocument::setAttributeMulti(const QStringList &primPaths,
                                    const QString &attrName,
                                    const QString &value)
{
    if (!m_impl->stage || primPaths.isEmpty()) return false;

    // Capture old values for undo
    QHash<QString, QString> oldValues;
    for (const QString &path : primPaths)
        oldValues[path] = readAttributeValue(path, attrName);

    auto cmd = std::make_unique<SetAttributeMultiCommand>(this, primPaths, attrName, oldValues, value);
    m_undoStack->push(std::move(cmd));
    return true;
}

bool UsdDocument::setAttributeMultiInternal(const QStringList &primPaths,
                                             const QString &attrName,
                                             const QString &value)
{
    if (!m_impl->stage || primPaths.isEmpty()) return false;

    bool anyOk = false;
    for (const QString &path : primPaths) {
        SdfPath sdfPath(path.toStdString());
        UsdPrim prim = m_impl->stage->GetPrimAtPath(sdfPath);
        if (!prim.IsValid()) continue;

        UsdAttribute attr = prim.GetAttribute(TfToken(attrName.toStdString()));
        if (!attr.IsValid()) continue;

        if (setAttrFromString(attr, value))
            anyOk = true;
    }
    if (anyOk) emit stageModified();
    return anyOk;
}

QVariantMap UsdDocument::getPrimInfo(const QString &primPath)
{
    QVariantMap info;
    if (!m_impl->stage) return info;

    SdfPath sdfPath(primPath.toStdString());
    UsdPrim prim = m_impl->stage->GetPrimAtPath(sdfPath);
    if (!prim.IsValid()) return info;

    info["path"]     = primPath;
    info["typeName"] = QString::fromStdString(prim.GetTypeName().GetString());
    info["isActive"] = prim.IsActive();
    info["isModel"]  = prim.IsModel();

    QStringList children;
    for (const UsdPrim &child : prim.GetChildren())
        children << QString::fromStdString(child.GetPath().GetString());
    info["children"] = children;

    return info;
}

bool UsdDocument::addPrim(const QString &parentPath,
                          const QString &name,
                          const QString &typeName)
{
    if (!m_impl->stage) return false;

    auto cmd = std::make_unique<AddPrimCommand>(this, parentPath, name, typeName);
    m_undoStack->push(std::move(cmd));
    return true;
}

bool UsdDocument::addPrimInternal(const QString &parentPath,
                                   const QString &name,
                                   const QString &typeName)
{
    if (!m_impl->stage) return false;

    SdfPath newPath = SdfPath(parentPath.toStdString()).AppendChild(TfToken(name.toStdString()));
    UsdPrim prim = m_impl->stage->DefinePrim(newPath, TfToken(typeName.toStdString()));
    if (!prim.IsValid()) return false;

    refreshPrimPaths();
    emit stageModified();
    return true;
}

bool UsdDocument::removePrim(const QString &primPath)
{
    if (!m_impl->stage) return false;

    // RemovePrimCommand backs up subtree in constructor, then calls redo() which does removePrimInternal()
    auto cmd = std::make_unique<RemovePrimCommand>(this, primPath);
    m_undoStack->push(std::move(cmd));
    return true;
}

bool UsdDocument::removePrimInternal(const QString &primPath)
{
    if (!m_impl->stage) return false;

    bool ok = m_impl->stage->RemovePrim(SdfPath(primPath.toStdString()));
    if (ok) {
        refreshPrimPaths();
        emit stageModified();
    }
    return ok;
}

bool UsdDocument::setPrimVisibility(const QString &primPath, bool visible)
{
    if (!m_impl->stage) return false;

    SdfPath sdfPath(primPath.toStdString());
    UsdPrim prim = m_impl->stage->GetPrimAtPath(sdfPath);
    if (!prim.IsValid()) return false;

    auto readOwnVis = [](const UsdPrim &p) -> bool {
        UsdGeomImageable img(p);
        if (!img) return true;
        TfToken vis;
        if (img.GetVisibilityAttr().Get(&vis))
            return vis != UsdGeomTokens->invisible;
        return true;
    };

    // Map: primPath → {oldVisible, newVisible}. Later writes override newVisible.
    QMap<QString, QPair<bool,bool>> entryMap;

    auto addEntry = [&](const UsdPrim &p, bool newVis) {
        QString path = QString::fromStdString(p.GetPath().GetString());
        auto it = entryMap.find(path);
        if (it != entryMap.end()) {
            it->second = newVis; // keep original oldVisible, update newVisible
        } else {
            entryMap.insert(path, {readOwnVis(p), newVis});
        }
    };

    if (visible) {
        // Build chain: target → parent → ... → topmost computed-invisible ancestor
        QVector<UsdPrim> chain;
        chain.append(prim);
        UsdPrim cur = prim.GetParent();
        while (cur && !cur.IsPseudoRoot()) {
            UsdGeomImageable img(cur);
            if (img && img.ComputeVisibility() == UsdGeomTokens->invisible) {
                chain.append(cur);
                cur = cur.GetParent();
            } else {
                break;
            }
        }

        if (chain.size() == 1) {
            // No invisible ancestors — just toggle self, preserve children
            addEntry(prim, true);
        } else {
            // Recursive split: process top-down.
            // At each ancestor level set node→inherited, ALL children→invisible.
            // Path nodes appear twice — map keeps the last (inherited) value.
            for (int i = chain.size() - 1; i >= 0; --i) {
                UsdPrim node = chain[i];
                addEntry(node, true); // inherited
                for (const UsdPrim &child : node.GetChildren())
                    addEntry(child, false); // invisible
            }
        }
    } else {
        addEntry(prim, false);
    }

    // Convert map to entries, skipping no-ops
    QVector<SetPrimActiveCommand::Entry> entries;
    for (auto it = entryMap.cbegin(); it != entryMap.cend(); ++it) {
        if (it->first != it->second) {
            SetPrimActiveCommand::Entry e;
            e.primPath = it.key();
            e.oldVisible = it->first;
            e.newVisible = it->second;
            entries.append(e);
        }
    }

    if (entries.isEmpty()) return true;
    auto cmd = std::make_unique<SetPrimActiveCommand>(this, entries);
    m_undoStack->push(std::move(cmd));
    return true;
}

bool UsdDocument::setPrimVisibilityInternal(const QString &primPath, bool visible)
{
    if (!m_impl->stage) return false;
    if (!setVisibilityRaw(primPath, visible)) return false;

    // Update tree: this node + all descendants (inherited visibility propagates)
    m_impl->primTreeModel->updateVisibility(primPath, m_impl->stage);

    emit stageModified();
    return true;
}

bool UsdDocument::setVisibilityRaw(const QString &primPath, bool visible)
{
    if (!m_impl->stage) return false;

    SdfPath sdfPath(primPath.toStdString());
    UsdPrim prim = m_impl->stage->GetPrimAtPath(sdfPath);
    if (!prim.IsValid()) return false;

    UsdGeomImageable imageable(prim);
    if (!imageable) return false;

    imageable.GetVisibilityAttr().Set(
        visible ? UsdGeomTokens->inherited : UsdGeomTokens->invisible
    );
    return true;
}

void UsdDocument::refreshVisibilityTree()
{
    if (!m_impl->stage) return;
    m_impl->primTreeModel->updateVisibilityAll(m_impl->stage);
}

// ---- 属性模型操作 ----
void UsdDocument::loadAttributes(const QStringList &primPaths)
{
    auto *am = m_impl->attrModel;
    am->pub_beginResetModel();
    am->removeAllItemsNotNotify();

    if (!m_impl->stage || primPaths.isEmpty()) {
        am->pub_endResetModel();
        return;
    }

    // Fetch attribute list (single or multi-prim common)
    QVariantList attrs;
    if (primPaths.size() == 1)
        attrs = getAttributes(primPaths.first());
    else
        attrs = getCommonAttributes(primPaths);

    for (const QVariant &v : attrs) {
        const QVariantMap m = v.toMap();
        auto info = std::make_shared<AttrInfo>();
        info->name     = m[QStringLiteral("name")].toString();
        info->typeName = m[QStringLiteral("typeName")].toString();
        info->value    = m[QStringLiteral("value")].toString();
        info->isCustom = m[QStringLiteral("isCustom")].toBool();
        info->readOnly = m[QStringLiteral("readOnly")].toBool();
        am->appendItemNotNotify(info);
    }

    am->pub_endResetModel();
}

void UsdDocument::refreshAttributes(const QStringList &primPaths)
{
    auto *am = m_impl->attrModel;
    if (!m_impl->stage || primPaths.isEmpty() || am->length() == 0)
        return;

    QVariantList attrs;
    if (primPaths.size() == 1)
        attrs = getAttributes(primPaths.first());
    else
        attrs = getCommonAttributes(primPaths);

    // In-place update existing rows via setData (emits dataChanged, preserves scroll)
    const int valueRole = am->nameRoles().value("value", -1);
    if (valueRole < 0) return;

    const int existingCount = am->length();
    const int updateCount = qMin(existingCount, (int)attrs.size());

    for (int i = 0; i < updateCount; ++i) {
        const QVariantMap m = attrs[i].toMap();
        const QString newValue = m[QStringLiteral("value")].toString();
        am->setData(am->index(i, 0), newValue, valueRole);
    }

    // Append any new attributes beyond the existing count
    for (int i = existingCount; i < attrs.size(); ++i) {
        const QVariantMap m = attrs[i].toMap();
        auto info = std::make_shared<AttrInfo>();
        info->name     = m[QStringLiteral("name")].toString();
        info->typeName = m[QStringLiteral("typeName")].toString();
        info->value    = m[QStringLiteral("value")].toString();
        info->isCustom = m[QStringLiteral("isCustom")].toBool();
        info->readOnly = m[QStringLiteral("readOnly")].toBool();
        am->appendItem(info);
    }
}

void UsdDocument::clearAttributes()
{
    auto *am = m_impl->attrModel;
    am->pub_beginResetModel();
    am->removeAllItemsNotNotify();
    am->pub_endResetModel();
}

void *UsdDocument::stagePtr() const
{
    // 调用方须包含 <pxr/usd/usd/stage.h> 并用 UsdStageRefPtr 强转
    return static_cast<void *>(&m_impl->stage);
}

QModelIndex UsdDocument::findPrimModelIndex(const QString &path) const
{
    if (!m_impl->primTreeModel) return {};
    return m_impl->primTreeModel->indexForPath(path);
}

// ---- Undo / Redo ----
void UsdDocument::undo()
{
    m_undoStack->undo();
}

void UsdDocument::redo()
{
    m_undoStack->redo();
}

// ---- Read attribute value as string ----
QString UsdDocument::readAttributeValue(const QString &primPath, const QString &attrName)
{
    if (!m_impl->stage) return {};

    SdfPath sdfPath(primPath.toStdString());
    UsdPrim prim = m_impl->stage->GetPrimAtPath(sdfPath);
    if (!prim.IsValid()) return {};

    UsdAttribute attr = prim.GetAttribute(TfToken(attrName.toStdString()));
    if (!attr.IsValid()) return {};

    VtValue val;
    attr.Get(&val);
    return vtValueToString(val);
}

// ---- Add Attribute ----
bool UsdDocument::addAttribute(const QStringList &primPaths,
                                const QString &attrName,
                                const QString &typeName,
                                bool custom,
                                const QString &variability)
{
    if (!m_impl->stage || primPaths.isEmpty()) return false;

    auto cmd = std::make_unique<AddAttributeCommand>(this, primPaths, attrName, typeName, custom, variability);
    m_undoStack->push(std::move(cmd));
    return true;
}

bool UsdDocument::addAttributeInternal(const QString &primPath,
                                        const QString &attrName,
                                        const QString &typeName,
                                        bool custom,
                                        const QString &variability)
{
    if (!m_impl->stage) return false;

    SdfPath sdfPath(primPath.toStdString());
    UsdPrim prim = m_impl->stage->GetPrimAtPath(sdfPath);
    if (!prim.IsValid()) return false;

    // Map typeName string to SdfValueTypeName
    SdfValueTypeName sdfType = SdfSchema::GetInstance().FindType(TfToken(typeName.toStdString()));
    if (!sdfType) return false;

    // Map variability
    SdfVariability sdfVar = SdfVariabilityVarying;
    if (variability == "Uniform")
        sdfVar = SdfVariabilityUniform;

    UsdAttribute attr = prim.CreateAttribute(TfToken(attrName.toStdString()),
                                              sdfType, custom, sdfVar);
    if (!attr.IsValid()) return false;

    // Set type-appropriate default value so the attribute isn't empty
    VtValue defaultVal = sdfType.GetDefaultValue();
    if (!defaultVal.IsEmpty())
        attr.Set(defaultVal);

    emit stageModified();
    return true;
}

// ---- Get Applied Schemas ----
QStringList UsdDocument::getAppliedSchemas(const QString &primPath)
{
    QStringList result;
    if (!m_impl->stage) return result;

    SdfPath sdfPath(primPath.toStdString());
    UsdPrim prim = m_impl->stage->GetPrimAtPath(sdfPath);
    if (!prim.IsValid()) return result;

    TfTokenVector schemas = prim.GetAppliedSchemas();
    for (const TfToken &tok : schemas)
        result << QString::fromStdString(tok.GetString());
    return result;
}

// ---- Get Available API Schemas ----
QStringList UsdDocument::getAvailableApiSchemas()
{
    QStringList result;

    // Get all registered applied API schema types
    const auto &registry = UsdSchemaRegistry::GetInstance();

    // Iterate all known schema types
    std::set<TfType> apiTypes;
    TfType base = TfType::FindByName("UsdAPISchemaBase");
    if (base.IsUnknown()) return result;
    base.GetAllDerivedTypes(&apiTypes);

    for (const TfType &type : apiTypes) {
        // Only include applied API schemas (not non-applied)
        if (!registry.IsAppliedAPISchema(type))
            continue;

        // Get schema type name token
        TfToken schemaName = UsdSchemaRegistry::GetSchemaTypeName(type);
        if (!schemaName.IsEmpty())
            result << QString::fromStdString(schemaName.GetString());
    }

    result.sort();
    return result;
}

// ---- Apply API Schema ----
bool UsdDocument::applyApiSchema(const QStringList &primPaths, const QString &schemaIdentifier)
{
    if (!m_impl->stage || primPaths.isEmpty()) return false;

    auto cmd = std::make_unique<ApplyApiSchemaCommand>(this, primPaths, schemaIdentifier);
    m_undoStack->push(std::move(cmd));
    return true;
}

bool UsdDocument::applyApiSchemaInternal(const QString &primPath, const QString &schemaIdentifier)
{
    if (!m_impl->stage) return false;

    SdfPath sdfPath(primPath.toStdString());
    UsdPrim prim = m_impl->stage->GetPrimAtPath(sdfPath);
    if (!prim.IsValid()) return false;

    // Record existing attribute names before applying
    std::set<std::string> existingAttrs;
    for (const UsdAttribute &attr : prim.GetAttributes())
        existingAttrs.insert(attr.GetName().GetString());

    bool ok = prim.ApplyAPI(TfToken(schemaIdentifier.toStdString()));
    if (!ok) return false;

    // Set type-appropriate defaults only for NEW attributes from the schema
    for (const UsdAttribute &attr : prim.GetAttributes()) {
        if (existingAttrs.count(attr.GetName().GetString()))
            continue; // existed before, skip
        VtValue val;
        if (!attr.Get(&val) || val.IsEmpty()) {
            VtValue defaultVal = attr.GetTypeName().GetDefaultValue();
            if (!defaultVal.IsEmpty())
                attr.Set(defaultVal);
        }
    }

    emit stageModified();
    return true;
}

// ---- Remove API Schema ----
bool UsdDocument::removeApiSchema(const QStringList &primPaths, const QString &schemaIdentifier)
{
    if (!m_impl->stage || primPaths.isEmpty()) return false;

    auto cmd = std::make_unique<RemoveApiSchemaCommand>(this, primPaths, schemaIdentifier);
    m_undoStack->push(std::move(cmd));
    return true;
}

bool UsdDocument::removeApiSchemaInternal(const QString &primPath, const QString &schemaIdentifier)
{
    if (!m_impl->stage) return false;

    SdfPath sdfPath(primPath.toStdString());
    UsdPrim prim = m_impl->stage->GetPrimAtPath(sdfPath);
    if (!prim.IsValid()) return false;

    // Capture schema-defined property names BEFORE removal
    std::set<std::string> definedBefore;
    for (const TfToken &t : prim.GetPrimDefinition().GetPropertyNames())
        definedBefore.insert(t.GetString());

    bool ok = prim.RemoveAPI(TfToken(schemaIdentifier.toStdString()));
    if (!ok) return false;

    // Capture schema-defined property names AFTER removal
    std::set<std::string> definedAfter;
    for (const TfToken &t : prim.GetPrimDefinition().GetPropertyNames())
        definedAfter.insert(t.GetString());

    // Only remove properties that belonged to the removed schema
    // (in definedBefore but not in definedAfter)
    for (const std::string &name : definedBefore) {
        if (definedAfter.count(name))
            continue; // still defined by type or another schema
        prim.RemoveProperty(TfToken(name));
    }

    emit stageModified();
    return ok;
}

// ---- Remove Attribute ----
bool UsdDocument::removeAttribute(const QStringList &primPaths, const QString &attrName)
{
    if (!m_impl->stage || primPaths.isEmpty()) return false;

    QVector<RemoveAttributeCommand::Entry> entries;
    for (const QString &primPath : primPaths) {
        SdfPath sdfPath(primPath.toStdString());
        UsdPrim prim = m_impl->stage->GetPrimAtPath(sdfPath);
        if (!prim.IsValid()) continue;

        UsdAttribute attr = prim.GetAttribute(TfToken(attrName.toStdString()));
        if (!attr.IsValid()) continue;

        RemoveAttributeCommand::Entry e;
        e.primPath = primPath;
        e.typeName = QString::fromStdString(attr.GetTypeName().GetAsToken().GetString());
        e.custom = attr.IsCustom();
        e.variability = (attr.GetVariability() == SdfVariabilityUniform) ? "Uniform" : "Varying";
        e.oldValue = readAttributeValue(primPath, attrName);
        entries.append(e);
    }

    if (entries.isEmpty()) return false;

    auto cmd = std::make_unique<RemoveAttributeCommand>(this, attrName, entries);
    m_undoStack->push(std::move(cmd));
    return true;
}

bool UsdDocument::removeAttributeInternal(const QString &primPath, const QString &attrName)
{
    if (!m_impl->stage) return false;

    SdfPath sdfPath(primPath.toStdString());
    UsdPrim prim = m_impl->stage->GetPrimAtPath(sdfPath);
    if (!prim.IsValid()) return false;

    bool ok = prim.RemoveProperty(TfToken(attrName.toStdString()));
    if (ok) emit stageModified();
    return ok;
}
