// SPDX-License-Identifier: BSD-3-Clause
#include "GraphGraphics.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include "odb/dbBlockCallBackObj.h"
#include "web/core.h"

namespace delgpu {
namespace {
class GraphGraphics : public web::Renderer, public odb::dbBlockCallBackObj
{
 public:
  GraphGraphics(odb::dbBlock* block, std::vector<LayerNodes>&& layers)
      : layers_(std::move(layers)), displacement_(layers_.size())
  {
    addOwner(block);
    for (const auto& layer : layers_) {
      for (const char* name :
           {"trims", "vnodes", "arcs/A", "arcs/B", "arcs/C"}) {
        addDisplayControl(layer.layer + "/" + name, true);
      }
      addDisplayControl(layer.layer + "/domains", false);
    }
    web::Gui::get()->registerRenderer(this);
  }
  const char* getDisplayControlGroupName() override { return "DELGPU"; }
  void update(size_t index, std::vector<double>&& displacement)
  {
    if (!hasOwner()) {
      return;
    }
    displacement_.at(index) = std::move(displacement);
    redraw();
  }
  void drawObjects(web::Painter& painter) override
  {
    if (!hasOwner()) {
      return;
    }
    const auto& bounds = painter.getBounds();
    const int marker
        = std::max(1, static_cast<int>(8.0 / painter.getPixelsPerDBU()));
    const std::array<web::Painter::Color, 3> colors{
        web::Painter::kRed, web::Painter::kGreen, web::Painter::kCyan};
    for (size_t layer_id = 0; layer_id < layers_.size(); ++layer_id) {
      const auto& layer = layers_[layer_id];
      auto offset = [&](int node) {
        return displacement_[layer_id].empty()
                   ? 0
                   : static_cast<int>(
                         std::llround(displacement_[layer_id][node]));
      };
      auto point = [&](const Vnode& node) {
        const auto p = center(node);
        const int shift = offset(node.id);
        return layer.horizontal ? odb::Point(p.x() + shift, p.y())
                                : odb::Point(p.x(), p.y() + shift);
      };
      const bool trims = checkDisplayControl(layer.layer + "/trims");
      const bool nodes = checkDisplayControl(layer.layer + "/vnodes");
      const bool domains = checkDisplayControl(layer.layer + "/domains");
      if (trims || nodes || domains) {
        painter.setPen(web::Painter::kYellow, true);
        painter.setBrush(web::Painter::Color(web::Painter::kYellow, 45));
        for (const auto& node : layer.nodes) {
          const auto p = point(node);
          const int shift = offset(node.id);
          const int dx = layer.horizontal ? shift : 0;
          const int dy = layer.horizontal ? 0 : shift;
          const odb::Rect moved(node.trim.xMin() + dx,
                                node.trim.yMin() + dy,
                                node.trim.xMax() + dx,
                                node.trim.yMax() + dy);
          if (trims && moved.intersects(bounds)) {
            painter.drawRect(moved);
          }
          if (nodes && bounds.intersects(p)) {
            painter.drawX(p.x(), p.y(), marker);
          }
          if (domains) {
            const odb::Point low = layer.horizontal
                                       ? odb::Point(node.low2 / 2, p.y())
                                       : odb::Point(p.x(), node.low2 / 2);
            const odb::Point high = layer.horizontal
                                        ? odb::Point(node.up2 / 2, p.y())
                                        : odb::Point(p.x(), node.up2 / 2);
            const odb::Rect box(std::min(low.x(), high.x()),
                                std::min(low.y(), high.y()),
                                std::max(low.x(), high.x()),
                                std::max(low.y(), high.y()));
            if (box.intersects(bounds)) {
              painter.drawLine(low, high);
            }
          }
        }
      }
      for (const ArcType type : {ArcType::kA, ArcType::kB, ArcType::kC}) {
        if (!checkDisplayControl(layer.layer + "/arcs/" + toString(type))) {
          continue;
        }
        painter.setPen(colors[static_cast<size_t>(type)], true);
        for (const auto& arc : layer.arcs) {
          if (arc.type != type) {
            continue;
          }
          const auto a = point(layer.nodes[arc.src]);
          const auto b = point(layer.nodes[arc.dst]);
          const odb::Rect box(std::min(a.x(), b.x()),
                              std::min(a.y(), b.y()),
                              std::max(a.x(), b.x()),
                              std::max(a.y(), b.y()));
          if (box.intersects(bounds)) {
            painter.drawLine(a, b);
          }
        }
      }
    }
  }

 private:
  std::vector<LayerNodes> layers_;
  std::vector<std::vector<double>> displacement_;
};
std::unique_ptr<GraphGraphics> graphics;
}  // namespace
void clearGraphGraphics()
{
  if (graphics) {
    graphics.reset();
    if (web::Gui::enabled()) {
      web::Gui::get()->redraw();
    }
  }
}
void showGraphGraphics(odb::dbBlock* block, std::vector<LayerNodes>&& layers)
{
  clearGraphGraphics();
  if (web::Gui::enabled() && block != nullptr && !layers.empty()) {
    graphics = std::make_unique<GraphGraphics>(block, std::move(layers));
    web::Gui::get()->redraw();
  }
}
void updateGraphGraphics(size_t layer, std::vector<double>&& displacement)
{
  if (graphics) {
    graphics->update(layer, std::move(displacement));
  }
}
}  // namespace delgpu
