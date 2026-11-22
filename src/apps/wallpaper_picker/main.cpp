// OKARU: okaru-wallpaper-picker, el selector de fondos de pantalla de OKARU.
#include "apps/common/okaru_app.h"
#include "apps/wallpaper_picker/picker_window.h"

#include <clocale>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <string>
#include <string_view>
#include <sys/file.h>
#include <unistd.h>

namespace {

  // Una sola instancia, residente: si ya hay una, se le pide que se muestre u oculte (SIGUSR1) y esta termina; así
  // el atajo abre al instante. Devuelve false si había otra. El descriptor queda abierto (y con el candado) mientras
  // viva el proceso.
  bool claimSingleInstance(bool toggleRunning) {
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
      if (pid > 1 && toggleRunning) {
        ::kill(static_cast<pid_t>(pid), SIGUSR1);
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

int main(int argc, char** argv) {
  // --daemon: arrancar oculto (la shell lo precarga al iniciar la sesión). Si ya hay uno, no se alterna.
  const bool daemon = argc > 1 && std::string_view(argv[1]) == "--daemon";
  if (!claimSingleInstance(!daemon)) {
    return 0;
  }
  std::setlocale(LC_ALL, "");

  okaru::apps::OkaruApp app({
      .appId = "dev.okaru.WallpaperPicker",
      .dataName = "wallpaper-picker",
      .titleKey = "apps.wallpaper-picker.title",
      .layer = true,
      .fullscreen = true,
      .backdropOtherOutputs = true,
      .resident = true,
      .startHidden = daemon,
  });
  okaru::wallpaper_picker::PickerWindow window(app, !daemon);
  return app.run();
}
