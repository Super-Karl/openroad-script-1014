// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors
#include "OptimizationGraphics.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

#include "UnrepairedGraphics.h"
#include "odb/db.h"
#include "odb/dbBlockCallBackObj.h"
#include "web/core.h"

namespace del {
namespace {
class OptimizationGraphics : public web::Renderer,
                             public odb::dbBlockCallBackObj
{
 public:
  OptimizationGraphics(odb::dbBlock* block,
                       const std::vector<LayerNodes>& layers)
      : layers_(layers), dbu_per_micron_(block->getDbUnitsPerMicron())
  {
    addOwner(block);
    for (const auto& layer : layers_) {
      if (layer.nodes.empty()) {
        continue;
      }
      for (const char* leaf :
           {"trims", "vnodes", "arcs/A", "arcs/B", "arcs/C"}) {
        addDisplayControl(layer.layer + "/" + leaf, true);
      }
      addDisplayControl(layer.layer + "/initial_trims", false);
      addDisplayControl(layer.layer + "/unrepaired_boxes", true);
    }
    web::Gui::get()->registerRenderer(this);
  }
  const char* getDisplayControlGroupName() override { return "DEL"; }
  bool sameFrame(int iteration, bool exact) const
  {
    return iteration_ == iteration && exact_ == exact;
  }
  void setFrame(int iteration, bool exact)
  {
    iteration_ = iteration;
    exact_ = exact;
  }
  void update(const std::vector<std::vector<double>>& positions, bool exact)
  {
    displacement_ = positions;
    boxes_.assign(layers_.size(), {});
    for (size_t l = 0; l < layers_.size(); ++l) {
      for (const auto& arc : layers_[l].arcs) {
        const auto box = unrepairedArcBounds(
            layers_[l], arc, displacement_[l], dbu_per_micron_, exact);
        if (box) {
          boxes_[l].push_back(*box);
        }
      }
    }
  }

  void drawObjects(web::Painter& painter) override
  {
    if (!hasOwner() || displacement_.size() != layers_.size()) {
      return;
    }
    for (size_t l = 0; l < layers_.size(); ++l) {
      drawLayer(painter, l);
      drawUnrepaired(painter, l);
    }
  }

 private:
  void drawLayer(web::Painter& painter, size_t l)
  {
    const auto& layer = layers_[l];
    const auto& bounds = painter.getBounds();
    const auto color = web::Painter::kHighlightColors
        [l % web::Painter::kHighlightColors.size()];
    const bool trims = checkDisplayControl(layer.layer + "/trims");
    const bool nodes = checkDisplayControl(layer.layer + "/vnodes");
    const bool initial = checkDisplayControl(layer.layer + "/initial_trims");
    const int marker
        = std::max(1, static_cast<int>(12 / painter.getPixelsPerDBU()));
    painter.setPen(web::Painter::Color(color, 255), true);
    painter.setBrush(color);
    std::vector<odb::Point> centers;
    centers.reserve(layer.nodes.size());
    for (size_t i = 0; i < layer.nodes.size(); ++i) {
      const auto& node = layer.nodes[i];
      // Only rendering is rounded to DBU; solver coordinates remain continuous.
      const int delta = static_cast<int>(std::llround(displacement_[l][i]));
      const int dx = layer.horizontal ? delta : 0;
      const int dy = layer.horizontal ? 0 : delta;
      const auto p = center(node);
      centers.emplace_back(p.x() + dx, p.y() + dy);
      const auto& b = node.trim;
      const odb::Rect moved(
          b.xMin() + dx, b.yMin() + dy, b.xMax() + dx, b.yMax() + dy);
      if (trims && bounds.intersects(moved)) {
        painter.drawRect(moved);
      }
      if (initial && bounds.intersects(b)) {
        painter.setPen(web::Painter::kGray, true);
        painter.drawLine(b.xMin(), b.yMin(), b.xMax(), b.yMin());
        painter.drawLine(b.xMax(), b.yMin(), b.xMax(), b.yMax());
        painter.drawLine(b.xMax(), b.yMax(), b.xMin(), b.yMax());
        painter.drawLine(b.xMin(), b.yMax(), b.xMin(), b.yMin());
        painter.setPen(web::Painter::Color(color, 255), true);
      }
      if (nodes && bounds.intersects(centers.back())) {
        painter.drawX(centers.back().x(), centers.back().y(), marker);
      }
    }
    for (const auto type : {ArcType::kA, ArcType::kB, ArcType::kC}) {
      if (!checkDisplayControl(layer.layer + "/arcs/" + toString(type))) {
        continue;
      }
      painter.setPen(web::Painter::kHighlightColors[static_cast<size_t>(type)],
                     true);
      for (const auto& arc : layer.arcs) {
        if (arc.type != type) {
          continue;
        }
        const auto& a = centers[arc.src];
        const auto& b = centers[arc.dst];
        if (bounds.intersects(odb::Rect(std::min(a.x(), b.x()),
                                        std::min(a.y(), b.y()),
                                        std::max(a.x(), b.x()),
                                        std::max(a.y(), b.y())))) {
          painter.drawLine(a, b);
        }
      }
    }
  }
  void drawUnrepaired(web::Painter& painter, size_t l)
  {
    if (!checkDisplayControl(layers_[l].layer + "/unrepaired_boxes")) {
      return;
    }
    painter.setPen(web::Painter::kRed, true);
    for (const auto& box : boxes_[l]) {
      if (!painter.getBounds().intersects(box)) {
        continue;
      }
      // Outline only: keep the routed metal visible inside the rectangle.
      painter.drawLine(box.xMin(), box.yMin(), box.xMax(), box.yMin());
      painter.drawLine(box.xMax(), box.yMin(), box.xMax(), box.yMax());
      painter.drawLine(box.xMax(), box.yMax(), box.xMin(), box.yMax());
      painter.drawLine(box.xMin(), box.yMax(), box.xMin(), box.yMin());
    }
  }
  std::vector<LayerNodes> layers_;
  int iteration_ = -1;
  bool exact_ = false;
  double dbu_per_micron_;
  std::vector<std::vector<odb::Rect>> boxes_;
  std::vector<std::vector<double>> displacement_;
};
}  // namespace

bool showOptimizationFrame(odb::dbBlock* block,
                           const std::vector<LayerNodes>& layers,
                           const std::vector<std::vector<double>>& displacement,
                           int iteration,
                           int frame_ms,
                           bool exact)
{
  static std::unique_ptr<OptimizationGraphics> graphics;
  if (iteration == 0) {
    graphics.reset();
    if (web::Gui::enabled() && block != nullptr && !layers.empty()) {
      graphics = std::make_unique<OptimizationGraphics>(block, layers);
    }
  }
  if (graphics != nullptr) {
    if (!graphics->hasOwner()) {
      return false;
    }
    if (graphics->sameFrame(iteration, exact)) {
      web::Gui::get()->status(
          "DEL postprocess running; progress in Tcl console");
      web::Gui::get()->pause(1);
      return graphics->hasOwner();
    }
    graphics->setFrame(iteration, exact);
    graphics->update(displacement, exact);
    web::Gui::get()->status(
        exact ? "DEL postprocess complete: exact trim conflicts"
              : "DEL continuous optimization: iteration "
                    + std::to_string(iteration));
    web::Gui::get()->redraw();
    if (frame_ms > 0) {
      web::Gui::get()->pause(frame_ms);
    }
    return graphics->hasOwner();
  }
  return true;
}
}  // namespace del
