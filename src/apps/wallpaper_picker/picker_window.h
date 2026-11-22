#pragma once

// OKARU: interfaz de okaru-wallpaper-picker. Carrusel de fondos a pantalla completa sobre el escritorio desenfocado;
// Enter o clic en la central lo aplica, Esc cierra sin cambios.

#include "apps/wallpaper_picker/carousel.h"

#include <memory>

class Node;
struct KeyboardEvent;

namespace okaru::apps {
  class OkaruApp;
}

namespace okaru::wallpaper_picker {

  class PickerWindow {
  public:
    explicit PickerWindow(apps::OkaruApp& app);

    PickerWindow(const PickerWindow&) = delete;
    PickerWindow& operator=(const PickerWindow&) = delete;

  private:
    [[nodiscard]] std::unique_ptr<Node> build(float width, float height, float scale);
    [[nodiscard]] bool onKey(const KeyboardEvent& event);

    apps::OkaruApp& m_app;
    Carousel m_carousel;
  };

} // namespace okaru::wallpaper_picker
