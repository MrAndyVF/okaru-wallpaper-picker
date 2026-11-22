#pragma once

// OKARU: lista de fondos de okaru-wallpaper-picker, sin UI: filtro por nombre, orden, favoritos primero, y el estado
// que se recuerda entre aperturas (carpeta y orden; la pantalla siempre vuelve a «todas»).

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace okaru::wallpaper_picker {

  enum class SortMode : std::uint8_t { NameAsc, NameDesc, Newest, Oldest, Random };

  struct Wallpaper {
    std::filesystem::path path;
    std::filesystem::file_time_type mtime{};
  };

  // Los que contienen `query` en el nombre (sin distinguir mayúsculas), favoritos primero y cada grupo en `sort`.
  // `seed` fija el orden aleatorio (el mismo mientras el selector está abierto).
  [[nodiscard]] std::vector<Wallpaper> arrange(
      std::vector<Wallpaper> all, std::string_view query, SortMode sort, std::uint64_t seed,
      const std::function<bool(const std::filesystem::path&)>& isFavorite
  );

  // Carpeta elegida: vacía = la raíz, kAllFolders = la raíz con todas sus subcarpetas, si no, una subcarpeta
  // (relativa a la raíz).
  inline constexpr std::string_view kAllFolders = "*";

  struct PickerState {
    SortMode sort = SortMode::NameAsc;
    std::string folder;
  };

  [[nodiscard]] PickerState loadState(const std::filesystem::path& path);
  void saveState(const PickerState& state, const std::filesystem::path& path);

} // namespace okaru::wallpaper_picker
