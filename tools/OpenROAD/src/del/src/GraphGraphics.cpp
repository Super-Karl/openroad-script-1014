// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "GraphGraphics.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "odb/dbBlockCallBackObj.h"
#include "web/core.h"

namespace del {
namespace {

// Size of a Vnode center marker in screen pixels, so it stays visible at any
// zoom level.
constexpr double kNodeMarkerPixels = 12.0;

web::Painter::Color colorAt(size_t index)
{
  return web::Painter::kHighlightColors
      [index % web::Painter::kHighlightColors.size()];
}

std::string controlName(const std::string& layer, ArcType type)
{
  return layer + "/arcs/" + toString(type);
}

class VnodeGraphics : public web::Renderer, public odb::dbBlockCallBackObj
{
 public:
  VnodeGraphics(odb::dbBlock* block, const std::vector<LayerNodes>& layers)
  {
    addOwner(block);
    for (const LayerNodes& layer : layers) {
      if (layer.nodes.empty()) {
        continue;
      }
      std::vector<odb::Point>& points = points_[layer.layer];
      points.reserve(layer.nodes.size());
      for (const Vnode& node : layer.nodes) {
        points.push_back(center(node));
      }
      addDisplayControl(layer.layer + "/vnodes", true);
    }
    web::Gui::get()->registerRenderer(this);
  }

  const char* getDisplayControlGroupName() override { return "DEL"; }

  void drawObjects(web::Painter& painter) override
  {
    // OpenDB detaches callbacks on block destruction (including read_db).
    if (!hasOwner()) {
      return;
    }
    const int marker = std::max(
        1, static_cast<int>(kNodeMarkerPixels / painter.getPixelsPerDBU()));
    const odb::Rect& bounds = painter.getBounds();
    size_t index = 0;
    for (const auto& [layer, points] : points_) {
      const web::Painter::Color color = colorAt(index++);
      if (!checkDisplayControl(layer + "/vnodes")) {
        continue;
      }
      painter.setPen(web::Painter::Color(color, 255), true);
      for (const odb::Point& point : points) {
        if (bounds.intersects(point)) {
          painter.drawX(point.x(), point.y(), marker);
        }
      }
    }
  }

 private:
  std::map<std::string, std::vector<odb::Point>> points_;
};

class ArcGraphics : public web::Renderer, public odb::dbBlockCallBackObj
{
 public:
  ArcGraphics(odb::dbBlock* block, const std::vector<LayerNodes>& layers)
  {
    addOwner(block);
    for (const LayerNodes& layer : layers) {
      if (layer.nodes.empty()) {
        continue;
      }
      for (const Arc& arc : layer.arcs) {
        // Node ids index into the node list of the same layer.
        segments_[controlName(layer.layer, arc.type)].emplace_back(
            center(layer.nodes[arc.src]), center(layer.nodes[arc.dst]));
      }
      // Keep one switch per rule, so an empty rule is still visible.
      for (const ArcType type : {ArcType::kA, ArcType::kB, ArcType::kC}) {
        addDisplayControl(controlName(layer.layer, type), true);
      }
    }
    web::Gui::get()->registerRenderer(this);
  }

  const char* getDisplayControlGroupName() override { return "DEL"; }

  void drawObjects(web::Painter& painter) override
  {
    if (!hasOwner()) {
      return;
    }
    const odb::Rect& bounds = painter.getBounds();
    size_t index = 0;
    for (const auto& [name, segments] : segments_) {
      const web::Painter::Color color = colorAt(index++);
      if (!checkDisplayControl(name)) {
        continue;
      }
      painter.setPen(web::Painter::Color(color, 255), true);
      for (const auto& [src, dst] : segments) {
        if (bounds.intersects(src) || bounds.intersects(dst)) {
          painter.drawLine(src, dst);
        }
      }
    }
  }

 private:
  std::map<std::string, std::vector<std::pair<odb::Point, odb::Point>>>
      segments_;
};

}  // namespace

void showVnodes(odb::dbBlock* block, const std::vector<LayerNodes>& layers)
{
  static std::unique_ptr<VnodeGraphics> graphics;
  graphics.reset();
  if (web::Gui::enabled() && block != nullptr && !layers.empty()) {
    graphics = std::make_unique<VnodeGraphics>(block, layers);
  }
  if (web::Gui::enabled()) {
    web::Gui::get()->redraw();
  }
}

void showArcs(odb::dbBlock* block, const std::vector<LayerNodes>& layers)
{
  static std::unique_ptr<ArcGraphics> graphics;
  graphics.reset();
  if (web::Gui::enabled() && block != nullptr && !layers.empty()) {
    graphics = std::make_unique<ArcGraphics>(block, layers);
  }
  if (web::Gui::enabled()) {
    web::Gui::get()->redraw();
  }
}

}  // namespace del
