// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "GraphGraphics.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "odb/dbBlockCallBackObj.h"
#include "web/core.h"

namespace leo {
namespace {

// Size of a Vnode center marker in screen pixels, so it stays visible at any
// zoom level.
constexpr double kNodeMarkerPixels = 12.0;

// Length of a move arrow head in screen pixels.
constexpr double kArrowHeadPixels = 10.0;

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

  const char* getDisplayControlGroupName() override { return "LEO"; }

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

  const char* getDisplayControlGroupName() override { return "LEO"; }

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

class MoveGraphics : public web::Renderer, public odb::dbBlockCallBackObj
{
 public:
  MoveGraphics(odb::dbBlock* block, const std::vector<LayerNodes>& layers)
  {
    addOwner(block);
    for (const LayerNodes& layer : layers) {
      if (layer.nodes.empty()) {
        continue;
      }
      LayerShapes& shapes = moves_[layer.layer];
      for (const Vnode& node : layer.nodes) {
        // Shift the trim box by the move of the LPA stage along the preferred
        // direction: half of the doubled offset is in DBU.
        const int64_t dx = (node.pos_x2 - node.center_x2) / 2;
        const int64_t dy = (node.pos_y2 - node.center_y2) / 2;
        odb::Rect box = node.trim;
        box.moveDelta(static_cast<int>(dx), static_cast<int>(dy));
        shapes.boxes.push_back(box);
        if (moved(node)) {
          shapes.arrows.emplace_back(
              odb::Point(node.center_x2 / 2, node.center_y2 / 2),
              odb::Point(node.pos_x2 / 2, node.pos_y2 / 2));
        }
      }
      addDisplayControl(layer.layer + "/moves", true);
    }
    web::Gui::get()->registerRenderer(this);
  }

  const char* getDisplayControlGroupName() override { return "LEO"; }

  void drawObjects(web::Painter& painter) override
  {
    if (!hasOwner()) {
      return;
    }
    const odb::Rect& bounds = painter.getBounds();
    // Arrow head length in DBU, kept at a fixed size on screen.
    const int head = std::max(
        1, static_cast<int>(kArrowHeadPixels / painter.getPixelsPerDBU()));
    size_t index = 0;
    for (const auto& [layer, shapes] : moves_) {
      const web::Painter::Color color = colorAt(index++);
      if (!checkDisplayControl(layer + "/moves")) {
        continue;
      }
      painter.setPen(web::Painter::Color(color, 255), true);
      painter.setBrush(web::Painter::Color(color, 90));
      for (const odb::Rect& box : shapes.boxes) {
        if (box.intersects(bounds)) {
          painter.drawRect(box);
        }
      }
      for (const auto& [from, to] : shapes.arrows) {
        if (!bounds.intersects(from) && !bounds.intersects(to)) {
          continue;
        }
        painter.drawLine(from, to);
        drawArrowHead(painter, from, to, head);
      }
    }
  }

 private:
  struct LayerShapes
  {
    std::vector<odb::Rect> boxes;
    std::vector<std::pair<odb::Point, odb::Point>> arrows;
  };

  // Triangle at the end of the arrow, pointing at the new position.
  static void drawArrowHead(web::Painter& painter,
                            const odb::Point& from,
                            const odb::Point& to,
                            int head)
  {
    const double dx = to.x() - from.x();
    const double dy = to.y() - from.y();
    const double length = std::sqrt(dx * dx + dy * dy);
    if (length < head) {
      return;
    }
    const double ux = dx / length;
    const double uy = dy / length;
    const int base_x = to.x() - static_cast<int>(ux * head);
    const int base_y = to.y() - static_cast<int>(uy * head);
    const int side = std::max(1, head / 2);
    const std::vector<odb::Point> head_points{
        to,
        odb::Point(base_x - static_cast<int>(uy * side),
                   base_y + static_cast<int>(ux * side)),
        odb::Point(base_x + static_cast<int>(uy * side),
                   base_y - static_cast<int>(ux * side))};
    painter.drawPolygon(head_points);
  }

  std::map<std::string, LayerShapes> moves_;
};

}  // namespace

void showMoves(odb::dbBlock* block, const std::vector<LayerNodes>& layers)
{
  static std::unique_ptr<MoveGraphics> graphics;
  graphics.reset();
  if (web::Gui::enabled() && block != nullptr && !layers.empty()) {
    graphics = std::make_unique<MoveGraphics>(block, layers);
  }
  if (web::Gui::enabled()) {
    web::Gui::get()->redraw();
  }
}

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

}  // namespace leo
