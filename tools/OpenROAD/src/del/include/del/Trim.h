// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <string>
#include <vector>

#include "odb/geom.h"

namespace del {

struct Trim
{
  odb::Rect box;
  std::string layer;
  unsigned int net_id = 0;
  // True when the trim sits outside the metal end at the high coordinate of
  // the layer's preferred direction, false at the low coordinate end.
  bool high_end = false;
};

// Extract exposed end edges of the union, with trims outside the metal.
std::vector<Trim> makeTrimBoxes(const std::vector<odb::Rect>& metal,
                                int width,
                                bool horizontal,
                                std::string layer,
                                unsigned int net_id);

}  // namespace del
