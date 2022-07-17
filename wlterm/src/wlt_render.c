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
 * Terminal Rendering
 */

#include <cairo.h>
#include <errno.h>
#include <libtsm.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wlterm.h"

struct wlt_renderer {
	unsigned int width;
	unsigned int height;
	int stride;
	uint8_t *data;
	cairo_surface_t *surface;
	tsm_age_t age;
};

static int wlt_renderer_realloc(struct wlt_renderer *rend, unsigned int width,
				unsigned int height)
{
	int stride;
	uint8_t *data;
	cairo_surface_t *surface;

	stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
	data = malloc(abs(stride * height));
	if (!data)
		return -ENOMEM;

	surface = cairo_image_surface_create_for_data(data,
						      CAIRO_FORMAT_ARGB32,
						      width,
						      height,
						      stride);
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(surface);
		free(data);
		return -ENOMEM;
	}

	if (rend->data) {
		cairo_surface_destroy(rend->surface);
		free(rend->data);
	}

	rend->width = width;
	rend->height = height;
	rend->stride = stride;
	rend->data = data;
	rend->surface = surface;
	rend->age = 0;
	return 0;
}

int wlt_renderer_new(struct wlt_renderer **out, unsigned int width,
		     unsigned int height)
{
	struct wlt_renderer *rend;
	int r;

	rend = calloc(1, sizeof(*rend));
	if (!rend)
		return -ENOMEM;

	r = wlt_renderer_realloc(rend, width, height);
	if (r < 0)
		goto err_rend;

	*out = rend;
	return 0;

err_rend:
	free(rend);
	return r;
}

void wlt_renderer_free(struct wlt_renderer *rend)
{
	if (!rend)
		return;

	cairo_surface_destroy(rend->surface);
	free(rend->data);
	free(rend);
}

int wlt_renderer_resize(struct wlt_renderer *rend, unsigned int width,
			unsigned int height)
{
	return wlt_renderer_realloc(rend, width, height);
}

void wlt_renderer_dirty(struct wlt_renderer *rend)
{
	rend->age = 0;
}

static void wlt_renderer_fill(struct wlt_renderer *rend,
			      unsigned int x, unsigned int y,
			      unsigned int width, unsigned int height,
			      uint8_t br, uint8_t bg, uint8_t bb)
{
	unsigned int i, tmp;
	uint8_t *dst;
	uint32_t out;

	/* clip width */
	tmp = x + width;
	if (tmp <= x || x >= rend->width)
		return;
	if (tmp > rend->width)
		width = rend->width - x;

	/* clip height */
	tmp = y + height;
	if (tmp <= y || y >= rend->height)
		return;
	if (tmp > rend->height)
		height = rend->height - y;

	/* prepare */
	dst = rend->data;
	dst = &dst[y * rend->stride + x * 4];
	out = (0xff << 24) | (br << 16) | (bg << 8) | bb;

	/* fill buffer */
	while (height--) {
		for (i = 0; i < width; ++i)
			((uint32_t*)dst)[i] = out;

		dst += rend->stride;
	}
}

/* used for debugging; draws a border on the given rectangle */
static void wlt_renderer_highlight(struct wlt_renderer *rend,
				   unsigned int x, unsigned int y,
				   unsigned int width, unsigned int height)
{
	unsigned int i, j, tmp;
	uint8_t *dst;
	uint32_t out;

	/* clip width */
	tmp = x + width;
	if (tmp <= x || x >= rend->width)
		return;
	if (tmp > rend->width)
		width = rend->width - x;

	/* clip height */
	tmp = y + height;
	if (tmp <= y || y >= rend->height)
		return;
	if (tmp > rend->height)
		height = rend->height - y;

	/* prepare */
	dst = rend->data;
	dst = &dst[y * rend->stride + x * 4];
	out = (0xff << 24) | (0xd0 << 16) | (0x10 << 8) | 0x10;

	/* draw outline into buffer */
	for (i = 0; i < height; ++i) {
		((uint32_t*)dst)[0] = out;
		((uint32_t*)dst)[width - 1] = out;

		if (!i || i + 1 == height) {
			for (j = 0; j < width; ++j)
				((uint32_t*)dst)[j] = out;
		}

		dst += rend->stride;
	}
}

static void wlt_renderer_blend(struct wlt_renderer *rend,
			       const struct wlt_glyph *glyph,
			       unsigned int x, unsigned int y,
			       uint8_t fr, uint8_t fg, uint8_t fb,
			       uint8_t br, uint8_t bg, uint8_t bb)
{
	unsigned int i, tmp, width, height;
	const uint8_t *src;
	uint8_t *dst;
	uint32_t out;
	uint_fast32_t r, g, b;

	/* clip width */
	tmp = x + glyph->width;
	if (tmp <= x || x >= rend->width)
		return;
	if (tmp > rend->width)
		width = rend->width - x;
	else
		width = glyph->width;

	/* clip height */
	tmp = y + glyph->height;
	if (tmp <= y || y >= rend->height)
		return;
	if (tmp > rend->height)
		height = rend->height - y;
	else
		height = glyph->height;

	/* prepare */
	dst = rend->data;
	dst = &dst[y * rend->stride + x * 4];
	src = glyph->buffer;

	/* blend buffer */
	while (height--) {
		for (i = 0; i < width; ++i) {
			if (src[i] == 0) {
				r = br;
				g = bg;
				b = bb;
			} else if (src[i] == 255) {
				r = fr;
				g = fg;
				b = fb;
			} else {
				/* Division by 255 (t /= 255) is done with:
				 *   t += 0x80
				 *   t = (t + (t >> 8)) >> 8
				 * This speeds up the computation by ~20% as
				 * the division is skipped. */
				r = fr * src[i] + br * (255 - src[i]);
				r += 0x80;
				r = (r + (r >> 8)) >> 8;

				g = fg * src[i] + bg * (255 - src[i]);
				g += 0x80;
				g = (g + (g >> 8)) >> 8;

				b = fb * src[i] + bb * (255 - src[i]);
				b += 0x80;
				b = (b + (b >> 8)) >> 8;
			}

			out = (0xff << 24) | (r << 16) | (g << 8) | b;
			((uint32_t*)dst)[i] = out;
		}

		dst += rend->stride;
		src += glyph->stride;
	}
}

static bool overlap(const struct wlt_draw_ctx *ctx, double x1, double y1,
		    double x2, double y2)
{
	return (ctx->x1 < x2 && ctx->x2 > x1 &&
		ctx->y1 < y2 && ctx->y2 > y1);
}

static int wlt_renderer_draw_cell(struct tsm_screen *screen, uint32_t id,
				  const uint32_t *ch, size_t len,
				  unsigned int cwidth, unsigned int posx,
				  unsigned int posy,
				  const struct tsm_screen_attr *attr,
				  tsm_age_t age, void *data)
{
	const struct wlt_draw_ctx *ctx = data;
	struct wlt_renderer *rend = ctx->rend;
	uint8_t fr, fg, fb, br, bg, bb;
	unsigned int x, y;
	struct wlt_glyph *glyph;
	bool skip;
	int r;

	x = posx * ctx->cell_width;
	y = posy * ctx->cell_height;

	/* If the cell is inside of the dirty-region *and* our age and the
	 * cell age is non-zero *and* the cell-age is smaller than our age,
	 * then skip drawing as it's already on-screen. */
	skip = overlap(ctx, x, y, x + ctx->cell_width, y + ctx->cell_height);
	skip = skip && age && rend->age && age <= rend->age;

	if (skip && !ctx->debug)
		return 0;

	/* invert colors if requested */
	if (attr->inverse) {
		fr = attr->br;
		fg = attr->bg;
		fb = attr->bb;
		br = attr->fr;
		bg = attr->fg;
		bb = attr->fb;
	} else {
		fr = attr->fr;
		fg = attr->fg;
		fb = attr->fb;
		br = attr->br;
		bg = attr->bg;
		bb = attr->bb;
	}

	/* !len means background-only */
	if (!len) {
		wlt_renderer_fill(rend, x, y, ctx->cell_width * cwidth,
				  ctx->cell_height, br, bg, bb);
	} else {
		r = wlt_face_render(ctx->face, &glyph, id, ch, len, cwidth);
		if (r < 0)
			wlt_renderer_fill(rend, x, y, ctx->cell_width * cwidth,
					  ctx->cell_height, br, bg, bb);
		else
			wlt_renderer_blend(rend, glyph, x, y,
					   fr, fg, fb, br, bg, bb);
	}

	if (!skip && ctx->debug)
		wlt_renderer_highlight(rend, x, y, ctx->cell_width * cwidth,
				       ctx->cell_height);

	return 0;
}

void wlt_renderer_draw(const struct wlt_draw_ctx *ctx)
{
	struct wlt_renderer *rend = ctx->rend;
	struct tsm_screen_attr attr;
	unsigned int w, h;

	/* cairo is *way* too slow to render all masks efficiently. Therefore,
	 * we render all glyphs into a shadow buffer on the CPU and then tell
	 * cairo to blit it into the gtk buffer. This way we get two mem-writes
	 * but at least it's fast enough to render a whole screen. */

	cairo_surface_flush(rend->surface);
	rend->age = tsm_screen_draw(ctx->screen, wlt_renderer_draw_cell,
				    (void*)ctx);
	cairo_surface_mark_dirty(rend->surface);

	cairo_set_source_surface(ctx->cr, rend->surface, 0, 0);
	cairo_paint(ctx->cr);

	/* draw padding */
	w = tsm_screen_get_width(ctx->screen);
	h = tsm_screen_get_height(ctx->screen);
	tsm_vte_get_def_attr(ctx->vte, &attr);
	cairo_set_source_rgb(ctx->cr,
			     attr.br / 255.0,
			     attr.bg / 255.0,
			     attr.bb / 255.0);
	cairo_move_to(ctx->cr, w * ctx->cell_width, 0);
	cairo_line_to(ctx->cr, w * ctx->cell_width, h * ctx->cell_height);
	cairo_line_to(ctx->cr, 0, h * ctx->cell_height);
	cairo_line_to(ctx->cr, 0, rend->height);
	cairo_line_to(ctx->cr, rend->width, rend->height);
	cairo_line_to(ctx->cr, rend->width, 0);
	cairo_close_path(ctx->cr);
	cairo_fill(ctx->cr);
}
