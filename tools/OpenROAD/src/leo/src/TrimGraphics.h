// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <vector>

#include "leo/Trim.h"

namespace odb {
class dbBlock;
}

namespace leo {
void showTrims(odb::dbBlock* block, const std::vector<Trim>& trims);
}  // namespace leo
