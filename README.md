# Cosmos Browser

Navegador web hecho desde cero en C por **COS-Mods**. Motor propio: parser HTML tolerante a errores,
CSS básico, layout (bloques, líneas, listas, tablas, formularios dibujados), render por software
y red vía `curl`. Sin WebKit, sin Chromium, sin JavaScript.

Pensado para correr en hardware viejo (probado el diseño para la Vaio con Turion II: solo usa SSE2,
nada de SSSE3/SSE4).

## Compilar (Arch Linux)

    sudo pacman -S --needed base-devel sdl2 curl
    make
    ./cosmos                      # abre cosmos:home
    ./cosmos https://example.com  # o varias URLs / archivos / búsquedas

En Debian/Ubuntu/Void: `libsdl2-dev` + `curl` + `gcc` + `make`.
Las fuentes (DejaVu) van en `fonts/` junto al ejecutable; si faltan busca en `/usr/share/fonts/TTF`.

## Uso

| Tecla / acción | Qué hace |
|---|---|
| `Ctrl+L` / `F6` | barra de direcciones (si no es URL, busca en DuckDuckGo) |
| `Ctrl+T` / `Ctrl+W` / `Ctrl+Tab` | nueva / cerrar / siguiente pestaña |
| `Alt+←` `Alt+→` / `Backspace` | atrás / adelante |
| `F5` / `Ctrl+R` | recargar |
| `F9` / `Ctrl+E` / botón **Aa** | modo lectura (solo el contenido principal) |
| `Ctrl +` `Ctrl -` `Ctrl 0` / `Ctrl+rueda` | zoom |
| rueda, `Re Pág`, `Av Pág`, `Espacio`, `Inicio`, `Fin` | desplazarse |
| clic con rueda / `Ctrl+clic` en enlace | abrir en pestaña nueva |
| `Esc` | detener la carga |

También abre carpetas locales (`/home/carlos` o `file:///`) y arrastrar un archivo a la ventana.

## Captura sin ventana (para probar el motor)

    make headless
    ./cosmos-headless --shot salida.ppm --size 1000x800 [--reader] [--zoom 1.2] [--scroll 400] URL

## Cómo está hecho

| Archivo | Contenido |
|---|---|
| `html.c` | tokenizer + constructor de árbol, entidades, CSS (selectores simples y descendientes), estilos, modo lectura |
| `layout.c` | flujo de bloques/líneas, márgenes colapsables, listas, tablas con ancho automático, controles de formulario |
| `gfx.c` | framebuffer, fuentes TTF (stb_truetype), decodificación PNG/JPG/GIF/BMP (stb_image) |
| `net.c` | `curl` en un proceso hijo, `file://`, resolución de URLs, barra inteligente |
| `main.c` | pestañas, hilos de carga (la UI nunca se congela), historial, eventos SDL2 |
| `pages.c` | `cosmos:home` y `cosmos:about` |

## Limitaciones actuales (y siguientes pasos)

- Sin JavaScript, sin cookies, sin envío de formularios (los campos se dibujan pero no son editables).
- Solo CSS dentro de la página (`<style>` y `style=""`); falta cargar hojas externas, `flex`/`grid`, bordes y fondos en línea.
- Sin selección/copia de texto de la página ni búsqueda en página (`Ctrl+F`).
- Sin SVG/WebP; GIF solo el primer cuadro.
- Ideas: formularios GET funcionales, hojas CSS externas, favoritos, descargas, cache.

## Video (YouTube) — nuevo en 0.2

Cosmos Browser reproduce video sin JavaScript: delega en dos programas externos.

- **yt-dlp** resuelve el enlace de YouTube (título, duración, URL del stream de 360p).
- **ffmpeg** decodifica a cuadros BGRA y audio PCM; el navegador solo los dibuja y los manda a SDL.

Instalar (Arch): `sudo pacman -S yt-dlp ffmpeg` (si yt-dlp se queja de JavaScript: `sudo pacman -S deno` o `nodejs`).
Si YouTube deja de funcionar, casi siempre basta `yt-dlp -U` / actualizar el paquete.

Uso: pega un enlace `youtube.com/watch?v=…`, `youtu.be/…`, `/shorts/…`, o escribe `yt gatos` en la barra para buscar.
También abre archivos `.mp4 .webm .mkv .mp3 …` (locales o por URL).

Teclas: **Espacio/K** pausa · **←/→** ±5 s · **J/L** ±10 s · **↑/↓** volumen · **M** silencio · **F** pantalla completa · clic en la barra para saltar.

Variables: `COSMOS_YTDLP` (otro yt-dlp), `COSMOS_FFMPEG`, `COSMOS_FPS` (por defecto 24; bájalo si la PC va justa).
Prueba sin ventana: `cosmos --shot out.ppm --wait 4 URL` imprime posición y bytes de audio.

## Novedades 0.3

**Video:** calidad seleccionable 144p–1080p con **C** (cicla y da la vuelta; Shift+C baja) o con `-` / `+` (reinicia en la misma posición; por defecto 360p, `COSMOS_QUALITY=480` para cambiarlo).

**CSS (mucho más completo):**
- Hojas de estilo externas (`<link rel=stylesheet>`, `@import`, descarga en paralelo) y `@media` (min/max-width, screen/print, color-scheme claro).
- Selectores: `>`, `+`, `~`, `[attr]`, `[attr^=$*~|=]`, `:first-child`, `:last-child`, `:only-child`, `:nth-child()`, `:not()`, `:root`, `:empty`, `:link`; especificidad y `!important` reales.
- Valores: `var(--x)`, `calc()`, `min()/max()/clamp()`, `vw/vh/rem/em/pt/cm…`, `env()`.
- Cajas: `border*` (ancho/color por lado), `border-radius` (en fondos), `box-sizing`, `min-height`, márgenes y relleno horizontal en elementos en línea, fondos en línea (insignias).
- Texto: `line-height`, `text-transform`, atajo `font`.
- Diseño: `display:flex` (fila, wrap, grow/shrink, `justify-content`, `align-items`, `gap`, `margin:auto`), `display:grid` con columnas iguales (`repeat(n,1fr)`, `auto-fit`), `float`/`clear`, `<img align>`.
- Se ignoran: `position`, animaciones, `:hover`, imágenes de fondo, `box-shadow`.

Páginas de prueba en `tests/` (`cosmos tests/css2.html`).

## Aceleración por hardware (VA-API) — 0.4

ffmpeg decodifica con VA-API usando la GPU. En la Vaio (Radeon HD 4250, decodificador UVD) sirve para **H.264**, que es justo el formato `avc1` que se le pide a YouTube.

Preparar (Arch): `sudo pacman -S libva-mesa-driver libva-utils` y comprobar con `vainfo` que sale `VAProfileH264…`. Si no detecta el driver: `LIBVA_DRIVER_NAME=r600 ./cosmos …`

Modos (`COSMOS_HWACCEL`):
- `auto` (defecto): usa VA-API si existe `/dev/dri/renderD128`.
- `vaapi`: decodifica y escala en la GPU (solo se descarga el cuadro ya pequeño).
- `copy`: decodifica en la GPU y escala en CPU (más compatible).
- `none`: todo en CPU.

Si un modo falla, Cosmos baja solo al siguiente (GPU → copia → CPU) y sigue reproduciendo. En los controles aparece "VA-API" mientras se use. Otro dispositivo: `COSMOS_VAAPI_DEVICE=/dev/dri/renderD129`.
Ojo: la UVD de esa GPU no decodifica VP9 ni AV1; por eso se pide siempre H.264.

## JavaScript — 0.5

Cosmos ahora ejecuta JavaScript con **QuickJS** (quickjs-ng, incluido en `quickjs/`, licencia MIT).

- `js.c`: puente nativo entre QuickJS y el árbol DOM del motor (`Node`). Los nodos JS son envoltorios cacheados de los `Node` en C.
- `prelude.js`: el resto de la API web escrita en JS: `document`, `Element` (classList, style, dataset, atributos…), eventos con captura/burbujeo y `on*` (también atributos `onclick="..."`), `setTimeout/setInterval/requestAnimationFrame` (reloj virtual), `fetch`, `XMLHttpRequest`, `URL`, `URLSearchParams`, `localStorage`, `matchMedia`, `Intl` mínimo, observers (IntersectionObserver avisa "visible" para que carguen imágenes perezosas), `document.write`, scripts insertados dinámicamente, `DOMContentLoaded`/`load`…
- `genprelude.py` convierte `prelude.js` en `prelude.h` al compilar (necesita `python3`).
- Los scripts externos se descargan en paralelo. Los de rastreo/analítica conocidos se omiten. `type=module` no se ejecuta todavía.
- Tras ejecutar los scripts se aplican los estilos y se hace el layout. Los clics se despachan al nodo bajo el cursor (si el JS llama `preventDefault()` no se sigue el enlace). Si el JS cambia el DOM después, la página se re-estiliza y se vuelve a maquetar.
- Límites de seguridad: tiempo máximo de CPU por script, memoria de 384 MB, máx. 60 peticiones por página, `file://` solo desde páginas locales.
- Variables: `COSMOS_NOJS=1` desactiva JS; `COSMOS_JSDEBUG=1` imprime errores y `console.*` en la terminal; `COSMOS_JSEVAL='expr'` evalúa una expresión tras cargar (solo para depurar).
- Pruebas: `tests/js1.html` (DOM, timers, fetch), `tests/js2.html` (clics, burbujeo, preventDefault), `tests/js3.html` (jQuery 3.7 y Vue 3 reales). Prueba con `COSMOS_TEST_CLICK="x,y;x,y" ./cosmos-headless --shot out.ppm URL`.

No soportado aún: módulos ES, Web Workers, WebSocket, canvas, Shadow DOM real, `position`, y varios modelos de CSS modernos (algunas webs hechas con React/Next quedan en blanco por CSS, no por JS).

## Edición WebKit — 0.6

`cosmos-wk` es Cosmos con el motor **WebKitGTK** (el de Safari/GNOME Web): CSS, JS y video completos (YouTube, DuckDuckGo, etc.). La interfaz es propia (COS-Mods): pestañas, barra, tema oscuro, `cosmos://home`, descargas a ~/Descargas, cookies persistentes.

```
sudo pacman -S gtk3 webkit2gtk-4.1 gst-plugins-good gst-plugins-bad gst-libav
make cosmos-wk
./cosmos-wk [URL | búsqueda | yt búsqueda]
```
Atajos: Ctrl+T/W, Ctrl+L, F5, Alt+←/→, Ctrl +/-/0, F11, F12 (inspector), Ctrl+1..8.
Por defecto usa la GPU (aceleración por hardware); `COSMOS_SOFTWARE=1` fuerza render por software si algo se dibuja raro. El motor propio en C sigue en `./cosmos`.

### Lanzador
`./cosmos-browser [URL]` usa WebKit (`cosmos-wk`) si está compilado y, si el sistema no tiene WebKitGTK, el motor propio (`cosmos`). Fuerza uno con `COSMOS_ENGINE=own` o `COSMOS_ENGINE=webkit`. `make` compila los dos cuando puede.

## 0.7 (edición WebKit): historial, marcadores, privado y contraseñas

- `cosmos://history`, `cosmos://bookmarks`, `cosmos://passwords`; estrella para marcar; autocompletado en la barra.
- Modo privado: contexto efímero (sin historial, cookies ni disco); no guarda ni rellena contraseñas.
- Contraseñas: bóveda cifrada AES-256-GCM (scrypt) con contraseña maestra, en `~/.local/share/cosmos-browser/passwords.vault`.
  La maestra NO se puede recuperar. Es un gestor de hobby, sin auditar.
- Extensiones de Firefox: NO compatibles (WebKitGTK no soporta WebExtensions).

### Importar (menú ☰ → "Importar contraseñas / cookies…")
- Contraseñas: CSV exportado de Chrome, Edge, Brave, Firefox o Bitwarden (columnas url, username, password). Borra el CSV después: va sin cifrar.
- Cookies: `cookies.txt` (formato Netscape) o el `cookies.sqlite` de un perfil de Firefox.
- No soportado: cookies y contraseñas de Chrome directo de su perfil (van cifradas con el llavero del sistema).

## 0.8 (edición WebKit): descargas e idiomas

- **Descargas:** van a `~/Download` (si no existe usa la carpeta de descargas del sistema; si tampoco, la crea). Si el archivo ya existe se llama `nombre (1).ext`.
  En la barra aparece un círculo que se rellena de azul según avanza la descarga; al terminar se llena entero con una palomita. Al pulsarlo se abre una ventanita con el historial de descargas (clic en una para abrirla, ✕ para cancelar, abrir la carpeta, limpiar la lista). Atajo: `Ctrl+J`. El historial se guarda en `~/.local/share/cosmos-browser/downloads.tsv` (las descargas de pestañas privadas no).
- **Idiomas:** inglés por defecto. Menú ☰ → Language: English, Español, 日本語, 中文 (simplificado), 한국어, العربية (de derecha a izquierda), Русский, Українська. Se guarda en `settings.conf`.
  Para ver bien CJK y árabe necesitas fuentes: `sudo pacman -S noto-fonts noto-fonts-cjk`.
  Las traducciones están en `gen_i18n.py` (edítalas y ejecuta `python3 gen_i18n.py` para regenerar `wk-i18n.h`).
