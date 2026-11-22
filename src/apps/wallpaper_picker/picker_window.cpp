#include "apps/wallpaper_picker/picker_window.h"

#include "apps/common/okaru_app.h"
#include "config/config_types.h"
#include "core/input/key_modifiers.h"
#include "core/input/key_symbols.h"
#include "core/log.h"
#include "core/process/process.h"
#include "i18n/i18n.h"
#include "render/core/renderer.h"
#include "shell/wallpaper/wallpaper_paths.h"
#include "theme/builtin_palettes.h"
#include "theme/community_palettes.h"
#include "theme/custom_palettes.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/button.h"
#include "ui/controls/image.h"
#include "ui/controls/input.h"
#include "ui/controls/label.h"
#include "ui/controls/segmented.h"
#include "ui/controls/select.h"
#include "ui/palette.h"
#include "ui/style.h"
#include "util/file_utils.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <random>

namespace okaru::wallpaper_picker {

  using i18n::tr;
  namespace fs = std::filesystem;

  namespace {
    constexpr Logger kLog("wallpaper-picker");

    constexpr float kOpenMs = 280.0F;
    constexpr float kCloseMs = 180.0F;
    constexpr float kApplyMs = 420.0F;
    // Al aplicar: cuánto crece la central y cuánto se apartan las vecinas (en anchos de tarjeta).
    constexpr float kApplyGrow = 0.12F;
    constexpr float kApplyScatter = 0.9F;
    constexpr auto kTickInterval = std::chrono::milliseconds(8);
    // Tamaño de la tarjeta central respecto al ancho de la pantalla.
    constexpr float kCardWidthRatio = 0.32F;
    constexpr float kCardCenterY = 0.47F; // altura del centro de las tarjetas, respecto a la pantalla
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
    // Lo que tiene que quedarse el carrusel en un fondo para pedir su precarga al shell.
    constexpr auto kShellPreloadDelay = std::chrono::milliseconds(250);

    // Segmentos del modo del favorito y fuentes de paleta, en el orden de los controles.
    constexpr std::array<std::string_view, 3> kThemeKeys{"dark", "light", "auto"};
    constexpr std::array<std::optional<PaletteSource>, 5> kSources{
        std::nullopt, PaletteSource::Builtin, PaletteSource::Wallpaper, PaletteSource::Community,
        PaletteSource::Custom,
    };
    constexpr std::array<std::string_view, 5> kSourceKeys{"none", "builtin", "wallpaper", "community", "custom"};
    constexpr std::array<std::string_view, 10> kWallpaperSchemes{
        "m3-content", "m3-tonal-spot", "m3-fruit-salad", "m3-rainbow",    "m3-monochrome",
        "vibrant",    "faithful",      "soft",           "dysfunctional", "muted",
    };

    fs::path statePath() { return fs::path(FileUtils::stateDir()) / "wallpaper-picker.toml"; }

    float easeOutCubic(float t) {
      const float u = 1.0F - std::clamp(t, 0.0F, 1.0F);
      return 1.0F - u * u * u;
    }

    float lerp(float a, float b, float t) { return a + (b - a) * t; }
  } // namespace

  PickerWindow::PickerWindow(apps::OkaruApp& app) : m_app(app), m_state(loadState(statePath())) {
    m_seed = std::random_device{}();
    m_app.setBuild([this](float w, float h, float s) { return build(w, h, s); });
    m_app.setKeyHandler([this](const KeyboardEvent& event) { return onKey(event); });
    m_app.setBeforeRender([this](Renderer& renderer) { beforeRender(renderer); });
    // La config cambia al marcar favoritos (los escribe el shell): reordenar sin perder el enfocado.
    m_app.setReloadCallback([this] { rearrange(focusedPath()); });
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
    m_shellPreload.stop();
    for (Card& card : m_cards) {
      releaseCard(card);
    }
    releasePreload();
  }

  // ── Carpeta, lista y orden ───────────────────────────────────────────────────────────────────────

  void PickerWindow::loadEntries() {
    const auto& config = m_app.config().config();
    // shortcut: el modo «auto» se toma como oscuro (la app no ve el servicio de tema del shell); afinarlo si se usan
    // carpetas de fondos distintas para claro y oscuro con el modo automático.
    const ThemeMode configured = shellThemeMode(config.theme);
    const ThemeMode mode = wallpaper::effectiveThemeMode(configured, configured == ThemeMode::Light);
    m_rootDir = wallpaper::resolveGlobalWallpaperDirectory(config.wallpaper, mode);
    requestFolderScan();
  }

  void PickerWindow::requestFolderScan() {
    m_scanFlatten = m_state.folder == kAllFolders;
    m_scanDir = m_rootDir;
    if (!m_state.folder.empty() && !m_scanFlatten) {
      std::error_code ec;
      if (fs::is_directory(m_rootDir / m_state.folder, ec)) {
        m_scanDir = m_rootDir / m_state.folder;
      } else {
        m_state.folder.clear(); // la subcarpeta recordada ya no existe
      }
    }
    // La raíz sin aplanar siempre: de ahí sale la lista de subcarpetas.
    const bool rootReady = m_scanner.requestScan(m_rootDir, false);
    const bool folderReady = m_scanner.requestScan(m_scanDir, m_scanFlatten);
    if (rootReady && folderReady) {
      onScanComplete();
    }
  }

  void PickerWindow::onScanComplete() {
    if (const WallpaperScanResult* root = m_scanner.cached(m_rootDir, false); root != nullptr) {
      m_subfolders.clear();
      for (const WallpaperEntry& entry : root->entries) {
        if (entry.isDir) {
          m_subfolders.push_back(fs::relative(entry.absPath, m_rootDir).string());
        }
      }
      std::ranges::sort(m_subfolders);
      if (m_folderSelect != nullptr) {
        m_folderSelect->setOptions(folderLabels());
        m_folderSelect->setSelectedIndexSilently(folderIndex());
      }
    }
    const WallpaperScanResult* folder = m_scanner.cached(m_scanDir, m_scanFlatten);
    if (folder == nullptr) {
      return; // falta el escaneo de la carpeta elegida: llega en otra llamada
    }
    m_all.clear();
    for (const WallpaperEntry& entry : folder->entries) {
      if (!entry.isDir) {
        m_all.push_back({.path = entry.absPath, .mtime = entry.mtime});
      }
    }
    // La primera vez se abre sobre el fondo actual; después, sobre el que estaba enfocado.
    rearrange(m_firstScan ? fs::path(m_app.config().getDefaultWallpaperPath()) : focusedPath());
    m_firstScan = false;
  }

  std::vector<std::string> PickerWindow::folderLabels() const {
    std::vector<std::string> labels{
        tr("apps.wallpaper-picker.folder.root", "name", m_rootDir.filename().string()),
        tr("apps.wallpaper-picker.folder.all"),
    };
    labels.insert(labels.end(), m_subfolders.begin(), m_subfolders.end());
    return labels;
  }

  std::size_t PickerWindow::folderIndex() const {
    if (m_state.folder.empty()) {
      return 0;
    }
    if (m_state.folder == kAllFolders) {
      return 1;
    }
    const auto it = std::ranges::find(m_subfolders, m_state.folder);
    return it != m_subfolders.end() ? 2 + static_cast<std::size_t>(it - m_subfolders.begin()) : 0;
  }

  void PickerWindow::setFolder(std::size_t index) {
    std::string folder;
    if (index == 1) {
      folder = std::string(kAllFolders);
    } else if (index >= 2 && index - 2 < m_subfolders.size()) {
      folder = m_subfolders[index - 2];
    }
    if (folder == m_state.folder) {
      return;
    }
    m_state.folder = std::move(folder);
    saveState(m_state, statePath());
    requestFolderScan();
  }

  void PickerWindow::setSort(std::size_t index) {
    const auto sort = static_cast<SortMode>(std::min<std::size_t>(index, 4));
    if (sort == m_state.sort) {
      return;
    }
    m_state.sort = sort;
    saveState(m_state, statePath());
    rearrange(focusedPath());
  }

  void PickerWindow::setQuery(const std::string& query) {
    if (query == m_query) {
      return;
    }
    m_query = query;
    rearrange(focusedPath());
  }

  void PickerWindow::rearrange(const fs::path& keep) {
    releasePreload(); // con la lista vieja: sus rutas son las retenidas
    const ConfigService& config = m_app.config();
    const auto arranged = arrange(m_all, m_query, m_state.sort, m_seed, [&config](const fs::path& path) {
      return config.isWallpaperFavorite(path.string());
    });
    m_entries.clear();
    m_entries.reserve(arranged.size());
    for (const Wallpaper& wallpaper : arranged) {
      m_entries.push_back(wallpaper.path);
    }
    m_carousel.setCount(m_entries.size());
    const auto it = std::ranges::find(m_entries, keep);
    m_carousel.jumpTo(it != m_entries.end() ? static_cast<std::size_t>(it - m_entries.begin()) : 0);
    m_syncedIndex = static_cast<std::size_t>(-1);
    restartPreload();
    m_app.requestFrame();
  }

  fs::path PickerWindow::focusedPath() const {
    return m_entries.empty() ? fs::path() : m_entries[std::min(m_carousel.index(), m_entries.size() - 1)];
  }

  // ── Favoritos ────────────────────────────────────────────────────────────────────────────────────

  const WallpaperFavorite* PickerWindow::focusedFavorite() const {
    return m_entries.empty() ? nullptr : m_app.config().wallpaperFavorite(focusedPath().string());
  }

  void PickerWindow::runShell(std::vector<std::string> args) {
    args.insert(args.begin(), {"okaru", "msg"});
    if (!process::runAsync(args)) {
      kLog.warn("could not run okaru msg {}", args[2]);
    }
  }

  void PickerWindow::toggleFavorite() {
    if (m_entries.empty() || m_phase != Phase::Open) {
      return;
    }
    runShell({"wallpaper-favorite", focusedFavorite() != nullptr ? "remove" : "add", focusedPath().string()});
  }

  void PickerWindow::setFavoriteTheme(std::size_t segment) {
    if (m_syncingFavorite || focusedFavorite() == nullptr || segment >= kThemeKeys.size()) {
      return;
    }
    runShell({"wallpaper-favorite", "theme", std::string(kThemeKeys[segment]), focusedPath().string()});
  }

  std::vector<std::string> PickerWindow::paletteValues(std::optional<PaletteSource> source) const {
    std::vector<std::string> values;
    if (!source.has_value()) {
      return values;
    }
    switch (*source) {
    case PaletteSource::Builtin:
      for (const auto& palette : okaru::theme::builtinPalettes()) {
        values.emplace_back(palette.name);
      }
      break;
    case PaletteSource::Wallpaper:
      values.assign(kWallpaperSchemes.begin(), kWallpaperSchemes.end());
      break;
    case PaletteSource::Community:
      for (const auto& palette : okaru::theme::availableCommunityPalettes()) {
        values.push_back(palette.name);
      }
      break;
    case PaletteSource::Custom:
      for (const auto& palette : okaru::theme::availableCustomPalettes()) {
        values.push_back(palette.name);
      }
      break;
    }
    return values;
  }

  void PickerWindow::setFavoritePaletteSource(std::size_t index) {
    if (m_syncingFavorite || focusedFavorite() == nullptr || index >= kSources.size()) {
      return;
    }
    // Al cambiar de fuente se toma su primera paleta; luego se elige otra en el selector de al lado.
    const auto values = paletteValues(kSources[index]);
    runShell({"wallpaper-favorite", "palette", std::string(kSourceKeys[index]), values.empty() ? "-" : values.front(),
              focusedPath().string()});
  }

  void PickerWindow::setFavoritePalette(std::size_t index) {
    const WallpaperFavorite* favorite = focusedFavorite();
    if (m_syncingFavorite || favorite == nullptr || !favorite->paletteSource.has_value()) {
      return;
    }
    const auto values = paletteValues(favorite->paletteSource);
    if (index >= values.size()) {
      return;
    }
    const auto source = static_cast<std::size_t>(
        std::ranges::find(kSources, favorite->paletteSource) - kSources.begin()
    );
    runShell({"wallpaper-favorite", "palette", std::string(kSourceKeys[source]), values[index], focusedPath().string()}
    );
  }

  void PickerWindow::syncFavoriteRow() {
    if (m_starButton == nullptr) {
      return;
    }
    m_syncingFavorite = true;
    const WallpaperFavorite* favorite = focusedFavorite();
    m_starButton->setGlyph(favorite != nullptr ? "star-filled" : "star");
    m_starButton->setTooltip(tr(favorite != nullptr ? "apps.wallpaper-picker.unfavorite" : "apps.wallpaper-picker.favorite"));
    m_themeControls->setVisible(favorite != nullptr);
    if (favorite != nullptr) {
      const ThemeMode mode = favorite->themeMode.value_or(ThemeMode::Auto);
      m_themeSegmented->setSelectedIndex(mode == ThemeMode::Dark ? 0 : mode == ThemeMode::Light ? 1 : 2);
      const auto source = static_cast<std::size_t>(
          std::ranges::find(kSources, favorite->paletteSource) - kSources.begin()
      );
      m_sourceSelect->setSelectedIndexSilently(source < kSources.size() ? source : 0);

      const auto values = paletteValues(favorite->paletteSource);
      m_paletteSelect->setVisible(!values.empty());
      if (!values.empty()) {
        std::vector<std::string> labels;
        for (const auto& value : values) {
          labels.push_back(
              favorite->paletteSource == PaletteSource::Wallpaper ? tr(std::format("theme.scheme.{}", value)) : value
          );
        }
        std::string selected;
        switch (*favorite->paletteSource) {
        case PaletteSource::Builtin:
          selected = favorite->builtinPalette;
          break;
        case PaletteSource::Wallpaper:
          selected = favorite->wallpaperScheme;
          break;
        case PaletteSource::Community:
          selected = favorite->communityPalette;
          break;
        case PaletteSource::Custom:
          selected = favorite->customPalette;
          break;
        }
        m_paletteSelect->setOptions(std::move(labels));
        const auto it = std::ranges::find(values, selected);
        m_paletteSelect->setSelectedIndexSilently(it != values.end() ? static_cast<std::size_t>(it - values.begin()) : 0);
      }
    }
    m_syncingFavorite = false;
  }

  // ── Escena ───────────────────────────────────────────────────────────────────────────────────────

  std::unique_ptr<Node> PickerWindow::build(float width, float height, float scale) {
    for (Card& card : m_cards) {
      releaseCard(card); // la escena anterior (y sus imágenes) se descarta
    }
    m_width = width;
    m_height = height;
    m_scale = scale;
    m_syncedIndex = static_cast<std::size_t>(-1);
    if (m_monitors.empty()) {
      for (const auto& output : m_app.outputs()) {
        if (output.done && !output.connectorName.empty()) {
          m_monitors.push_back(output.connectorName);
        }
      }
    }

    auto root = ui::node({.width = width, .height = height});
    m_root = root.get();
    root->addChild(ui::box({.out = &m_veil, .width = width, .height = height}));

    // Toda la pantalla: rueda para recorrer, clic para elegir o aplicar.
    root->addChild(ui::inputArea({
        .width = width,
        .height = height,
        .onClick = [this](const InputArea::PointerData& data) { clickAt(data.localX, data.localY); },
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
        .out = &m_count,
        .fontSize = Style::fontSizeCaption * scale,
        .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
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
    root->addChild(buildToolbar(scale));
    root->addChild(buildFavoriteRow(scale));
    m_app.setAfterBuild([this] {
      if (m_search != nullptr) {
        m_app.focus(m_search->inputArea()); // escribir busca directamente
      }
    });
    return root;
  }

  std::unique_ptr<Node> PickerWindow::buildToolbar(float scale) {
    std::vector<std::string> monitorLabels{tr("apps.wallpaper-picker.monitor.all")};
    monitorLabels.insert(monitorLabels.end(), m_monitors.begin(), m_monitors.end());
    std::vector<std::string> sortLabels;
    for (const char* key : {"name-asc", "name-desc", "newest", "oldest", "random"}) {
      sortLabels.push_back(tr(std::format("apps.wallpaper-picker.sort.{}", key)));
    }

    auto toolbar = ui::row({
        .align = FlexAlign::Center,
        .gap = Style::spaceSm * scale,
        .padding = Style::spaceSm * scale,
        .fill = colorSpecFromRole(ColorRole::SurfaceContainer, 0.85F),
        .radius = Style::scaledRadiusLg(scale),
    });
    m_toolbar = toolbar.get();
    toolbar->addChild(ui::input({
        .out = &m_search,
        .value = m_query,
        .placeholder = tr("apps.wallpaper-picker.search"),
        .clearButtonEnabled = true,
        .width = 260.0F * scale,
        .onChange = [this](const std::string& value) { setQuery(value); },
    }));
    toolbar->addChild(ui::select({
        .out = &m_folderSelect,
        .options = folderLabels(),
        .selectedIndex = folderIndex(),
        .width = 200.0F * scale,
        .onSelectionChanged = [this](std::size_t index, std::string_view) { setFolder(index); },
    }));
    toolbar->addChild(ui::select({
        .options = std::move(sortLabels),
        .selectedIndex = static_cast<std::size_t>(m_state.sort),
        .width = 170.0F * scale,
        .onSelectionChanged = [this](std::size_t index, std::string_view) { setSort(index); },
    }));
    toolbar->addChild(ui::select({
        .options = std::move(monitorLabels),
        .selectedIndex = m_monitorIndex,
        .width = 170.0F * scale,
        .onSelectionChanged = [this](std::size_t index, std::string_view) { m_monitorIndex = index; },
    }));
    toolbar->setZIndex(200);
    return toolbar;
  }

  std::unique_ptr<Node> PickerWindow::buildFavoriteRow(float scale) {
    auto row = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm * scale});
    m_favoriteRow = row.get();
    row->addChild(ui::button({
        .out = &m_starButton,
        .glyph = "star",
        .variant = ButtonVariant::Ghost,
        .onClick = [this] { toggleFavorite(); },
    }));

    std::vector<ui::SegmentedOption> modes{
        {.label = tr("settings.options.theme.mode.dark")},
        {.label = tr("settings.options.theme.mode.light")},
        {.label = tr("common.states.auto")},
    };
    std::vector<std::string> sources{
        tr("apps.wallpaper-picker.palette-global"),
        tr("settings.options.theme.source.built-in"),
        tr("settings.options.theme.source.wallpaper"),
        tr("settings.options.theme.source.community"),
        tr("settings.options.theme.source.custom"),
    };
    auto controls = ui::row({
        .align = FlexAlign::Center,
        .gap = Style::spaceSm * scale,
        .padding = Style::spaceXs * scale,
        .fill = colorSpecFromRole(ColorRole::SurfaceContainer, 0.85F),
        .radius = Style::scaledRadiusLg(scale),
    });
    m_themeControls = controls.get();
    controls->addChild(ui::segmented({
        .out = &m_themeSegmented,
        .options = std::move(modes),
        .selectedIndex = 2,
        .onChange = [this](std::size_t index) { setFavoriteTheme(index); },
    }));
    controls->addChild(ui::select({
        .out = &m_sourceSelect,
        .options = std::move(sources),
        .selectedIndex = 0,
        .width = 170.0F * scale,
        .onSelectionChanged = [this](std::size_t index, std::string_view) { setFavoritePaletteSource(index); },
    }));
    controls->addChild(ui::select({
        .out = &m_paletteSelect,
        .options = std::vector<std::string>{},
        .width = 190.0F * scale,
        .onSelectionChanged = [this](std::size_t index, std::string_view) { setFavoritePalette(index); },
    }));
    controls->setVisible(false);
    row->addChild(std::move(controls));
    row->setZIndex(200);
    return row;
  }

  void PickerWindow::beforeRender(Renderer& renderer) {
    (void)m_thumbnails.uploadPending(renderer.textureManager());
    if (m_syncedIndex != m_carousel.index()) {
      syncFavoriteRow();
      m_syncedIndex = m_carousel.index();
      if (!m_entries.empty() && focusedPath() != fs::path(m_app.config().getDefaultWallpaperPath())) {
        m_shellPreload.start(kShellPreloadDelay, [this] {
          if (m_phase == Phase::Open && !m_entries.empty()) {
            runShell({"wallpaper-preload", focusedPath().string()});
          }
        });
      }
      m_root->layout(renderer); // la fila cambió de ancho (controles del favorito)
    }
    layoutScene(&renderer);
    pumpPreload();
  }

  void PickerWindow::layoutScene(Renderer* renderer) {
    if (m_root == nullptr || m_veil == nullptr || m_width <= 0.0F) {
      return;
    }
    const float open = easeOutCubic(m_openProgress);
    const float apply = easeOutCubic(m_applyProgress);
    // Velo del color de superficie del tema, apenas perceptible: con tema claro, más opaco lo deja todo blanco. Al
    // aplicar, se va con el resto: lo que queda es la imagen elegida a pantalla completa.
    const ColorSpec veil = colorSpecFromRole(ColorRole::Surface, kVeilAlpha * open * (1.0F - apply));
    m_veil->setFill(veil);
    m_app.setBackdropVeil(veil); // las demás pantallas, igual

    const float cardW = m_width * kCardWidthRatio;
    const float cardH = cardW * m_height / m_width; // la proporción de la pantalla
    const float centerX = m_width * 0.5F;
    const float centerY = m_height * kCardCenterY;
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
      const float scale = slot.scale * lerp(0.9F, 1.0F, open);
      float w = cardW * scale;
      float h = cardH * scale;
      float x = centerX + slotDistance(slot.offset) * cardW - w * 0.5F;
      float y = centerY - h * 0.5F + rise;
      float opacity = open;
      float radius = baseRadius;
      if (m_phase == Phase::Applying) {
        if (focused) {
          // Un «pop»: crece un poco desde su centro mientras se desvanece; detrás nace la transición del shell.
          const float grow = 1.0F + kApplyGrow * apply;
          x -= w * (grow - 1.0F) * 0.5F;
          y -= h * (grow - 1.0F) * 0.5F;
          w *= grow;
          h *= grow;
          opacity = 1.0F - apply;
        } else {
          // Las vecinas salen hacia su lado.
          x += (slot.offset < 0.0F ? -1.0F : 1.0F) * kApplyScatter * cardW * apply;
          opacity *= 1.0F - apply;
        }
      }

      // Orden de dibujo: slots() va de la más lejana a la central.
      card.image->setZIndex(static_cast<std::int32_t>(s + 10));
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


    // Barra arriba; nombre, posición y favorito bajo la tarjeta central; ayuda abajo.
    const float textOpacity = open * (1.0F - apply);
    const float slide = (1.0F - open) * 24.0F * m_scale;
    m_toolbar->setPosition(centerX - m_toolbar->width() * 0.5F, Style::spaceLg * 2.0F * m_scale - slide);
    m_toolbar->setOpacity(textOpacity);

    float y = centerY + cardH * 0.5F + rise + Style::spaceLg * m_scale;
    const bool empty = m_entries.empty();
    if (!empty && renderer != nullptr) {
      m_name->setText(m_entries[m_carousel.index()].stem().string());
      m_name->setMaxWidth(cardW);
      m_name->measure(*renderer);
      m_count->setText(std::format("{} / {}", m_carousel.index() + 1, m_entries.size()));
      m_count->measure(*renderer);
    }
    m_name->setPosition(centerX - m_name->width() * 0.5F, y);
    y += m_name->height() + Style::spaceXs * m_scale;
    m_count->setPosition(centerX - m_count->width() * 0.5F, y);
    y += m_count->height() + Style::spaceSm * m_scale;
    m_favoriteRow->setPosition(centerX - m_favoriteRow->width() * 0.5F, y);
    for (Node* node : {static_cast<Node*>(m_name), static_cast<Node*>(m_count), m_favoriteRow}) {
      node->setOpacity(textOpacity);
      node->setVisible(!empty);
      node->setZIndex(100);
    }
    m_favoriteRow->setZIndex(200);

    if (renderer != nullptr) {
      m_hint->measure(*renderer);
    }
    m_hint->setPosition(centerX - m_hint->width() * 0.5F, m_height - m_hint->height() - Style::spaceLg * 2.0F * m_scale);
    m_hint->setOpacity(textOpacity);
    m_hint->setZIndex(100);

    m_empty->setVisible(empty);
    if (empty && renderer != nullptr) {
      m_empty->setText(
          m_query.empty() ? tr("apps.wallpaper-picker.empty", "dir", m_scanDir.string())
                          : tr("apps.wallpaper-picker.no-match", "query", m_query)
      );
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

  // ── Precarga ─────────────────────────────────────────────────────────────────────────────────────

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

  // ── Animación y entrada ──────────────────────────────────────────────────────────────────────────

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

  void PickerWindow::clickAt(float x, float y) {
    if (m_entries.empty() || (m_phase != Phase::Open && m_phase != Phase::Opening)) {
      return;
    }
    // Fuera de la franja de las tarjetas no hace nada (la barra y la fila de favorito tienen sus controles).
    const float cardH = m_width * kCardWidthRatio * m_height / m_width;
    if (std::abs(y - m_height * kCardCenterY) > cardH * 0.5F) {
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
    // Sin pantalla: todas (y el tema del favorito si lo es). --origin: la transición del shell nace en el centro de la
    // tarjeta (coordenadas del escritorio); en las demás pantallas entra desde ese lado.
    std::vector<std::string> args{"wallpaper-set"};
    for (const auto& output : m_app.outputs()) {
      if (output.connectorName == m_app.mainOutputName()) {
        args.emplace_back("--origin");
        args.push_back(std::format(
            "{},{}", static_cast<float>(output.logicalX) + m_width * 0.5F,
            static_cast<float>(output.logicalY) + m_height * kCardCenterY
        ));
        break;
      }
    }
    if (m_monitorIndex > 0 && m_monitorIndex <= m_monitors.size()) {
      args.push_back(m_monitors[m_monitorIndex - 1]);
    }
    args.push_back(focusedPath().string());
    runShell(std::move(args));
    m_app.setBackdropBlur(false); // el escritorio se ve nítido mientras la transición avanza
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
    // El buscador tiene el foco: las letras van a él; estas teclas son del carrusel.
    const bool ctrl = (event.modifiers & KeyMod::Ctrl) != 0;
    if (ctrl && event.sym == XKB_KEY_f) {
      toggleFavorite();
      return true;
    }
    switch (event.sym) {
    case XKB_KEY_Escape:
      if (!m_query.empty() && m_search != nullptr) {
        m_search->setValue("");
        setQuery("");
      } else {
        close();
      }
      return true;
    case XKB_KEY_Left:
      navigate(-1);
      return true;
    case XKB_KEY_Right:
      navigate(1);
      return true;
    case XKB_KEY_Page_Up:
      navigate(-5);
      return true;
    case XKB_KEY_Page_Down:
      navigate(5);
      return true;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
      apply();
      return true;
    default:
      return false;
    }
  }

} // namespace okaru::wallpaper_picker
