# Changelog

Each version has an English and a Spanish section. / Cada versión tiene una sección en inglés y otra en español.

## [1.0.0] - 2026-11-22

### English

#### Features
- First release. Replaces OKARU's wallpaper panel (removed in OKARU 1.3.9): your wallpapers in a full-screen carousel
  over the blurred desktop, on the focused screen, with the other screens blurred too.
- Arrows, Page Up/Down, the wheel or a click on a side card browse it with a spring animation. Enter or a click on
  the centered card applies it (to every screen by default): the card pops and fades while the shell's transition is
  born from it. Esc clears the search or closes without changes; clicking another screen closes too.
- Toolbar: search by name, folder (root, a subfolder or all of them), order (name A–Z/Z–A, newest, oldest, random)
  and screen, plus the name, position and favorite star of the centered wallpaper. Folder and order are remembered.
- Favorites: the star (or Ctrl + F) marks the centered wallpaper; favorites come first and can set the theme mode and
  palette applied with them.
- Thumbnails of the whole folder are generated in the background into the disk cache (the nearest first), and the
  shell decodes the wallpaper you stop on, so applying starts at once.
- Resident: OKARU starts it hidden shortly after the shell and Super + Y only shows or hides it, so it opens in a
  frame or two. `okaru-wallpaper-picker --daemon` starts it hidden; running it again toggles it.

### Español

#### Novedades
- Primera versión. Reemplaza al panel de fondos de OKARU (quitado en OKARU 1.3.9): tus fondos en un carrusel a
  pantalla completa sobre el escritorio desenfocado, en la pantalla con el foco, con las demás desenfocadas también.
- Las flechas, RePág/AvPág, la rueda o un clic en una tarjeta lateral lo recorren con una animación elástica. Enter o
  un clic en la central lo aplica (en todas las pantallas por defecto): la tarjeta hace un «pop» y se desvanece
  mientras la transición del shell nace desde ella. Esc borra la búsqueda o cierra sin cambios; un clic en otra
  pantalla también cierra.
- Barra: búsqueda por nombre, carpeta (la principal, una subcarpeta o todas), orden (nombre A–Z/Z–A, más nuevos, más
  antiguos, aleatorio) y pantalla, más el nombre, la posición y la estrella del fondo central. La carpeta y el orden
  se recuerdan.
- Favoritos: la estrella (o Ctrl + F) marca el fondo central; los favoritos van primero y pueden fijar el modo de
  tema y la paleta que se aplican con ellos.
- Las miniaturas de toda la carpeta se generan en segundo plano en la caché en disco (primero las cercanas), y el
  shell decodifica el fondo en el que te detienes, así que aplicar empieza al instante.
- Residente: OKARU lo arranca oculto poco después de la shell y Super + Y solo lo muestra u oculta, así que abre en
  uno o dos cuadros. `okaru-wallpaper-picker --daemon` lo arranca oculto; ejecutarlo otra vez lo alterna.
