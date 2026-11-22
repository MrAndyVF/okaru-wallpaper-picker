#pragma once

// OKARU: interfaz de okaru-wallpaper-picker. Capa a pantalla completa sobre el escritorio desenfocado: un velo que se
// funde, el carrusel de fondos (la central grande, las vecinas más chicas y tenues) y el nombre del enfocado.
// ←/→, la rueda o un clic en una vecina recorren; Enter o clic en la central aplica (en todos los monitores) y Esc
// cierra sin cambios.

#include "apps/wallpaper_picker/carousel.h"
#include "core/timer_manager.h"
#include "render/core/thumbnail_service.h"
#include "shell/wallpaper/panel/wallpaper_scanner.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class Box;
class Image;
class Label;
class Node;
class Renderer;
struct KeyboardEvent;

namespace okaru::apps {
  class OkaruApp;
}

namespace okaru::wallpaper_picker {

  class PickerWindow {
  public:
    explicit PickerWindow(apps::OkaruApp& app);
    ~PickerWindow();

    PickerWindow(const PickerWindow&) = delete;
    PickerWindow& operator=(const PickerWindow&) = delete;

  private:
    // Una tarjeta del carrusel; se reutilizan según lo que entra en pantalla.
    struct Card {
      Image* image = nullptr;
      std::string path; // miniatura pedida (vacía = libre)
      std::uint32_t texture = 0; // textura puesta en la imagen (0 = ninguna aún)
    };
    static constexpr std::size_t kCardPool = 9;
    static constexpr float kVisibleRadius = 3.6F;

    [[nodiscard]] std::unique_ptr<Node> build(float width, float height, float scale);
    [[nodiscard]] bool onKey(const KeyboardEvent& event);
    void beforeRender(Renderer& renderer);

    void loadEntries();
    void onScanComplete();
    // Recoloca tarjetas, velo y textos según el carrusel y el progreso de apertura/cierre.
    void layoutScene(Renderer* renderer);
    void bindCard(Card& card, const std::string& path);
    void releaseCard(Card& card);
    // Precarga: genera en segundo plano la miniatura de cada fondo (queda en la caché en disco) y mantiene en memoria
    // solo las de alrededor de la selección.
    void restartPreload();
    void pumpPreload();
    void releasePreload();
    void startTicking();
    void tick();
    void navigate(int delta);
    void clickAt(float x);
    void apply();
    void close();

    apps::OkaruApp& m_app;
    Carousel m_carousel;
    ThumbnailService m_thumbnails;
    ThumbnailService::Subscription m_thumbnailSub;
    WallpaperScanner m_scanner;
    std::filesystem::path m_directory;
    std::vector<std::filesystem::path> m_entries;

    Node* m_root = nullptr;
    Box* m_veil = nullptr;
    Label* m_name = nullptr;
    Label* m_hint = nullptr;
    Label* m_empty = nullptr;
    std::array<Card, kCardPool> m_cards{};
    float m_width = 0.0F;
    float m_height = 0.0F;
    float m_scale = 1.0F;
    int m_thumbPx = 0;
    std::vector<std::size_t> m_preloadOrder;
    std::size_t m_preloadNext = 0;
    std::vector<std::string> m_preloadInFlight; // pedidas para la caché en disco, aún sin decodificar
    // Retenidas en memoria: los índices [m_residentFirst, m_residentEnd) alrededor de la selección.
    std::size_t m_residentFirst = 0;
    std::size_t m_residentEnd = 0;

    // 0 = cerrado, 1 = abierto. Al aplicar, la central crece hasta llenar la pantalla (m_applyProgress).
    float m_openProgress = 0.0F;
    float m_applyProgress = 0.0F;
    enum class Phase : std::uint8_t { Opening, Open, Closing, Applying };
    Phase m_phase = Phase::Opening;
    Timer m_ticker;
    std::chrono::steady_clock::time_point m_lastTick;
    float m_axisAccum = 0.0F;
  };

} // namespace okaru::wallpaper_picker
