# okaru-wallpaper-picker

**English** · [Español](#español)

The wallpaper picker of [OKARU](https://github.com/MrAndyVF/okaru) (郁): your wallpapers in a full-screen carousel over
the blurred desktop. **Super + Y** opens and closes it; arrows, the wheel or a click move through the carousel, Enter or
a click on the centered one applies it (to every monitor by default) and Esc closes without changes.

## Install

It ships in OKARU's pacman repository `[okaru]` (set up by OKARU's installer):

```sh
sudo pacman -S okaru-wallpaper-picker
```

## Build

It builds against OKARU's shared core, provided by the `okaru-core` package:

```sh
meson setup build && meson compile -C build
meson test -C build
# Run it with the translations from this repository:
OKARU_APPS_DIR=$PWD/apps ./build/okaru-wallpaper-picker
```

Translations (English and Spanish) live in `apps/wallpaper-picker/translations/`.

## License

MIT.

---

## Español

El selector de fondos de pantalla de [OKARU](https://github.com/MrAndyVF/okaru) (郁): tus fondos en un carrusel a
pantalla completa sobre el escritorio desenfocado. **Super + Y** lo abre y lo cierra; las flechas, la rueda o un clic
recorren el carrusel, Enter o un clic en el central lo aplica (en todos los monitores por defecto) y Esc cierra sin
cambios.

### Instalar

Viene en el repositorio pacman de OKARU, `[okaru]` (lo configura el instalador de OKARU):

```sh
sudo pacman -S okaru-wallpaper-picker
```

### Compilar

Se compila contra el núcleo compartido de OKARU, que trae el paquete `okaru-core`:

```sh
meson setup build && meson compile -C build
meson test -C build
# Ejecutarlo con las traducciones de este repositorio:
OKARU_APPS_DIR=$PWD/apps ./build/okaru-wallpaper-picker
```

Las traducciones (inglés y español) están en `apps/wallpaper-picker/translations/`.

### Licencia

MIT.
