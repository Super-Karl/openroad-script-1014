// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors
#pragma once

#include <optional>
#include <vector>

#include "del/Graph.h"

namespace del {
// Continuous residual classification; outward-rounded box encloses both trims.
std::optional<odb::Rect> unrepairedArcBounds(
    const LayerNodes& layer,
    const Arc& arc,
    const std::vector<double>& displacement_dbu,
    double dbu_per_micron,
    bool exact = false);
}  // namespace del
