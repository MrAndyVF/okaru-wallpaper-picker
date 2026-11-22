#include "apps/wallpaper_picker/carousel.h"

#include <cassert>
#include <cmath>
#include <vector>

using okaru::wallpaper_picker::Carousel;

int main() {
  Carousel carousel;
  assert(!carousel.move(1) && carousel.slots(2.0F).empty()); // vacío: nada que mover ni dibujar

  carousel.setCount(5);
  carousel.jumpTo(9); // fuera de rango: se queda en la última
  assert(carousel.index() == 4 && carousel.position() == 4.0F && carousel.settled());

  // Los extremos no se pasan.
  assert(!carousel.move(3) && carousel.index() == 4);
  assert(carousel.move(-10) && carousel.index() == 0);

  // El resorte llega al índice en menos de un segundo y sin pasarse (críticamente amortiguado).
  float minPosition = carousel.position();
  for (int frame = 0; frame < 60 && carousel.step(16.0F); ++frame) {
    minPosition = std::min(minPosition, carousel.position());
  }
  assert(carousel.settled() && carousel.position() == 0.0F && minPosition >= -0.01F);

  // Tarjetas visibles: la central al final (se dibuja encima), las vecinas más chicas y tenues.
  carousel.jumpTo(2);
  const auto slots = carousel.slots(1.0F);
  assert(slots.size() == 3);
  assert(slots.back().index == 2 && slots.back().offset == 0.0F && slots.back().scale == 1.0F);
  assert(slots.front().scale < 1.0F && slots.front().brightness < 1.0F);

  // Menos tarjetas: la selección se ajusta.
  carousel.setCount(2);
  assert(carousel.index() == 1);

  // Orden de precarga: de dentro hacia fuera, sin repetir ni saltarse ninguno.
  using okaru::wallpaper_picker::outwardOrder;
  assert(outwardOrder(0, 3).empty());
  assert((outwardOrder(5, 1) == std::vector<std::size_t>{1, 2, 0, 3, 4}));
  assert((outwardOrder(4, 9) == std::vector<std::size_t>{3, 2, 1, 0})); // start fuera de rango: la última
  return 0;
}
