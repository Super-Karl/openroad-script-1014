// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "TrimGraphics.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "odb/dbBlockCallBackObj.h"
#include "web/core.h"

namespace del {
namespace {
class TrimGraphics : public web::Renderer, public odb::dbBlockCallBackObj
{
 public:
  TrimGraphics(odb::dbBlock* block, const std::vector<Trim>& trims)
  {
    addOwner(block);
    for (const auto& trim : trims) {
      layers_[trim.layer].push_back(trim.box);
    }
    for (const auto& [layer, boxes] : layers_) {
      addDisplayControl(layer + "/trims", true);
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
    const auto& bounds = painter.getBounds();
    size_t index = 0;
    for (const auto& [layer, boxes] : layers_) {
      const auto color = web::Painter::kHighlightColors
          [index++ % web::Painter::kHighlightColors.size()];
      if (!checkDisplayControl(layer + "/trims")) {
        continue;
      }
      painter.setPen(web::Painter::Color(color, 255), true);
      painter.setBrush(color);
      for (const auto& box : boxes) {
        if (box.intersects(bounds)) {
          painter.drawRect(box);
        }
      }
    }
  }

 private:
  std::map<std::string, std::vector<odb::Rect>> layers_;
};
}  // namespace

void showTrims(odb::dbBlock* block, const std::vector<Trim>& trims)
{
  static std::unique_ptr<TrimGraphics> graphics;
  graphics.reset();
  if (web::Gui::enabled() && block != nullptr && !trims.empty()) {
    graphics = std::make_unique<TrimGraphics>(block, trims);
  }
  if (web::Gui::enabled()) {
    web::Gui::get()->redraw();
  }
}

}  // namespace del
