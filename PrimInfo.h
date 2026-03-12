#pragma once
#include <string>
#include <prism/prism.hpp>
#include <prism/qt/core/hpp/prismQt.hpp>

struct PrimInfo {
    std::string name;
    std::string path;
    std::string typeName;
    bool isActive = true;
};

PRISM_FIELDS(PrimInfo, name, path, typeName, isActive)
PRISMQT_CLASS(PrimInfo)
