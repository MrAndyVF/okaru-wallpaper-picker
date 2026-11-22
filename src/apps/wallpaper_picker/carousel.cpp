#include "apps/wallpaper_picker/carousel.h"

#include <algorithm>
#include <cmath>

namespace okaru::wallpaper_picker {

  namespace {
    // Resorte críticamente amortiguado: llega rápido y sin rebotar.
    constexpr float kStiffness = 0.00018F; // por ms²
    constexpr float kDamping = 0.0268F;     // 2·√k (crítico), por ms; asienta en ~400 ms
    constexpr float kRestDistance = 0.001F;
    constexpr float kRestVelocity = 0.00001F;
    // Las vecinas se encogen y se oscurecen con la distancia al centro.
    constexpr float kScaleStep = 0.18F;
    constexpr float kMinScale = 0.55F;
    constexpr float kDimStep = 0.22F;
    constexpr float kMinBrightness = 0.4F;
  } // namespace

  std::vector<std::size_t> outwardOrder(std::size_t count, std::size_t start) {
    std::vector<std::size_t> out;
    if (count == 0) {
      return out;
    }
    start = std::min(start, count - 1);
    out.reserve(count);
    out.push_back(start);
    for (std::size_t step = 1; out.size() < count; ++step) {
      if (start + step < count) {
        out.push_back(start + step);
      }
      if (step <= start) {
        out.push_back(start - step);
      }
    }
    return out;
  }

  void Carousel::setCount(std::size_t count) {
    m_count = count;
    if (m_count == 0) {
      m_index = 0;
      m_position = 0.0F;
      m_velocity = 0.0F;
      return;
    }
    m_index = std::min(m_index, m_count - 1);
  }

  bool Carousel::move(int delta) {
    if (m_count == 0) {
      return false;
    }
    const long target = std::clamp(static_cast<long>(m_index) + delta, 0L, static_cast<long>(m_count) - 1);
    return select(static_cast<std::size_t>(target));
  }

  bool Carousel::select(std::size_t index) {
    if (index >= m_count || index == m_index) {
      return false;
    }
    m_index = index;
    return true;
  }

  void Carousel::jumpTo(std::size_t index) {
    if (m_count == 0) {
      return;
    }
    m_index = std::min(index, m_count - 1);
    m_position = static_cast<float>(m_index);
    m_velocity = 0.0F;
  }

  bool Carousel::step(float dtMs) {
    // Pasos cortos: un cuadro largo (ventana tapada) no debe volver inestable el resorte.
    while (dtMs > 0.0F && !settled()) {
      const float dt = std::min(dtMs, 8.0F);
      const float displacement = m_position - static_cast<float>(m_index);
      m_velocity += (-kStiffness * displacement - kDamping * m_velocity) * dt;
      m_position += m_velocity * dt;
      dtMs -= dt;
    }
    if (std::abs(m_position - static_cast<float>(m_index)) < kRestDistance && std::abs(m_velocity) < kRestVelocity) {
      m_position = static_cast<float>(m_index);
      m_velocity = 0.0F;
    }
    return !settled();
  }

  bool Carousel::settled() const noexcept { return m_position == static_cast<float>(m_index) && m_velocity == 0.0F; }

  std::vector<CarouselSlot> Carousel::slots(float radius) const {
    std::vector<CarouselSlot> out;
    if (m_count == 0) {
      return out;
    }
    const long first = std::max(0L, static_cast<long>(std::floor(m_position - radius)));
    const long last = std::min(static_cast<long>(m_count) - 1, static_cast<long>(std::ceil(m_position + radius)));
    for (long i = first; i <= last; ++i) {
      const float offset = static_cast<float>(i) - m_position;
      const float distance = std::abs(offset);
      if (distance > radius) {
        continue;
      }
      out.push_back({
          .index = static_cast<std::size_t>(i),
          .offset = offset,
          .scale = std::max(kMinScale, 1.0F - kScaleStep * distance),
          .brightness = std::max(kMinBrightness, 1.0F - kDimStep * distance),
      });
    }
    std::ranges::sort(out, [](const CarouselSlot& a, const CarouselSlot& b) {
      return std::abs(a.offset) > std::abs(b.offset);
    });
    return out;
  }

} // namespace okaru::wallpaper_picker
