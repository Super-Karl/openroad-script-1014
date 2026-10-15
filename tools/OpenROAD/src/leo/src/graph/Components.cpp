// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <vector>

#include "leo/Graph.h"

namespace leo {
namespace {

// Union find that keeps the smallest node id as the root, so the components
// come out ordered by their first node.
int findRoot(std::vector<int>& parent, int node)
{
  int root = node;
  while (parent[root] != root) {
    root = parent[root];
  }
  while (parent[node] != root) {
    const int next = parent[node];
    parent[node] = root;
    node = next;
  }
  return root;
}

void unite(std::vector<int>& parent, int a, int b)
{
  const int root_a = findRoot(parent, a);
  const int root_b = findRoot(parent, b);
  if (root_a == root_b) {
    return;
  }
  if (root_a < root_b) {
    parent[root_b] = root_a;
  } else {
    parent[root_a] = root_b;
  }
}

}  // namespace

void collectComponents(LayerNodes& layer)
{
  layer.components.clear();
  const int node_count = static_cast<int>(layer.nodes.size());
  if (node_count == 0) {
    return;
  }

  // Weak components: every arc joins its two endpoints, isolated nodes stay on
  // their own.
  std::vector<int> parent(node_count);
  std::iota(parent.begin(), parent.end(), 0);
  for (const Arc& arc : layer.arcs) {
    unite(parent, arc.src, arc.dst);
  }

  std::vector<std::vector<int>> nodes_of_root(node_count);
  for (int id = 0; id < node_count; ++id) {
    nodes_of_root[findRoot(parent, id)].push_back(id);
  }
  std::vector<std::vector<int>> arcs_of_root(node_count);
  for (const Arc& arc : layer.arcs) {
    arcs_of_root[findRoot(parent, arc.src)].push_back(arc.id);
  }

  // The root is the smallest node id of the component, so walking the roots in
  // order gives the stable component numbering.
  for (int root = 0; root < node_count; ++root) {
    if (nodes_of_root[root].empty()) {
      continue;
    }
    Component component;
    component.id = static_cast<int>(layer.components.size());
    component.nodes = std::move(nodes_of_root[root]);
    component.arcs = std::move(arcs_of_root[root]);
    layer.components.push_back(std::move(component));
  }
}

}  // namespace leo
