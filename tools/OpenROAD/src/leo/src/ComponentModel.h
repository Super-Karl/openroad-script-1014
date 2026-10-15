// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once
#include <map>
#include <utility>

#include "leo/Graph.h"
namespace leo {
// Construct independent local-index data for this invocation. Nothing is
// retained between subgraphs or between LPA and EMA.
inline LayerNodes componentModel(const LayerNodes& layer,
                                 const Component& component)
{
  LayerNodes model;
  model.horizontal = layer.horizontal;
  std::map<int, int> local;
  for (int global : component.nodes) {
    const int id = model.nodes.size();
    local.emplace(global, id);
    model.nodes.push_back(layer.nodes[global]);
    model.nodes.back().id = id;
  }
  for (int global : component.arcs) {
    Arc arc = layer.arcs[global];
    arc.id = model.arcs.size();
    arc.src = local.at(arc.src);
    arc.dst = local.at(arc.dst);
    model.arcs.push_back(std::move(arc));
  }
  return model;
}
}  // namespace leo
