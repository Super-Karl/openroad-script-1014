// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <vector>

#include "leo/Graph.h"
#include "leo/Trim.h"

namespace odb {
class dbBlock;
class dbTechLayer;
}  // namespace odb

namespace utl {
class Logger;
}

namespace leo {

class Leo
{
 public:
  explicit Leo(utl::Logger* logger);

  void run(int prune_factor = kDefaultPruneFactor);
  const std::vector<Trim>& getTrims() const { return trims_; }
  const std::vector<LayerNodes>& getLayerNodes() const { return layer_nodes_; }

 private:
  odb::dbBlock* init();
  void buildGraph(odb::dbBlock* block,
                  odb::dbTechLayer* layer,
                  int prune_factor);
  void runLpa(LayerNodes& layer);
  void runEma(LayerNodes& layer);

  utl::Logger* logger_;
  std::vector<Trim> trims_;
  std::vector<LayerNodes> layer_nodes_;
};

}  // namespace leo
