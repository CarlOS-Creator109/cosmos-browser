# Cosmos Browser - Makefile
# Dependencias: gcc, SDL2 (libsdl2-dev / pacman -S sdl2), curl en el PATH.
CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wno-unused-function -Wno-unused-variable
.DEFAULT_GOAL := all

# 'make' compila el motor propio y, si hay WebKitGTK instalado, también la edición WebKit
HAVE_WK := $(shell pkg-config --exists gtk+-3.0 webkit2gtk-4.1 && echo yes)
all: cosmos
ifeq ($(HAVE_WK),yes)
all: cosmos-wk
else
	@echo "(WebKitGTK no encontrado: solo se compil\xc3\xb3 el motor propio. Para WebKit: pacman -S gtk3 webkit2gtk-4.1)"
endif
SRC     = gfx.c net.c html.c layout.c pages.c player.c js.c main.c
QJS     = quickjs/quickjs.o quickjs/libregexp.o quickjs/libunicode.o quickjs/dtoa.o
QJSFLAGS = -O2 -D_GNU_SOURCE -w -fwrapv

quickjs/%.o: quickjs/%.c
	$(CC) $(QJSFLAGS) -c -o $@ $<

prelude.h: prelude.js genprelude.py
	python3 genprelude.py prelude.js prelude.h || touch prelude.h

cosmos: $(SRC) cosmos.h prelude.h $(QJS)
	$(CC) $(CFLAGS) $(shell sdl2-config --cflags) -o $@ $(SRC) $(QJS) $(shell sdl2-config --libs) -lpthread -lm

# versión sin ventana (solo --shot), para probar el motor sin SDL
headless: cosmos-headless
cosmos-headless: $(SRC) cosmos.h prelude.h $(QJS)
	$(CC) $(CFLAGS) -DHEADLESS -o $@ $(SRC) $(QJS) -lpthread -lm

cosmos-wk: cosmos-wk.c wk-data.h wk-import.h wk-i18n.h
	$(CC) -O2 -Wall -Wno-deprecated-declarations -o $@ cosmos-wk.c $(shell pkg-config --cflags --libs gtk+-3.0 webkit2gtk-4.1 sqlite3) -lgcrypt

clean:
	rm -f cosmos cosmos-headless cosmos-wk quickjs/*.o

.PHONY: all headless clean cosmos-wk
