/*
 * wlterm - Wayland Terminal
 *
 * Copyright (c) 2011-2013 David Herrmann <dh.herrmann@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * TODO
 */

#ifndef WLT_WLTERM_H
#define WLT_WLTERM_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/* types */

struct wlt_font;
struct wlt_face;
struct wlt_renderer;

/* fonts */

enum wlt_glyph_format {
	WLT_GLYPH_INVALID,
	WLT_GLYPH_A1,
	WLT_GLYPH_A8,
	WLT_GLYPH_RGB24,
};

struct wlt_glyph {
	unsigned long id;

	unsigned int cwidth;
	unsigned int format;
	unsigned int width;
	int stride;
	unsigned int height;
	uint8_t *buffer;
	void *cr_surface;
};

#define WLT_FACE_DONT_CARE (-1)

int wlt_font_new(struct wlt_font **out);
void wlt_font_ref(struct wlt_font *font);
void wlt_font_unref(struct wlt_font *font);

int wlt_face_new(struct wlt_face **out, struct wlt_font *font,
		 const char *desc_str, int desc_size, int desc_bold,
		 int desc_italic);
void wlt_face_ref(struct wlt_face *face);
void wlt_face_unref(struct wlt_face *face);
unsigned int wlt_face_get_width(struct wlt_face *face);
unsigned int wlt_face_get_height(struct wlt_face *face);

int wlt_face_render(struct wlt_face *face, struct wlt_glyph **out,
		    unsigned long id, const uint32_t *ch, size_t len,
		    size_t cwidth);

/* rendering */

struct wlt_draw_ctx {
	bool debug;
	struct wlt_renderer *rend;
	cairo_t *cr;
	struct wlt_face *face;
	unsigned int cell_width;
	unsigned int cell_height;
	struct tsm_screen *screen;
	struct tsm_vte *vte;

	double x1;
	double y1;
	double x2;
	double y2;
};

int wlt_renderer_new(struct wlt_renderer **out, unsigned int width,
		     unsigned int height);
void wlt_renderer_free(struct wlt_renderer *rend);
int wlt_renderer_resize(struct wlt_renderer *rend, unsigned int width,
			unsigned int height);
void wlt_renderer_dirty(struct wlt_renderer *rend);
void wlt_renderer_draw(const struct wlt_draw_ctx *ctx);

#endif /* WLT_WLTERM_H */
