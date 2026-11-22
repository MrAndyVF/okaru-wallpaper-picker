#include "apps/wallpaper_picker/entries.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <random>
#include <toml++/toml.hpp>

namespace okaru::wallpaper_picker {

  namespace {
    std::string folded(std::string_view text) {
      std::string out(text);
      std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      return out;
    }
  } // namespace

  std::vector<Wallpaper> arrange(
      std::vector<Wallpaper> all, std::string_view query, SortMode sort, std::uint64_t seed,
      const std::function<bool(const std::filesystem::path&)>& isFavorite
  ) {
    if (!query.empty()) {
      const std::string needle = folded(query);
      std::erase_if(all, [&](const Wallpaper& w) { return !folded(w.path.filename().string()).contains(needle); });
    }
    switch (sort) {
    case SortMode::NameAsc:
    case SortMode::NameDesc: {
      std::ranges::sort(all, [](const Wallpaper& a, const Wallpaper& b) {
        return folded(a.path.filename().string()) < folded(b.path.filename().string());
      });
      if (sort == SortMode::NameDesc) {
        std::ranges::reverse(all);
      }
      break;
    }
    case SortMode::Newest:
      std::ranges::sort(all, [](const Wallpaper& a, const Wallpaper& b) { return a.mtime > b.mtime; });
      break;
    case SortMode::Oldest:
      std::ranges::sort(all, [](const Wallpaper& a, const Wallpaper& b) { return a.mtime < b.mtime; });
      break;
    case SortMode::Random: {
      // Orden estable antes de barajar: la misma semilla da el mismo orden aunque el escaneo cambie el suyo.
      std::ranges::sort(all, [](const Wallpaper& a, const Wallpaper& b) { return a.path < b.path; });
      std::mt19937_64 rng(seed);
      std::ranges::shuffle(all, rng);
      break;
    }
    }
    if (isFavorite) {
      std::ranges::stable_partition(all, [&](const Wallpaper& w) { return isFavorite(w.path); });
    }
    return all;
  }

  PickerState loadState(const std::filesystem::path& path) {
    PickerState state;
    try {
      const toml::table root = toml::parse_file(path.string());
      state.sort = static_cast<SortMode>(std::clamp<int>(root["sort"].value_or(0), 0, 4));
      state.folder = root["folder"].value_or(std::string{});
    } catch (const toml::parse_error&) {
      // Primer arranque o archivo dañado: valores de fábrica.
    }
    return state;
  }

  void saveState(const PickerState& state, const std::filesystem::path& path) {
    const toml::table root{
        {"sort", static_cast<int>(state.sort)},
        {"folder", state.folder},
    };
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path);
    out << root << '\n';
  }

} // namespace okaru::wallpaper_picker
