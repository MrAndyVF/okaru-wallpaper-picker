#pragma once

// OKARU: estado del carrusel de okaru-wallpaper-picker, sin UI. El índice elegido es entero; la posición que se dibuja
// es continua y lo persigue con un resorte, así que navegar rápido encadena el movimiento sin saltos.

#include <cstddef>
#include <vector>

namespace okaru::wallpaper_picker {

  // Una tarjeta visible: `offset` en anchos de tarjeta respecto al centro (0 = la central).
  struct CarouselSlot {
    std::size_t index = 0;
    float offset = 0.0F;
    float scale = 1.0F;
    float brightness = 1.0F; // las lejanas se oscurecen (opacas: no dejan ver la de detrás)
  };

  // Los índices 0..count-1 de dentro hacia fuera a partir de `start` (start, start+1, start-1, start+2…): el orden en
  // que se precargan las miniaturas, primero las que se van a ver.
  [[nodiscard]] std::vector<std::size_t> outwardOrder(std::size_t count, std::size_t start);

  class Carousel {
  public:
    void setCount(std::size_t count);
    [[nodiscard]] std::size_t count() const noexcept { return m_count; }
    [[nodiscard]] std::size_t index() const noexcept { return m_index; }
    [[nodiscard]] float position() const noexcept { return m_position; }

    // Mueve la selección `delta` tarjetas, sin pasar de los extremos. Devuelve si cambió.
    bool move(int delta);
    bool select(std::size_t index);
    // Salta sin animación (al abrir, sobre el fondo actual).
    void jumpTo(std::size_t index);

    // Avanza el resorte `dtMs` milisegundos. Devuelve si sigue en movimiento (hay que pedir otro cuadro).
    bool step(float dtMs);
    [[nodiscard]] bool settled() const noexcept;

    // Las tarjetas a dibujar: las que están a `radius` o menos del centro, de la más lejana a la central (así la
    // central se dibuja encima).
    [[nodiscard]] std::vector<CarouselSlot> slots(float radius) const;

  private:
    std::size_t m_count = 0;
    std::size_t m_index = 0;
    float m_position = 0.0F;
    float m_velocity = 0.0F;
  };

} // namespace okaru::wallpaper_picker
