# okaru-wallpaper-picker: selector de fondos de pantalla de OKARU (carrusel a pantalla completa sobre el escritorio
# desenfocado). Se compila contra el núcleo instalado (paquete okaru-core).
project('okaru-wallpaper-picker', 'cpp',
  version: files('VERSION'),
  meson_version: '>=1.1',
  default_options: ['cpp_std=c++23', 'cpp_extensions=false', 'warning_level=0', 'b_ndebug=if-release'],
)

# El núcleo y sus bibliotecas son privados (/usr/lib/okaru): se enlazan con su ruta en el binario.
okaru_core = dependency('okaru-core')
_okaru_libdir = get_option('prefix') / get_option('libdir') / 'okaru'
_deps = [okaru_core]
_inc = include_directories('src')
_args = ['-Wall', '-Wextra', '-Wno-missing-field-initializers', '-Wpedantic', '-Wconversion', '-Wshadow']

_logic = files('src/apps/wallpaper_picker/carousel.cpp')

executable('okaru-wallpaper-picker',
  sources: files('src/apps/wallpaper_picker/picker_window.cpp', 'src/apps/wallpaper_picker/main.cpp') + _logic,
  include_directories: _inc,
  dependencies: _deps,
  cpp_args: _args,
  install_rpath: _okaru_libdir,
  install: true,
)

# Sus textos, que el núcleo busca en /usr/share/okaru/apps/wallpaper-picker.
install_subdir('apps/wallpaper-picker/translations',
  install_dir: get_option('datadir') / 'okaru' / 'apps' / 'wallpaper-picker')
install_data('assets/dev.okaru.WallpaperPicker.desktop', install_dir: get_option('datadir') / 'applications')

if get_option('tests').allowed()
  test('okaru_wallpaper_picker_carousel', executable('okaru_wallpaper_picker_carousel_test',
    sources: files('tests/okaru_wallpaper_picker_carousel_test.cpp') + _logic,
    include_directories: _inc,
    dependencies: _deps,
    build_rpath: _okaru_libdir,
    override_options: ['b_ndebug=false'],
  ))
endif
