#pragma once
#include <QString>
#include <prism/prism.hpp>
#include <prism/qt/core/hpp/prismQt.hpp>

struct AttrInfo {
    QString name;
    QString typeName;
    QString value;
    bool isCustom = false;
    bool readOnly = false;
};

PRISM_FIELDS(AttrInfo, name, typeName, value, isCustom, readOnly)
PRISMQT_CLASS(AttrInfo)
