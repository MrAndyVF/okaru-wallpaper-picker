#include "apps/wallpaper_picker/picker_window.h"

#include "apps/common/okaru_app.h"
#include "config/config_types.h"
#include "core/input/key_symbols.h"
#include "core/log.h"
#include "core/process/process.h"
#include "i18n/i18n.h"
#include "render/core/renderer.h"
#include "shell/wallpaper/wallpaper_paths.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/image.h"
#include "ui/controls/label.h"
#include "ui/palette.h"
#include "ui/style.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace okaru::wallpaper_picker {

  using i18n::tr;

  namespace {
    constexpr Logger kLog("wallpaper-picker");

    constexpr float kOpenMs = 280.0F;
    constexpr float kCloseMs = 180.0F;
    constexpr float kApplyMs = 360.0F;
    constexpr auto kTickInterval = std::chrono::milliseconds(8);
    // Tamaño de la tarjeta central respecto al ancho de la pantalla, y paso entre tarjetas (en anchos de tarjeta).
    constexpr float kCardWidthRatio = 0.32F;
    // Distancia al centro (en anchos de tarjeta) de una tarjeta a `offset` posiciones: la primera vecina se aparta
    // lo justo para verse casi entera; las siguientes se apilan más juntas detrás.
    constexpr float kFirstGap = 0.76F;
    constexpr float kNextGap = 0.36F;
    float slotDistance(float offset) {
      const float d = std::abs(offset);
      const float x = d <= 1.0F ? d * kFirstGap : kFirstGap + (d - 1.0F) * kNextGap;
      return offset < 0.0F ? -x : x;
    }
    // Inversa de slotDistance, para saber qué tarjeta hay bajo el puntero.
    float slotOffsetAt(float distance) {
      const float d = std::abs(distance);
      const float o = d <= kFirstGap ? d / kFirstGap : 1.0F + (d - kFirstGap) / kNextGap;
      return distance < 0.0F ? -o : o;
    }
    // Un paso de la rueda: un clic de rueda clásica vale 10–15; el touchpad suma pasos pequeños.
    constexpr float kAxisStep = 10.0F;
    constexpr float kVeilAlpha = 0.02F;
    // Miniaturas retenidas en memoria a cada lado de la selección, y cuántas se decodifican a la vez para la caché.
    constexpr std::size_t kResidentRadius = 30;
    constexpr std::size_t kPreloadInFlight = 6;

    float easeOutCubic(float t) {
      const float u = 1.0F - std::clamp(t, 0.0F, 1.0F);
      return 1.0F - u * u * u;
    }

    float lerp(float a, float b, float t) { return a + (b - a) * t; }
  } // namespace

  PickerWindow::PickerWindow(apps::OkaruApp& app) : m_app(app) {
    m_app.setBuild([this](float w, float h, float s) { return build(w, h, s); });
    m_app.setKeyHandler([this](const KeyboardEvent& event) { return onKey(event); });
    m_app.setBeforeRender([this](Renderer& renderer) { beforeRender(renderer); });
    m_app.addPollSource(&m_thumbnails);
    m_app.addPollSource(&m_scanner);
    m_app.setBackdropBlur(true);
    m_app.setBackdropClickCallback([this] { close(); }); // clic en otra pantalla: cerrar sin cambios
    m_thumbnailSub = m_thumbnails.subscribePendingUpload([this] { m_app.requestFrame(); });
    m_scanner.setOnComplete([this] { onScanComplete(); });
    loadEntries();
    startTicking(); // animación de apertura
  }

  PickerWindow::~PickerWindow() {
    m_ticker.stop();
    for (Card& card : m_cards) {
      releaseCard(card);
    }
    releasePreload();
  }

  void PickerWindow::loadEntries() {
    const auto& config = m_app.config().config();
    // shortcut: el modo «auto» se toma como oscuro (la app no ve el servicio de tema del shell); afinarlo si se usan
    // carpetas de fondos distintas para claro y oscuro con el modo automático.
    const ThemeMode configured = shellThemeMode(config.theme);
    const ThemeMode mode = wallpaper::effectiveThemeMode(configured, configured == ThemeMode::Light);
    m_directory = wallpaper::resolveGlobalWallpaperDirectory(config.wallpaper, mode);
    if (m_scanner.requestScan(m_directory, false)) {
      onScanComplete();
    }
  }

  void PickerWindow::onScanComplete() {
    const WallpaperScanResult* result = m_scanner.cached(m_directory, false);
    releasePreload(); // con la lista vieja: sus rutas son las retenidas
    m_entries.clear();
    if (result != nullptr) {
      for (const WallpaperEntry& entry : result->entries) {
        if (!entry.isDir) {
          m_entries.push_back(entry.absPath);
        }
      }
    }
    std::ranges::sort(m_entries, [](const auto& a, const auto& b) { return a.filename() < b.filename(); });
    m_carousel.setCount(m_entries.size());
    // Se abre sobre el fondo actual.
    const std::filesystem::path current = m_app.config().getDefaultWallpaperPath();
    const auto it = std::ranges::find(m_entries, current);
    m_carousel.jumpTo(it != m_entries.end() ? static_cast<std::size_t>(it - m_entries.begin()) : 0);
    restartPreload();
    m_app.requestFrame();
  }

  std::unique_ptr<Node> PickerWindow::build(float width, float height, float scale) {
    for (Card& card : m_cards) {
      releaseCard(card); // la escena anterior (y sus imágenes) se descarta
    }
    m_width = width;
    m_height = height;
    m_scale = scale;

    auto root = ui::node({.width = width, .height = height});
    m_root = root.get();
    root->addChild(ui::box({.out = &m_veil, .width = width, .height = height}));

    // Toda la pantalla: rueda para recorrer, clic para elegir o aplicar.
    root->addChild(ui::inputArea({
        .width = width,
        .height = height,
        .onClick = [this](const InputArea::PointerData& data) { clickAt(data.localX); },
        .onAxis =
            [this](const InputArea::PointerData& data) {
              m_axisAccum += static_cast<float>(data.axisValue);
              while (std::abs(m_axisAccum) >= kAxisStep) {
                const int step = m_axisAccum > 0.0F ? 1 : -1;
                m_axisAccum -= static_cast<float>(step) * kAxisStep;
                navigate(step);
              }
            },
    }));

    const float radius = Style::scaledRadiusLg(scale) * 1.5F;
    for (Card& card : m_cards) {
      root->addChild(ui::image({
          .out = &card.image,
          .fit = ImageFit::Cover,
          .radius = radius,
          .participatesInLayout = false,
      }));
      card.image->setHitTestVisible(false);
    }

    root->addChild(ui::label({
        .out = &m_name,
        .fontSize = Style::fontSizeTitle * scale,
        .color = colorSpecFromRole(ColorRole::OnSurface),
        .maxLines = 1,
    }));
    root->addChild(ui::label({
        .out = &m_hint,
        .text = tr("apps.wallpaper-picker.hint"),
        .fontSize = Style::fontSizeCaption * scale,
        .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
        .maxLines = 1,
    }));
    root->addChild(ui::label({
        .out = &m_empty,
        .fontSize = Style::fontSizeBody * scale,
        .color = colorSpecFromRole(ColorRole::OnSurface),
        .maxLines = 2,
    }));
    return root;
  }

  void PickerWindow::beforeRender(Renderer& renderer) {
    (void)m_thumbnails.uploadPending(renderer.textureManager());
    layoutScene(&renderer);
    pumpPreload();
  }

  void PickerWindow::layoutScene(Renderer* renderer) {
    if (m_root == nullptr || m_veil == nullptr || m_width <= 0.0F) {
      return;
    }
    const float open = easeOutCubic(m_openProgress);
    const float apply = easeOutCubic(m_applyProgress);
    // Al aplicar, el velo se va con el resto: lo que queda es la imagen elegida a pantalla completa.
    // Velo del color de superficie del tema, apenas perceptible: con tema claro, más opaco lo deja todo blanco.
    const ColorSpec veil = colorSpecFromRole(ColorRole::Surface, kVeilAlpha * open * (1.0F - apply));
    m_veil->setFill(veil);
    m_app.setBackdropVeil(veil); // las demás pantallas, igual

    const float cardW = m_width * kCardWidthRatio;
    const float cardH = cardW * m_height / m_width; // la proporción de la pantalla
    const float centerX = m_width * 0.5F;
    const float centerY = m_height * 0.47F;
    const float rise = (1.0F - open) * m_height * 0.05F; // al abrir, las tarjetas suben un poco

    if (renderer != nullptr) {
      const int px = static_cast<int>(std::lround(cardW * std::max(1.0F, renderer->renderScale())));
      if (px != m_thumbPx) {
        for (Card& card : m_cards) {
          releaseCard(card);
        }
        releasePreload();
        m_thumbPx = px;
        restartPreload();
      }
    }

    // Qué tarjeta muestra cada entrada visible: se conservan las que ya la tenían (su miniatura sigue cargada).
    const auto slots = m_carousel.slots(kVisibleRadius);
    std::array<Card*, kCardPool> assigned{};
    std::array<bool, kCardPool> used{};
    for (std::size_t s = 0; s < slots.size() && s < kCardPool; ++s) {
      const std::string& path = m_entries[slots[s].index].string();
      for (std::size_t c = 0; c < kCardPool; ++c) {
        if (!used[c] && m_cards[c].path == path) {
          assigned[s] = &m_cards[c];
          used[c] = true;
          break;
        }
      }
    }
    for (std::size_t s = 0; s < slots.size() && s < kCardPool; ++s) {
      if (assigned[s] != nullptr) {
        continue;
      }
      for (std::size_t c = 0; c < kCardPool; ++c) {
        if (!used[c]) {
          assigned[s] = &m_cards[c];
          used[c] = true;
          break;
        }
      }
    }
    for (std::size_t c = 0; c < kCardPool; ++c) {
      if (!used[c]) {
        releaseCard(m_cards[c]);
        m_cards[c].image->setVisible(false);
      }
    }

    const float baseRadius = Style::scaledRadiusLg(m_scale) * 1.5F;
    for (std::size_t s = 0; s < slots.size() && s < kCardPool; ++s) {
      const CarouselSlot& slot = slots[s];
      Card& card = *assigned[s];
      bindCard(card, m_entries[slot.index].string());

      const bool focused = slot.index == m_carousel.index();
      float scale = slot.scale * lerp(0.9F, 1.0F, open);
      float w = cardW * scale;
      float h = cardH * scale;
      float x = centerX + slotDistance(slot.offset) * cardW - w * 0.5F;
      float y = centerY - h * 0.5F + rise;
      float opacity = open;
      float radius = baseRadius;
      if (m_phase == Phase::Applying) {
        if (focused) {
          // La elegida crece hasta ser el fondo.
          x = lerp(x, 0.0F, apply);
          y = lerp(y, 0.0F, apply);
          w = lerp(w, m_width, apply);
          h = lerp(h, m_height, apply);
          radius = lerp(radius, 0.0F, apply);
          opacity = 1.0F;
        } else {
          opacity *= 1.0F - apply;
        }
      }

      // Orden de dibujo: slots() va de la más lejana a la central.
      const auto z = static_cast<std::int32_t>(s + 10);
      card.image->setZIndex(z);
      card.image->setPosition(x, y);
      card.image->setSize(w, h);
      card.image->setRadius(radius);
      card.image->setOpacity(opacity);
      const float light = focused ? 1.0F : lerp(slot.brightness, 1.0F, apply);
      card.image->setTint(Color{light, light, light, 1.0F});
      card.image->setBorder(colorSpecFromRole(ColorRole::Primary), focused ? 3.0F * m_scale * (1.0F - apply) : 0.0F);
      card.image->setVisible(true);

      if (renderer != nullptr && m_thumbPx > 0) {
        const TextureHandle handle = m_thumbnails.peek(card.path, m_thumbPx);
        if (handle.id.valid() && handle.id.value() != card.texture) {
          card.image->setExternalTexture(*renderer, handle);
          card.texture = handle.id.value();
        }
      }
    }

    // Nombre del enfocado bajo la tarjeta central, y la ayuda abajo.
    const float textOpacity = open * (1.0F - apply);
    const float belowCard = centerY + cardH * 0.5F + rise;
    if (!m_entries.empty() && renderer != nullptr) {
      m_name->setText(m_entries[m_carousel.index()].stem().string());
      m_name->setMaxWidth(cardW);
      m_name->measure(*renderer);
      m_name->setPosition(centerX - m_name->width() * 0.5F, belowCard + Style::spaceLg * m_scale);
    }
    m_name->setOpacity(textOpacity);
    m_name->setVisible(!m_entries.empty());
    m_name->setZIndex(100);
    if (renderer != nullptr) {
      m_hint->measure(*renderer);
      m_hint->setPosition(centerX - m_hint->width() * 0.5F, m_height - m_hint->height() - Style::spaceLg * 2.0F * m_scale);
    }
    m_hint->setOpacity(textOpacity);
    m_hint->setZIndex(100);

    const bool empty = m_entries.empty();
    m_empty->setVisible(empty);
    if (empty && renderer != nullptr) {
      m_empty->setText(tr("apps.wallpaper-picker.empty", "dir", m_directory.string()));
      m_empty->measure(*renderer);
      m_empty->setPosition(centerX - m_empty->width() * 0.5F, centerY - m_empty->height() * 0.5F);
      m_empty->setOpacity(textOpacity);
    }
  }

  void PickerWindow::bindCard(Card& card, const std::string& path) {
    if (card.path == path || m_thumbPx <= 0) {
      return;
    }
    releaseCard(card);
    card.path = path;
    (void)m_thumbnails.acquire(path, m_thumbPx);
  }

  void PickerWindow::releaseCard(Card& card) {
    if (!card.path.empty() && m_thumbPx > 0) {
      m_thumbnails.release(card.path, m_thumbPx);
    }
    card.path.clear();
    card.texture = 0;
  }

  void PickerWindow::restartPreload() {
    releasePreload();
    m_preloadOrder = outwardOrder(m_entries.size(), m_carousel.index());
    m_preloadNext = 0;
    pumpPreload();
  }

  void PickerWindow::pumpPreload() {
    if (m_thumbPx <= 0 || m_entries.empty()) {
      return;
    }
    // Ventana en memoria: entra lo cercano a la selección, sale lo que quedó lejos (solo si cambió).
    const std::size_t index = m_carousel.index();
    const std::size_t first = index > kResidentRadius ? index - kResidentRadius : 0;
    const std::size_t end = std::min(m_entries.size(), index + kResidentRadius + 1);
    if (first != m_residentFirst || end != m_residentEnd) {
      for (std::size_t i = first; i < end; ++i) {
        if (i < m_residentFirst || i >= m_residentEnd) {
          (void)m_thumbnails.acquire(m_entries[i].string(), m_thumbPx);
        }
      }
      for (std::size_t i = m_residentFirst; i < m_residentEnd; ++i) {
        if (i < first || i >= end) {
          m_thumbnails.release(m_entries[i].string(), m_thumbPx);
        }
      }
      m_residentFirst = first;
      m_residentEnd = end;
    }
    // Caché en disco: las ya decodificadas se sueltan y se piden las siguientes, de dentro hacia fuera.
    std::erase_if(m_preloadInFlight, [&](const std::string& path) {
      if (m_thumbnails.peek(path, m_thumbPx).id.valid()) {
        m_thumbnails.release(path, m_thumbPx);
        return true;
      }
      return false;
    });
    while (m_preloadInFlight.size() < kPreloadInFlight && m_preloadNext < m_preloadOrder.size()) {
      const std::size_t i = m_preloadOrder[m_preloadNext++];
      if (i >= m_residentFirst && i < m_residentEnd) {
        continue; // ya la retiene la ventana
      }
      const std::string path = m_entries[i].string();
      (void)m_thumbnails.acquire(path, m_thumbPx);
      m_preloadInFlight.push_back(path);
    }
  }

  void PickerWindow::releasePreload() {
    if (m_thumbPx > 0) {
      for (const std::string& path : m_preloadInFlight) {
        m_thumbnails.release(path, m_thumbPx);
      }
      for (std::size_t i = m_residentFirst; i < m_residentEnd && i < m_entries.size(); ++i) {
        m_thumbnails.release(m_entries[i].string(), m_thumbPx);
      }
    }
    m_preloadInFlight.clear();
    m_residentFirst = 0;
    m_residentEnd = 0;
  }

  void PickerWindow::startTicking() {
    if (m_ticker.active()) {
      return;
    }
    m_lastTick = std::chrono::steady_clock::now();
    m_ticker.startRepeating(kTickInterval, [this] { tick(); });
  }

  void PickerWindow::tick() {
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float, std::milli>(now - m_lastTick).count();
    m_lastTick = now;
    const bool moving = m_carousel.step(dt);
    switch (m_phase) {
    case Phase::Opening:
      m_openProgress = std::min(1.0F, m_openProgress + dt / kOpenMs);
      if (m_openProgress >= 1.0F) {
        m_phase = Phase::Open;
      }
      break;
    case Phase::Closing:
      m_openProgress = std::max(0.0F, m_openProgress - dt / kCloseMs);
      if (m_openProgress <= 0.0F) {
        m_ticker.stop();
        m_app.quit();
        return;
      }
      break;
    case Phase::Applying:
      m_applyProgress = std::min(1.0F, m_applyProgress + dt / kApplyMs);
      if (m_applyProgress >= 1.0F) {
        m_ticker.stop();
        m_app.quit();
        return;
      }
      break;
    case Phase::Open:
      if (!moving) {
        m_ticker.stop();
      }
      break;
    }
    m_app.requestFrame();
  }

  void PickerWindow::navigate(int delta) {
    if ((m_phase == Phase::Open || m_phase == Phase::Opening) && m_carousel.move(delta)) {
      startTicking();
    }
  }

  void PickerWindow::clickAt(float x) {
    if (m_entries.empty() || (m_phase != Phase::Open && m_phase != Phase::Opening)) {
      return;
    }
    // La tarjeta bajo el puntero: la central aplica, una vecina pasa a ser la central.
    const float offset = slotOffsetAt((x - m_width * 0.5F) / (m_width * kCardWidthRatio));
    const long target = std::lround(m_carousel.position() + offset);
    const auto clamped = static_cast<std::size_t>(std::clamp(target, 0L, static_cast<long>(m_entries.size()) - 1));
    if (clamped == m_carousel.index()) {
      apply();
    } else if (m_carousel.select(clamped)) {
      startTicking();
    }
  }

  void PickerWindow::apply() {
    if (m_entries.empty() || m_phase == Phase::Applying || m_phase == Phase::Closing) {
      return;
    }
    const std::string path = m_entries[m_carousel.index()].string();
    // Sin monitor: todos (y el tema del favorito si lo es). El shell funde el fondo debajo mientras la tarjeta crece.
    if (!process::runAsync(std::vector<std::string>{"okaru", "msg", "wallpaper-set", path})) {
      kLog.warn("could not run okaru msg wallpaper-set");
    }
    m_carousel.jumpTo(m_carousel.index()); // la central deja de deslizarse
    m_phase = Phase::Applying;
    startTicking();
  }

  void PickerWindow::close() {
    if (m_phase == Phase::Closing || m_phase == Phase::Applying) {
      return;
    }
    m_phase = Phase::Closing;
    startTicking();
  }

  bool PickerWindow::onKey(const KeyboardEvent& event) {
    if (!event.pressed || event.preedit) {
      return false;
    }
    switch (event.sym) {
    case XKB_KEY_Escape:
      close();
      return true;
    case XKB_KEY_Left:
    case XKB_KEY_h:
      navigate(-1);
      return true;
    case XKB_KEY_Right:
    case XKB_KEY_l:
      navigate(1);
      return true;
    case XKB_KEY_Page_Up:
      navigate(-5);
      return true;
    case XKB_KEY_Page_Down:
      navigate(5);
      return true;
    case XKB_KEY_Home:
      navigate(-static_cast<int>(m_entries.size()));
      return true;
    case XKB_KEY_End:
      navigate(static_cast<int>(m_entries.size()));
      return true;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
    case XKB_KEY_space:
      apply();
      return true;
    default:
      return false;
    }
  }

} // namespace okaru::wallpaper_picker
