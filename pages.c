/* Cosmos Browser - páginas internas (cosmos:home, cosmos:about) */
#include "cosmos.h"
#include <string.h>

static const char HOME[] =
"<html><head><title>Cosmos Browser</title>\n"
"<style>\n"
"body { background-color: #070914; color: #cfd6ff; margin: 0; }\n"
"h1 { font-size: 56px; color: #5de0ff; text-align: center; margin-top: 56px; margin-bottom: 4px; }\n"
"h1 span { color: #b48cff; }\n"
".sub { text-align: center; color: #8d95c9; font-size: 18px; margin-bottom: 28px; }\n"
".stars { text-align: center; color: #3b4180; font-size: 20px; margin: 4px; }\n"
".stars2 { text-align: center; color: #7b6cff; font-size: 14px; margin: 4px; }\n"
".box { width: 560px; margin-left: auto; margin-right: auto; background-color: #12163a; padding: 18px; }\n"
".box h3 { color: #ffd98a; margin-top: 0; }\n"
"a { color: #7fd3ff; }\n"
".hint { text-align: center; color: #6f77b0; font-size: 14px; margin-top: 24px; }\n"
"</style></head>\n"
"<body>\n"
"<p class=\"stars\">\xc2\xb7 &nbsp; \xe2\x9c\xa6 &nbsp; &nbsp; &nbsp; \xc2\xb7 &nbsp; &nbsp; \xe2\x9c\xa7 &nbsp; &nbsp; &nbsp; &nbsp; \xc2\xb7 &nbsp; &nbsp; \xe2\x9c\xa6 &nbsp; &nbsp; \xc2\xb7 &nbsp; &nbsp; &nbsp; \xe2\x9c\xa7 &nbsp; \xc2\xb7</p>\n"
"<p align=\"center\" style=\"margin-top:28px;margin-bottom:0\"><img src=\"cosmos:logo.png\" width=\"190\" alt=\"Cosmos Browser\"></p>\n"
"<p class=\"sub\">navegador con motor propio &middot; COS-Mods</p>\n"
"<p class=\"stars2\">\xe2\x9c\xa7 &nbsp; &nbsp; \xc2\xb7 &nbsp; &nbsp; &nbsp; \xe2\x9c\xa6 &nbsp; &nbsp; \xc2\xb7 &nbsp; &nbsp; \xe2\x9c\xa7 &nbsp; &nbsp; &nbsp; \xc2\xb7 &nbsp; &nbsp; \xe2\x9c\xa6</p>\n"
"<div class=\"box\">\n"
"<h3>Accesos r\xc3\xa1pidos</h3>\n"
"<p><a href=\"https://html.duckduckgo.com/html/\">DuckDuckGo (versi\xc3\xb3n HTML)</a></p>\n"
"<p><a href=\"https://en.wikipedia.org/wiki/Special:Random\">Wikipedia - art\xc3\xad""culo al azar</a></p>\n"
"<p><a href=\"cosmos:yt?q=musica\">YouTube</a> &middot; escribe <code>yt tu b\xc3\xbasqueda</code> en la barra, o pega un enlace de YouTube</p>\n"
"<p><a href=\"https://news.ycombinator.com/\">Hacker News</a></p>\n"
"<p><a href=\"https://example.com/\">example.com</a></p>\n"
"<p><a href=\"https://info.cern.ch/\">La primera p\xc3\xa1gina web (CERN)</a></p>\n"
"<p><a href=\"file:///\">Archivos de este equipo</a> &middot; <a href=\"cosmos:about\">Acerca de</a></p>\n"
"</div>\n"
"<p class=\"hint\">Escribe una direcci\xc3\xb3n o una b\xc3\xbasqueda en la barra y pulsa Enter.<br>Ctrl+T nueva pesta\xc3\xb1""a &middot; Ctrl+L barra &middot; Ctrl +/- zoom</p>\n"
"<p class=\"stars\">\xc2\xb7 &nbsp; &nbsp; \xe2\x9c\xa7 &nbsp; &nbsp; &nbsp; \xc2\xb7 &nbsp; \xe2\x9c\xa6 &nbsp; &nbsp; &nbsp; &nbsp; \xc2\xb7 &nbsp; &nbsp; &nbsp; \xe2\x9c\xa7 &nbsp; &nbsp; \xc2\xb7</p>\n"
"</body></html>\n";

static const char ABOUT[] =
"<html><head><title>Acerca de Cosmos Browser</title>\n"
"<style>\n"
"body { background-color: #070914; color: #cfd6ff; margin: 24px; }\n"
"h1 { color: #5de0ff; } h2 { color: #ffd98a; } a { color: #7fd3ff; }\n"
"code { color: #b8ffb0; }\n"
"</style></head><body>\n"
"<h1>Cosmos Browser " COSMOS_VERSION "</h1>\n"
"<p>Navegador hecho desde cero en C por <b>COS-Mods</b>. Motor propio: parser HTML, CSS b\xc3\xa1sico, layout, render por software (sin WebKit, sin JavaScript).</p>\n"
"<h2>Qu\xc3\xa9 entiende</h2>\n"
"<ul>\n"
"<li>HTML tolerante a errores (tablas, listas, encabezados, im\xc3\xa1genes PNG/JPG/GIF/BMP, formularios dibujados)</li>\n"
"<li>CSS: colores, fondos, tama\xc3\xb1os, m\xc3\xa1rgenes, padding, width/max-width, text-align, display:none, selectores por etiqueta, clase, id y descendientes</li>\n"
"<li>Enlaces, anclas (#), historial, pesta\xc3\xb1""as, zoom, carpetas locales con <code>file://</code></li>\n"
"</ul>\n"
"<h2>Atajos</h2>\n"
"<ul>\n"
"<li><code>Ctrl+T</code> nueva pesta\xc3\xb1""a &middot; <code>Ctrl+W</code> cerrar &middot; <code>Ctrl+Tab</code> siguiente</li>\n"
"<li><code>Ctrl+L</code> barra de direcciones &middot; <code>F5</code> / <code>Ctrl+R</code> recargar</li>\n"
"<li><code>Alt+\xe2\x86\x90</code> / <code>Alt+\xe2\x86\x92</code> atr\xc3\xa1s / adelante &middot; <code>Alt+Inicio</code> p\xc3\xa1gina de inicio</li>\n"
"<li><code>Ctrl +</code> / <code>Ctrl -</code> / <code>Ctrl 0</code> zoom &middot; rueda del mouse, Re P\xc3\xa1g, Av P\xc3\xa1g, Espacio</li>\n"
"<li>Clic con rueda o <code>Ctrl+clic</code> en un enlace: abrir en pesta\xc3\xb1""a nueva</li>\n"
"</ul>\n"
"<p><a href=\"cosmos:home\">Volver al inicio</a></p>\n"
"</body></html>\n";

const char *builtin_page(const char *name) {
    if (!strcmp(name, "home") || !*name) return HOME;
    if (!strcmp(name, "about")) return ABOUT;
    return NULL;
}
