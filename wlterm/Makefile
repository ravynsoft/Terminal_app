CFLAGS=-g -O0 -Wall -ltsm -D_GNU_SOURCE -lm
GTK=`pkg-config --cflags --libs gtk+-3.0 cairo pango pangocairo xkbcommon`
FILES=src/wlterm.c src/wlt_font.c src/wlt_render.c src/shl_htable.c src/shl_pty.c

all:
	gcc -o wlterm $(FILES) $(CFLAGS) $(GTK)
