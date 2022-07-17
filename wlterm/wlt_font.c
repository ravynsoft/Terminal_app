/*
 * wlterm - pango fonts
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
 * Pango Fonts
 * This helper uses pango to provide font support. Terminals have special
 * requirements for fonts. We need fixed cell sizes, multi-width characters
 * and more. This helper measures fonts and provides fixed glyphs to the
 * caller. No sophisticated font-handling is required by the caller.
 */

#include <cairo.h>
#include <errno.h>
#include <glib.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "wlterm.h"
#include "shl_htable.h"

struct wlt_font {
	unsigned long ref;
	PangoFontMap *map;
};

struct wlt_face {
	unsigned long ref;
	struct wlt_font *font;
	PangoContext *ctx;

	struct shl_htable glyphs;
	unsigned int width;
	unsigned int height;
	unsigned int baseline;
};

#define wlt_to_glyph(_id) \
	shl_htable_offsetof((_id), struct wlt_glyph, id)

static void wlt_glyph_free(struct wlt_glyph *glyph);

int wlt_font_new(struct wlt_font **out)
{
	struct wlt_font *font;
	int r;

	font = calloc(1, sizeof(*font));
	if (!font)
		return -ENOMEM;
	font->ref = 1;

	font->map = pango_cairo_font_map_get_default();
	if (font->map) {
		g_object_ref(font->map);
	} else {
		font->map = pango_cairo_font_map_new();
		if (!font->map) {
			r = -ENOMEM;
			goto err_free;
		}
	}

	*out = font;
	return 0;

err_free:
	free(font);
	return r;
}

void wlt_font_ref(struct wlt_font *font)
{
	if (!font || !font->ref)
		return;

	++font->ref;
}

void wlt_font_unref(struct wlt_font *font)
{
	if (!font || !font->ref || --font->ref)
		return;

	g_object_unref(font->map);
	free(font);
}

static void init_pango_desc(PangoFontDescription *desc, int desc_size,
			    int desc_bold, int desc_italic)
{
	PangoFontMask mask;
	int v;

	if (desc_size != WLT_FACE_DONT_CARE) {
		v = desc_size * PANGO_SCALE;
		if (desc_size > 0 && v > 0)
			pango_font_description_set_absolute_size(desc, v);
	}

	if (desc_bold != WLT_FACE_DONT_CARE) {
		v = desc_bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL;
		pango_font_description_set_weight(desc, v);
	}

	if (desc_italic != WLT_FACE_DONT_CARE) {
		v = desc_italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL;
		pango_font_description_set_style(desc, v);
	}

	pango_font_description_set_variant(desc, PANGO_VARIANT_NORMAL);
	pango_font_description_set_stretch(desc, PANGO_STRETCH_NORMAL);
	pango_font_description_set_gravity(desc, PANGO_GRAVITY_SOUTH);

	mask = pango_font_description_get_set_fields(desc);

	if (!(mask & PANGO_FONT_MASK_FAMILY))
		pango_font_description_set_family(desc, "monospace");
	if (!(mask & PANGO_FONT_MASK_WEIGHT))
		pango_font_description_set_weight(desc, PANGO_WEIGHT_NORMAL);
	if (!(mask & PANGO_FONT_MASK_STYLE))
		pango_font_description_set_style(desc, PANGO_STYLE_NORMAL);
	if (!(mask & PANGO_FONT_MASK_SIZE))
		pango_font_description_set_size(desc, 10 * PANGO_SCALE);
}

/*
 * There is no way to check whether a font is a monospace font. Moreover, there
 * is no "monospace extents" field of fonts that we can use to calculate a
 * suitable cell size. Any bounding-boxes provided by the fonts are mostly
 * useless for cell-size computations. Therefore, we simply render a bunch of
 * ASCII characters and compute the cell-size from these. If you passed a
 * monospace font, it will work out greatly. If you passed some other font, you
 * will get a suitable tradeoff (well, don't do that..).
 */
static void measure_pango(struct wlt_face *face)
{
	static const char str[] = "abcdefghijklmnopqrstuvwxyz"
				  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
				  "@!\"$%&/()=?\\}][{°^~+*#'<>|-_.:,;`´";
	static const size_t str_len = sizeof(str) - 1;
	PangoLayout *layout;
	PangoRectangle rec;

	layout = pango_layout_new(face->ctx);

	pango_layout_set_height(layout, 0);
	pango_layout_set_spacing(layout, 0);
	pango_layout_set_text(layout, str, str_len);
	pango_layout_get_pixel_extents(layout, NULL, &rec);

	/* We use an example layout to render a bunch of ASCII characters in a
	 * single line. The height and baseline of the resulting extents can be
	 * copied unchanged into the face. For the width we calculate the
	 * average (rounding up). */
	face->width = (rec.width + (str_len - 1)) / str_len;
	face->height = rec.height;
	face->baseline = PANGO_PIXELS_CEIL(pango_layout_get_baseline(layout));

	g_object_unref(layout);
}

static int init_pango(struct wlt_face *face, const char *desc_str,
		      int desc_size, int desc_bold, int desc_italic)
{
	PangoFontDescription *desc;

	/* set context options */
	pango_context_set_base_dir(face->ctx, PANGO_DIRECTION_LTR);
	pango_context_set_language(face->ctx, pango_language_get_default());

	/* set font description */
	desc = pango_font_description_from_string(desc_str);
	init_pango_desc(desc, desc_size, desc_bold, desc_italic);
	pango_context_set_font_description(face->ctx, desc);
	pango_font_description_free(desc);

	/* measure font */
	measure_pango(face);

	if (!face->width || !face->height)
		return -EINVAL;

	return 0;
}

int wlt_face_new(struct wlt_face **out, struct wlt_font *font,
		 const char *desc_str, int desc_size, int desc_bold,
		 int desc_italic)
{
	struct wlt_face *face;
	int r;

	face = calloc(1, sizeof(*face));
	if (!face)
		return -ENOMEM;
	face->ref = 1;
	face->font = font;

	shl_htable_init_ulong(&face->glyphs);
	face->ctx = pango_font_map_create_context(font->map);

	r = init_pango(face, desc_str, desc_size, desc_bold, desc_italic);
	if (r < 0)
		goto err_ctx;

	wlt_font_ref(face->font);
	*out = face;
	return 0;

err_ctx:
	g_object_unref(face->ctx);
	free(face);
	return r;
}

void wlt_face_ref(struct wlt_face *face)
{
	if (!face || !face->ref)
		return;

	++face->ref;
}

static void free_glyph(unsigned long *elem, void *ctx)
{
	wlt_glyph_free(wlt_to_glyph(elem));
}

void wlt_face_unref(struct wlt_face *face)
{
	if (!face || !face->ref || --face->ref)
		return;

	g_object_unref(face->ctx);
	shl_htable_clear_ulong(&face->glyphs, free_glyph, NULL);
	wlt_font_unref(face->font);
	free(face);
}

unsigned int wlt_face_get_width(struct wlt_face *face)
{
	return face->width;
}

unsigned int wlt_face_get_height(struct wlt_face *face)
{
	return face->height;
}

static unsigned int c2f(cairo_format_t format)
{
	switch (format) {
	case CAIRO_FORMAT_A1:
		return WLT_GLYPH_A1;
	case CAIRO_FORMAT_A8:
		return WLT_GLYPH_A8;
	case CAIRO_FORMAT_RGB24:
		return WLT_GLYPH_RGB24;
	default:
		return WLT_GLYPH_INVALID;
	}
}

static int create_glyph(struct wlt_face *face, struct wlt_glyph *glyph,
			const uint32_t *ch, size_t len)
{
	PangoLayoutLine *line;
	cairo_surface_t *surface;
	PangoRectangle rec;
	cairo_format_t format;
	PangoLayout *layout;
	cairo_t *cr;
	size_t cnt;
	glong ulen;
	char *val;
	int r;

	format = CAIRO_FORMAT_A8;
	glyph->format = c2f(format);
	glyph->width = face->width * glyph->cwidth;
	glyph->stride = cairo_format_stride_for_width(format, glyph->width);
	glyph->height = face->height;

	glyph->buffer = calloc(1, glyph->stride * glyph->height);
	if (!glyph->buffer)
		return -ENOMEM;

	surface = cairo_image_surface_create_for_data(glyph->buffer,
						      format,
						      glyph->width,
						      glyph->height,
						      glyph->stride);
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		r = -ENOMEM;
		goto err_buffer;
	}

	cr = cairo_create(surface);
	if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
		r = -ENOMEM;
		goto err_surface;
	}

	pango_cairo_update_context(cr, face->ctx);
	layout = pango_layout_new(face->ctx);

	val = g_ucs4_to_utf8(ch, len, NULL, &ulen, NULL);
	if (!val) {
		r = -ERANGE;
		goto err_layout;
	}

	/* render one line only */
	pango_layout_set_height(layout, 0);
	/* no line spacing */
	pango_layout_set_spacing(layout, 0);
	/* set text to char [+combining-chars] */
	pango_layout_set_text(layout, val, ulen);

	g_free(val);

	cnt = pango_layout_get_line_count(layout);
	if (cnt == 0) {
		r = -ERANGE;
		goto err_layout;
	}

	line = pango_layout_get_line_readonly(layout, 0);
	pango_layout_line_get_pixel_extents(line, NULL, &rec);

	cairo_move_to(cr, -rec.x, face->baseline),
	cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
	pango_cairo_show_layout_line(cr, line);

	g_object_unref(layout);
	cairo_destroy(cr);
	glyph->cr_surface = surface;
	return 0;

err_layout:
	g_object_unref(layout);
	cairo_destroy(cr);
err_surface:
	cairo_surface_destroy(surface);
err_buffer:
	free(glyph->buffer);
	return r;
}

int wlt_face_render(struct wlt_face *face, struct wlt_glyph **out,
		    unsigned long id, const uint32_t *ch, size_t len,
		    size_t cwidth)
{
	struct wlt_glyph *glyph;
	unsigned long *gid;
	bool b;
	int r;

	b = shl_htable_lookup_ulong(&face->glyphs, id, &gid);
	if (b) {
		*out = wlt_to_glyph(gid);
		return 0;
	}

	if (!len || !cwidth)
		return -EINVAL;

	glyph = calloc(1, sizeof(*glyph));
	if (!glyph)
		return -ENOMEM;
	glyph->id = id;
	glyph->cwidth = cwidth;

	r = create_glyph(face, glyph, ch, len);
	if (r < 0)
		goto err_free;

	r = shl_htable_insert_ulong(&face->glyphs, &glyph->id);
	if (r < 0)
		goto err_glyph;

	*out = glyph;
	return 0;

err_glyph:
	free(glyph->buffer);
err_free:
	free(glyph);
	return r;
}

static void wlt_glyph_free(struct wlt_glyph *glyph)
{
	if (glyph->cr_surface)
		cairo_surface_destroy(glyph->cr_surface);
	free(glyph->buffer);
	free(glyph);
}
