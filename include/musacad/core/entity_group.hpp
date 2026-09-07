// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <string>
#include <vector>

#include "musacad/core/entity_handle.hpp"

namespace musacad::core {

/// A named selection set (AutoCAD GROUP). Picking any member selects the whole group
/// while group selection (PICKSTYLE) is on. Members that were erased simply drop out:
/// the handles are validated wherever the group is used, and an empty group is not
/// written to a file.
struct EntityGroup {
    std::string name;        ///< "*A1"-style for unnamed groups, as AutoCAD names them
    std::string description;
    std::vector<EntityHandle> members;
    bool selectable = true;
};

} // namespace musacad::core
