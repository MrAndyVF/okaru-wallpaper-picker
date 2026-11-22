// OKARU: okaru-wallpaper-picker, el selector de fondos de pantalla de OKARU.
#include "apps/common/okaru_app.h"
#include "apps/wallpaper_picker/picker_window.h"

#include <clocale>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <string>
#include <sys/file.h>
#include <unistd.h>

namespace {

  // Una sola instancia: si ya hay una abierta, se cierra y esta termina (el atajo abre y cierra el selector).
  // Devuelve false si había otra. El descriptor queda abierto (y con el candado) mientras viva el proceso.
  bool claimSingleInstance() {
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    if (runtime == nullptr || runtime[0] == '\0') {
      return true;
    }
    const std::string path = std::string(runtime) + "/okaru-wallpaper-picker.lock";
    const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) {
      return true;
    }
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
      char buffer[32] = {};
      const ssize_t count = ::pread(fd, buffer, sizeof(buffer) - 1, 0);
      const long pid = count > 0 ? std::strtol(buffer, nullptr, 10) : 0;
      if (pid > 1) {
        ::kill(static_cast<pid_t>(pid), SIGTERM);
      }
      ::close(fd);
      return false;
    }
    const std::string pid = std::to_string(::getpid());
    if (::ftruncate(fd, 0) == 0) {
      (void)::pwrite(fd, pid.data(), pid.size(), 0);
    }
    return true;
  }

} // namespace

int main() {
  if (!claimSingleInstance()) {
    return 0;
  }
  std::setlocale(LC_ALL, "");

  okaru::apps::OkaruApp app({
      .appId = "dev.okaru.WallpaperPicker",
      .dataName = "wallpaper-picker",
      .titleKey = "apps.wallpaper-picker.title",
      .width = 640.0F,
      .height = 400.0F,
      .minWidth = 320.0F,
      .minHeight = 240.0F,
      .layer = true,
  });
  okaru::wallpaper_picker::PickerWindow window(app);
  return app.run();
}
