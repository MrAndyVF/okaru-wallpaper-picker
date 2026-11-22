#pragma once

// OKARU: interfaz de okaru-wallpaper-picker. Capa a pantalla completa sobre el escritorio desenfocado: un velo que se
// funde, una barra arriba (búsqueda, carpeta, orden, pantalla), el carrusel de fondos (la central grande, las vecinas
// más chicas y oscurecidas), el nombre del enfocado y, debajo, su estrella de favorito y el tema del favorito.
// ←/→, la rueda o un clic en una vecina recorren; Enter o clic en la central aplica (en todas las pantallas salvo que
// se elija una) y Esc borra la búsqueda o cierra sin cambios. Las escrituras de config van por el shell (okaru msg).

#include "apps/wallpaper_picker/carousel.h"
#include "apps/wallpaper_picker/entries.h"
#include "config/config_types.h"
#include "core/timer_manager.h"
#include "render/core/thumbnail_service.h"
#include "shell/wallpaper/panel/wallpaper_scanner.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class Box;
class Button;
class Flex;
class Image;
class Input;
class Label;
class Node;
class Renderer;
class Segmented;
class Select;
struct KeyboardEvent;

namespace okaru::apps {
  class OkaruApp;
}

namespace okaru::wallpaper_picker {

  class PickerWindow {
  public:
    // visible = false: arranca oculto (residente) hasta el primer SIGUSR1.
    PickerWindow(apps::OkaruApp& app, bool visible);
    ~PickerWindow();

    PickerWindow(const PickerWindow&) = delete;
    PickerWindow& operator=(const PickerWindow&) = delete;

  private:
    // Una tarjeta del carrusel; se reutilizan según lo que entra en pantalla.
    struct Card {
      Image* image = nullptr;
      Box* placeholder = nullptr; // fondo de la tarjeta mientras su miniatura carga
      std::string path;          // miniatura pedida (vacía = libre)
      std::uint32_t texture = 0; // textura puesta en la imagen (0 = ninguna aún)
    };
    static constexpr std::size_t kCardPool = 9;
    static constexpr float kVisibleRadius = 3.6F;

    [[nodiscard]] std::unique_ptr<Node> build(float width, float height, float scale);
    [[nodiscard]] std::unique_ptr<Node> buildToolbar(float scale);
    [[nodiscard]] std::unique_ptr<Node> buildInfoCard(float scale);
    [[nodiscard]] bool onKey(const KeyboardEvent& event);
    void beforeRender(Renderer& renderer);

    // Carpeta y escaneo: la raíz siempre (da la lista de subcarpetas) y la elegida si es otra.
    void loadEntries();
    void requestFolderScan();
    void onScanComplete();
    [[nodiscard]] std::vector<std::string> folderLabels() const;
    [[nodiscard]] std::size_t folderIndex() const;
    void setFolder(std::size_t index);
    void setSort(std::size_t index);
    void setQuery(const std::string& query);
    // Vuelve a armar la lista visible conservando el fondo enfocado si sigue en ella.
    void rearrange(const std::filesystem::path& keep);

    [[nodiscard]] std::filesystem::path focusedPath() const;
    [[nodiscard]] const WallpaperFavorite* focusedFavorite() const;
    void toggleFavorite();
    void setFavoriteTheme(std::size_t segment);
    void setFavoritePaletteSource(std::size_t index);
    void setFavoritePalette(std::size_t index);
    // Pone la fila de favorito al día con el fondo enfocado (sin reconstruir la escena).
    void syncFavoriteRow();
    [[nodiscard]] std::vector<std::string> paletteValues(std::optional<PaletteSource> source) const;
    void runShell(std::vector<std::string> args);

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
    void clickAt(float x, float y);
    void apply();
    void close();
    // Residente: mostrar desde cero (estado de apertura) / ocultar al terminar de cerrar o aplicar.
    void open();
    void finish();
    void forgetScene();

    apps::OkaruApp& m_app;
    Carousel m_carousel;
    ThumbnailService m_thumbnails;
    ThumbnailService::Subscription m_thumbnailSub;
    WallpaperScanner m_scanner;
    PickerState m_state;
    std::filesystem::path m_rootDir;
    std::vector<std::string> m_subfolders; // relativas a la raíz
    std::filesystem::path m_scanDir;
    bool m_scanFlatten = false;
    std::vector<Wallpaper> m_all;      // la carpeta elegida, sin filtrar
    std::vector<std::filesystem::path> m_entries; // lo que muestra el carrusel
    std::string m_query;
    std::uint64_t m_seed = 0;
    std::size_t m_monitorIndex = 0; // 0 = todas las pantallas; si no, m_monitors[index - 1]
    std::vector<std::string> m_monitors;
    bool m_firstScan = true;

    Node* m_root = nullptr;
    Box* m_veil = nullptr;
    Node* m_toolbar = nullptr;
    Input* m_search = nullptr;
    Select* m_folderSelect = nullptr;
    Label* m_name = nullptr;
    Label* m_count = nullptr;
    Label* m_hint = nullptr;
    Label* m_empty = nullptr;
    Node* m_infoCard = nullptr; // en la barra: nombre, posición, estrella y tema del favorito
    Node* m_hintCard = nullptr;
    Button* m_starButton = nullptr;
    Node* m_themeControls = nullptr;
    Segmented* m_themeSegmented = nullptr;
    Select* m_sourceSelect = nullptr;
    Select* m_paletteSelect = nullptr;
    bool m_syncingFavorite = false;
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
    std::size_t m_syncedIndex = static_cast<std::size_t>(-1); // índice para el que se armó la fila de favorito

    // 0 = cerrado, 1 = abierto. Al aplicar (m_applyProgress, un solo movimiento): se quita el desenfoque, la central
    // crece un poco y se desvanece, las vecinas salen hacia los lados y el resto se apaga, mientras la transición del
    // shell (disc…) nace desde donde estaba la central y descubre el fondo nuevo.
    float m_openProgress = 0.0F;
    float m_applyProgress = 0.0F;
    enum class Phase : std::uint8_t { Opening, Open, Closing, Applying };
    Phase m_phase = Phase::Opening;
    Timer m_ticker;
    // Pide al shell que decodifique el fondo enfocado cuando el carrusel se detiene en él: al aplicarlo, la transición
    // empieza sin esperar a la imagen.
    Timer m_shellPreload;
    std::chrono::steady_clock::time_point m_lastTick;
    float m_axisAccum = 0.0F;
  };

} // namespace okaru::wallpaper_picker
