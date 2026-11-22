#include "apps/wallpaper_picker/entries.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <unistd.h>

using namespace okaru::wallpaper_picker;
namespace fs = std::filesystem;

namespace {
  std::vector<std::string> names(const std::vector<Wallpaper>& list) {
    std::vector<std::string> out;
    for (const auto& w : list) {
      out.push_back(w.path.filename().string());
    }
    return out;
  }
} // namespace

int main() {
  using namespace std::chrono_literals;
  const auto t0 = fs::file_time_type::clock::now();
  const std::vector<Wallpaper> all{
      {"/w/Beach.jpg", t0 - 3h},
      {"/w/aurora.png", t0 - 1h},
      {"/w/City night.jpg", t0 - 2h},
      {"/w/forest.webp", t0},
  };
  const auto noFavorites = [](const fs::path&) { return false; };

  // Nombre sin distinguir mayúsculas, en los dos sentidos.
  assert((names(arrange(all, "", SortMode::NameAsc, 0, noFavorites))
          == std::vector<std::string>{"aurora.png", "Beach.jpg", "City night.jpg", "forest.webp"}));
  assert(names(arrange(all, "", SortMode::NameDesc, 0, noFavorites)).front() == "forest.webp");
  // Fecha.
  assert(names(arrange(all, "", SortMode::Newest, 0, noFavorites)).front() == "forest.webp");
  assert(names(arrange(all, "", SortMode::Oldest, 0, noFavorites)).front() == "Beach.jpg");
  // Búsqueda sin distinguir mayúsculas, también con espacios.
  assert((names(arrange(all, "NIGHT", SortMode::NameAsc, 0, noFavorites)) == std::vector<std::string>{"City night.jpg"}));
  assert(arrange(all, "zzz", SortMode::NameAsc, 0, noFavorites).empty());
  // Aleatorio: la misma semilla, el mismo orden, sin perder ninguno.
  const auto r1 = names(arrange(all, "", SortMode::Random, 42, noFavorites));
  assert(r1 == names(arrange(all, "", SortMode::Random, 42, noFavorites)) && r1.size() == 4);
  // Favoritos primero, conservando el orden dentro de cada grupo.
  const auto favorites = arrange(all, "", SortMode::NameAsc, 0, [](const fs::path& p) {
    return p.filename() == "forest.webp" || p.filename() == "Beach.jpg";
  });
  assert((names(favorites) == std::vector<std::string>{"Beach.jpg", "forest.webp", "aurora.png", "City night.jpg"}));

  // Estado: ida y vuelta, y valores de fábrica si no hay archivo.
  const fs::path file = fs::temp_directory_path() / ("okaru-wp-picker-" + std::to_string(::getpid())) / "state.toml";
  assert(loadState(file).sort == SortMode::NameAsc && loadState(file).folder.empty());
  saveState({.sort = SortMode::Newest, .folder = "anime/dark"}, file);
  const PickerState back = loadState(file);
  assert(back.sort == SortMode::Newest && back.folder == "anime/dark");
  fs::remove_all(file.parent_path());
  return 0;
}
