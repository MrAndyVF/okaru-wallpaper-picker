#include "apps/wallpaper_picker/picker_window.h"

#include "apps/common/okaru_app.h"
#include "core/input/key_symbols.h"
#include "i18n/i18n.h"
#include "ui/builders.h"
#include "ui/controls/label.h"
#include "ui/style.h"

namespace okaru::wallpaper_picker {

  using i18n::tr;

  PickerWindow::PickerWindow(apps::OkaruApp& app) : m_app(app) {
    m_app.setBuild([this](float w, float h, float s) { return build(w, h, s); });
    m_app.setKeyHandler([this](const KeyboardEvent& event) { return onKey(event); });
  }

  std::unique_ptr<Node> PickerWindow::build(float width, float height, float scale) {
    // shortcut: esqueleto de la fase 0 (solo el título); el carrusel llega en la fase 2.
    auto root = ui::column({
        .align = FlexAlign::Center,
        .justify = FlexJustify::Center,
        .padding = Style::spaceMd * scale,
        .width = width,
        .height = height,
    });
    root->addChild(ui::label({.text = tr("apps.wallpaper-picker.title")}));
    return root;
  }

  bool PickerWindow::onKey(const KeyboardEvent& event) {
    if (!event.pressed || event.preedit) {
      return false;
    }
    if (event.sym == XKB_KEY_Escape) {
      m_app.quit();
      return true;
    }
    return false;
  }

} // namespace okaru::wallpaper_picker
