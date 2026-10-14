// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "delgpu/Rules.h"
#include "delgpu/Trim.h"
#include "odb/geom.h"

namespace delgpu {

// One potential trim shape is one node v_i of the constraint graph, so a layer
// holds exactly one Vnode per trim. The position that LPA and EMA move is the
// trim center; it is kept doubled (center_x2, center_y2) because a center can
// fall on a half DBU. Distances between Vnodes are computed from these centers.
struct Vnode
{
  int id = 0;             // per-layer index into LayerNodes::nodes
  size_t trim_index = 0;  // global trim ordinal across the processed layers
  unsigned int net_id = 0;
  odb::Rect trim;
  int64_t center_x2 = 0;  // trim box xMin + xMax
  int64_t center_y2 = 0;  // trim box yMin + yMax
  // Side of the metal end the trim belongs to, as in Trim::high_end.
  bool high_end = false;
  // Movement along the preferred direction, doubled. l_move2 goes towards the
  // low coordinates, u_move2 towards the high ones, both at most move_limit.
  int64_t l_move2 = 0;
  int64_t u_move2 = 0;
  // Domain [low2, up2] of the trim center along the preferred direction,
  // doubled: low2 = center - l_move2, up2 = center + u_move2. A node whose trim
  // already overlaps fixed metal has an empty domain and no movement.
  int64_t low2 = 0;
  int64_t up2 = 0;
  bool blocked = false;
};

// Trim center in DBU; a center on a half DBU is truncated towards zero.
inline odb::Point center(const Vnode& node)
{
  return odb::Point(node.center_x2 / 2, node.center_y2 / 2);
}

// Trim center along the preferred direction, doubled.
inline int64_t along2(const Vnode& node, bool horizontal)
{
  return horizontal ? node.center_x2 : node.center_y2;
}

// Trim center across the tracks, doubled.
inline int64_t across2(const Vnode& node, bool horizontal)
{
  return horizontal ? node.center_y2 : node.center_x2;
}

enum class ArcType
{
  kA,  // [D, +inf): same-track separation that must be kept
  kB,  // [0, 0] u [Dt, +inf): same-track trims that may merge
  kC   // (-inf, -Du] u [0, 0] u [Du, +inf): adjacent-track trims
};

const char* toString(ArcType type);

// One interval of legal x_dst - x_src values of an arc. Bounds are inclusive
// and doubled, like the node centers; a bound without a value is infinite.
struct ArcRange
{
  bool has_low = true;
  int64_t low2 = 0;
  bool has_high = true;
  int64_t high2 = 0;
};

// An arc constrains x_dst - x_src to the union of its ranges, where x is the
// trim center along the preferred direction.
struct Arc
{
  int id = 0;
  int src = 0;
  int dst = 0;
  ArcType type = ArcType::kA;
  std::vector<ArcRange> ranges;
};

// All Vnodes of one routing layer, with the rules that apply to them.
struct LayerNodes
{
  std::string layer;
  bool horizontal = true;  // preferred routing direction of the layer
  int64_t pitch = 0;       // track pitch in DBU, 0 when the layer has none
  LayerRules rules;
  std::vector<Vnode> nodes;
  std::vector<Arc> arcs;
};

// One Vnode per trim in [begin, end). Ids are assigned in the preferred
// direction of the layer: id 0 is the leftmost trim on a horizontal layer and
// the bottom trim on a vertical layer. Nodes with the same center are ordered
// by net and trim index, so ids only depend on the geometry.
std::vector<Vnode> makeVnodes(const std::vector<Trim>& trims,
                              size_t begin,
                              size_t end,
                              bool horizontal);

// Compute the movement domain [low2, up2] of every Vnode of one layer. A trim
// slides along the preferred direction until it would overlap fixed metal; the
// travel is capped at LayerRules::move_limit. Only the given obstacles (the
// dbWire metal of every net, including the net of the node itself) are used,
// so a trim never cuts into metal. Nodes whose trim already overlaps an
// obstacle stay at their initial position with blocked set.
void computeDomains(LayerNodes& layer, const std::vector<odb::Rect>& obstacles);

// Build the A/B/C arcs of one layer from its Vnodes. Two nodes are related
// when they can reach a distance that breaks a rule:
//   same track, facing ends  -> B (may merge or stay Dt apart)
//   same track, other pair   -> A (must stay D apart)
//   adjacent tracks          -> C (may merge or stay Du apart)
// A pair whose whole reachable distance range is already legal gets no arc.
void buildArcs(LayerNodes& layer);

// True when [lo2, hi2] lies inside the union of the ranges, with endpoints
// included. The ranges must be sorted by their low bound.
bool coversRange(const std::vector<ArcRange>& ranges, int64_t lo2, int64_t hi2);

// Trim center offsets between two Vnodes, doubled to stay exact on a half DBU.
// ds2 follows the preferred routing direction, dt2 is across the tracks.
struct CenterOffset
{
  int64_t ds2 = 0;
  int64_t dt2 = 0;
};

CenterOffset centerOffset(const Vnode& a, const Vnode& b, bool horizontal);

// Twice the distance between the trim centers, in DBU. It is an integer so it
// can be compared exactly against a doubled rule threshold (2 * Dt, 2 * Du).
int64_t centerDistance2(const Vnode& a, const Vnode& b);

// Distance between the trim centers in DBU, for reporting only.
double centerDistance(const Vnode& a, const Vnode& b);

}  // namespace delgpu
