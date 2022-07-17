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
 * wlterm - Wayland Terminal
 * This is a rewrite of wlterm using GTK. It's meant to be simple and small and
 * serve as example how to use GTK+tsm+pango to write terminal emulators.
 */

#include <cairo.h>
#include <errno.h>
#include <gtk/gtk.h>
#include <libtsm.h>
#include <math.h>
#include <paths.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xkbcommon/xkbcommon.h>
#include "shl_pty.h"
#include "wlterm.h"

struct term {
	GtkWidget *window;
	GdkKeymap *keymap;
	GtkWidget *tarea;
	struct wlt_font *font;
	struct tsm_screen *screen;
	struct tsm_vte *vte;

	struct shl_pty *pty;
	int pty_bridge;
	GIOChannel *bridge_chan;
	guint bridge_src;
	GSource *pty_idle;
	guint pty_idle_src;
	guint child_src;

	struct wlt_renderer *rend;
	struct wlt_face *face;
	unsigned int cell_width;
	unsigned int cell_height;
	unsigned int width;
	unsigned int height;
	unsigned int columns;
	unsigned int rows;

	unsigned int sel;
	guint32 sel_start;
	gdouble sel_x;
	gdouble sel_y;

	unsigned int adjust_size : 1;
	unsigned int initialized : 1;
	unsigned int exited : 1;
};

static gboolean show_dirty;
static gboolean snap_size;
static gint sb_size = 2000;

static void err(const char *format, ...)
{
	va_list list;

	fprintf(stderr, "ERROR: ");

	va_start(list, format);
	vfprintf(stderr, format, list);
	va_end(list);

	fprintf(stderr, "\n");
}

static void info(const char *format, ...)
{
	va_list list;

	fprintf(stderr, "INFO: ");

	va_start(list, format);
	vfprintf(stderr, format, list);
	va_end(list);

	fprintf(stderr, "\n");
}

static const char *sev2str_table[] = {
	"FATAL",
	"ALERT",
	"CRITICAL",
	"ERROR",
	"WARNING",
	"NOTICE",
	"INFO",
	"DEBUG"
};

static const char *sev2str(unsigned int sev)
{
	if (sev > 7)
		return "DEBUG";

	return sev2str_table[sev];
}

static void log_tsm(void *data, const char *file, int line, const char *fn,
		    const char *subs, unsigned int sev, const char *format,
		    va_list args)
{
	fprintf(stderr, "%s: %s: ", sev2str(sev), subs);
	vfprintf(stderr, format, args);
	fprintf(stderr, "\n");
}

static void __attribute__((noreturn)) term_run_child(struct term *term)
{
	char **argv = (char*[]){
		getenv("SHELL") ? : _PATH_BSHELL,
		"-il",
		NULL
	};

	setenv("TERM", "xterm-256color", 1);
	execve(argv[0], argv, environ);
	exit(1);
}

static void term_set_geometry(struct term *term)
{
	GdkWindowHints hints;
	GdkGeometry geometry;

	geometry.width_inc = term->cell_width;
	geometry.height_inc = term->cell_height;
	geometry.base_width = geometry.width_inc;
	geometry.base_height = geometry.height_inc;
	geometry.min_width = geometry.base_width;
	geometry.min_height = geometry.base_height;

	hints = GDK_HINT_MIN_SIZE | GDK_HINT_BASE_SIZE;
	if (snap_size && term->adjust_size)
		hints |= GDK_HINT_RESIZE_INC;

	gtk_window_set_geometry_hints(GTK_WINDOW(term->window), term->tarea,
				      &geometry, hints);
	gtk_widget_queue_resize(term->tarea);
}

static void term_recalc_cells(struct term *term)
{
	term->columns = term->width / term->cell_width;
	term->rows = term->height / term->cell_height;

	if (!term->columns)
		term->columns = 1;
	if (!term->rows)
		term->rows = 1;
}

static int term_change_font(struct term *term)
{
	struct wlt_face *old;
	int r;

	old = term->face;
	r = wlt_face_new(&term->face, term->font, "monospace",
			 WLT_FACE_DONT_CARE, 0, 0);
	if (r < 0)
		return r;

	wlt_face_unref(old);

	term->cell_width = wlt_face_get_width(term->face);
	term->cell_height = wlt_face_get_height(term->face);

	return 0;
}

static void term_notify_resize(struct term *term)
{
	int r;

	r = tsm_screen_resize(term->screen, term->columns, term->rows);
	if (r < 0)
		err("cannot resize TSM screen (%d)", r);

	r = shl_pty_resize(term->pty, term->columns, term->rows);
	if (r < 0)
		err("cannot resize pty (%d)", r);
}

static void term_read_cb(struct shl_pty *pty, char *u8, size_t len, void *data)
{
	struct term *term = data;

	tsm_vte_input(term->vte, u8, len);
	gtk_widget_queue_draw(term->tarea);
}

static void term_child_cb(GPid pid, gint status, gpointer data)
{
	struct term *term = data;

	g_spawn_close_pid(pid);
	term->child_src = 0;
	gtk_main_quit();
}

static gboolean term_configure_cb(GtkWidget *widget, GdkEvent *ev,
				  gpointer data)
{
	struct term *term = data;
	GdkEventConfigure *cev = (void*)ev;
	GdkWindow *wnd;
	GdkWindowState st;
	GdkEventMask mask;
	bool new_adjust_size = term->adjust_size;
	int r, pid;

	term->width = cev->width;
	term->height = cev->height;

	/* Initial configure-event, setup fonts, pty, etc. */
	if (!term->initialized) {
		r = wlt_renderer_new(&term->rend, term->width, term->height);
		if (r < 0) {
			err("cannot initialize renderer (%d)", r);
			gtk_main_quit();
			return TRUE;
		}

		r = term_change_font(term);
		if (r < 0) {
			err("cannot load font (%d)", r);
			gtk_main_quit();
			return TRUE;
		}

		/* compute cell size */
		term_recalc_cells(term);

		term_set_geometry(term);
		gtk_widget_queue_draw(term->tarea);

		r = shl_pty_open(&term->pty, term_read_cb, term,
				 term->columns, term->rows);
		if (r < 0) {
			err("cannot spawn pty (%d)", r);
			gtk_main_quit();
			return TRUE;
		} else if (!r) {
			/* child */
			term_run_child(term);
			exit(1);
		}

		r = shl_pty_bridge_add(term->pty_bridge, term->pty);
		if (r < 0) {
			err("cannot add pty to bridge (%d)", r);
			shl_pty_close(term->pty);
			gtk_main_quit();
			return TRUE;
		}

		pid = shl_pty_get_child(term->pty);
		term->child_src = g_child_watch_add(pid, term_child_cb, term);

		wnd = gtk_widget_get_window(term->window);
		mask = gdk_window_get_events(wnd);
		mask |= GDK_KEY_PRESS_MASK;
		mask |= GDK_BUTTON_MOTION_MASK;
		mask |= GDK_BUTTON_PRESS_MASK;
		mask |= GDK_BUTTON_RELEASE_MASK;
		gdk_window_set_events(wnd, mask);

		term->initialized = 1;
		term_notify_resize(term);
	} else {
		/* compute cell size */
		term_recalc_cells(term);
		term_notify_resize(term);

		r = wlt_renderer_resize(term->rend, term->width, term->height);
		if (r < 0)
			err("cannot resize renderer (%d)", r);
	}

	/* adjust geometry */
	wnd = gtk_widget_get_window(term->window);
	st = gdk_window_get_state(wnd);

	if (st & (GDK_WINDOW_STATE_MAXIMIZED | GDK_WINDOW_STATE_FULLSCREEN)) {
		if (term->adjust_size)
			new_adjust_size = false;
	} else if (!term->adjust_size) {
		new_adjust_size = true;
	}

	if (new_adjust_size != term->adjust_size) {
		term->adjust_size = new_adjust_size;
		term_set_geometry(term);
	}

	gtk_widget_queue_draw(term->tarea);

	return TRUE;
}

static gboolean term_redraw_cb(GtkWidget *widget, cairo_t *cr, gpointer data)
{
	struct term *term = data;
	struct wlt_draw_ctx ctx;
	int64_t start, end;

	if (!term->initialized)
		return FALSE;

	start = g_get_monotonic_time();

	memset(&ctx, 0, sizeof(ctx));
	ctx.debug = show_dirty;
	ctx.rend = term->rend;
	ctx.cr = cr;
	ctx.face = term->face;
	ctx.cell_width = term->cell_width;
	ctx.cell_height = term->cell_height;
	ctx.screen = term->screen;
	ctx.vte = term->vte;
	cairo_clip_extents(cr, &ctx.x1, &ctx.y1, &ctx.x2, &ctx.y2);

	wlt_renderer_draw(&ctx);

	end = g_get_monotonic_time();
	if (0)
		info("draw: %lldms", (end - start) / 1000);

	return FALSE;
}

static void term_destroy_cb(GtkWidget *widget, gpointer data)
{
	struct term *term = data;

	term->window = NULL;
	term->tarea = NULL;

	if (!term->exited)
		gtk_main_quit();
}

#define ALL_MODS (GDK_SHIFT_MASK | GDK_LOCK_MASK | GDK_CONTROL_MASK | \
		  GDK_MOD1_MASK | GDK_MOD4_MASK)

static gboolean term_key_cb(GtkWidget *widget, GdkEvent *ev, gpointer data)
{
	struct term *term = data;
	GdkEventKey *e = (void*)ev;
	unsigned int mods = 0;
	gboolean b;
	GdkModifierType cmod;
	guint key;
	uint32_t ucs4;

	if (e->type != GDK_KEY_PRESS)
		return FALSE;

	if (e->state & GDK_SHIFT_MASK)
		mods |= TSM_SHIFT_MASK;
	if (e->state & GDK_LOCK_MASK)
		mods |= TSM_LOCK_MASK;
	if (e->state & GDK_CONTROL_MASK)
		mods |= TSM_CONTROL_MASK;
	if (e->state & GDK_MOD1_MASK)
		mods |= TSM_ALT_MASK;
	if (e->state & GDK_MOD4_MASK)
		mods |= TSM_LOGO_MASK;

	if (!term->keymap)
		term->keymap = gdk_keymap_get_default();

	b = gdk_keymap_translate_keyboard_state(term->keymap,
				e->hardware_keycode,
				e->state,
				e->group,
				&key,
				NULL,
				NULL,
				&cmod);

	if (b) {
		if (key == GDK_KEY_Up &&
		    ((e->state & ~cmod & ALL_MODS) == GDK_SHIFT_MASK)) {
			tsm_screen_sb_up(term->screen, 1);
			gtk_widget_queue_draw(term->tarea);
			return TRUE;
		} else if (key == GDK_KEY_Down &&
		    ((e->state & ~cmod & ALL_MODS) == GDK_SHIFT_MASK)) {
			tsm_screen_sb_down(term->screen, 1);
			gtk_widget_queue_draw(term->tarea);
			return TRUE;
		} else if (key == GDK_KEY_Page_Up &&
		    ((e->state & ~cmod & ALL_MODS) == GDK_SHIFT_MASK)) {
			tsm_screen_sb_page_up(term->screen, 1);
			gtk_widget_queue_draw(term->tarea);
			return TRUE;
		} else if (key == GDK_KEY_Page_Down &&
		    ((e->state & ~cmod & ALL_MODS) == GDK_SHIFT_MASK)) {
			tsm_screen_sb_page_down(term->screen, 1);
			gtk_widget_queue_draw(term->tarea);
			return TRUE;
		}
	}

	ucs4 = xkb_keysym_to_utf32(e->keyval);
	if (!ucs4)
		ucs4 = TSM_VTE_INVALID;

	if (tsm_vte_handle_keyboard(term->vte, e->keyval, 0, mods, ucs4)) {
		tsm_screen_sb_reset(term->screen);
		return TRUE;
	}

	return FALSE;
}

static gboolean term_button_cb(GtkWidget *widget, GdkEvent *ev,
				     gpointer data)
{
	GdkEventButton *e = (void*)ev;
	struct term *term = data;

	if (e->button != 1)
		return FALSE;

	if (e->type == GDK_BUTTON_PRESS) {
		term->sel = 1;
		term->sel_start = e->time;
		term->sel_x = e->x;
		term->sel_y = e->y;
	} else if (e->type == GDK_2BUTTON_PRESS) {
		term->sel = 2;
		/* TODO: select word */
		tsm_screen_selection_start(term->screen,
					   e->x / term->cell_width,
					   e->y / term->cell_height);
		gtk_widget_queue_draw(term->tarea);
	} else if (e->type == GDK_3BUTTON_PRESS) {
		term->sel = 2;
		/* TODO: select line */
		tsm_screen_selection_start(term->screen,
					   e->x / term->cell_width,
					   e->y / term->cell_height);
		gtk_widget_queue_draw(term->tarea);
	} else if (e->type == GDK_BUTTON_RELEASE) {
		if (term->sel == 1 && term->sel_start + 100 > e->time) {
			tsm_screen_selection_reset(term->screen);
			gtk_widget_queue_draw(term->tarea);
		} else if (term->sel > 1) {
			/* TODO: copy */
		}

		term->sel = 0;
	}

	return TRUE;
}

static gboolean term_motion_cb(GtkWidget *widget, GdkEvent *ev,
			       gpointer data)
{
	GdkEventMotion *e = (void*)ev;
	struct term *term = data;

	if (!term->sel)
		return TRUE;

	if (term->sel == 1) {
		if (fabs(term->sel_x - e->x) > 3 ||
		    fabs(term->sel_y - e->y) > 3) {
			term->sel = 2;
			tsm_screen_selection_start(term->screen,
						   term->sel_x / term->cell_width,
						   term->sel_y / term->cell_height);
			gtk_widget_queue_draw(term->tarea);
		}
	} else {
		tsm_screen_selection_target(term->screen,
					    e->x / term->cell_width,
					    e->y / term->cell_height);
		gtk_widget_queue_draw(term->tarea);
	}

	return FALSE;
}

static gboolean term_pty_idle_cb(gpointer data)
{
	struct term *term = data;

	shl_pty_dispatch(term->pty);
	term->pty_idle_src = 0;

	return FALSE;
}

static void term_write_cb(struct tsm_vte *vte, const char *u8, size_t len,
			  void *data)
{
	struct term *term = data;
	int r;

	if (!term->initialized)
		return;

	r = shl_pty_write(term->pty, u8, len);
	if (r < 0)
		err("OOM in pty-write (%d)", r);

	if (!term->pty_idle_src)
		term->pty_idle_src = g_idle_add(term_pty_idle_cb, term);
}

static gboolean term_bridge_cb(GIOChannel *chan, GIOCondition cond,
			       gpointer data)
{
	struct term *term = data;
	int r;

	r = shl_pty_bridge_dispatch(term->pty_bridge, 0);
	if (r < 0)
		err("bridge dispatch failed (%d)", r);

	return TRUE;
}

static void term_free(struct term *term)
{
	if (term->pty) {
		shl_pty_bridge_remove(term->pty_bridge, term->pty);
		shl_pty_close(term->pty);
		shl_pty_unref(term->pty);
	}
	if (term->child_src)
		g_source_remove(term->child_src);
	if (term->pty_idle_src)
		g_source_remove(term->pty_idle_src);
	g_source_unref(term->pty_idle);
	g_source_remove(term->bridge_src);
	g_io_channel_unref(term->bridge_chan);
	shl_pty_bridge_free(term->pty_bridge);
	tsm_vte_unref(term->vte);
	tsm_screen_unref(term->screen);
	wlt_renderer_free(term->rend);
	wlt_face_unref(term->face);
	wlt_font_unref(term->font);
	if (term->window)
		gtk_widget_destroy(term->window);
	free(term);
}

static int term_new(struct term **out)
{
	struct term *term;
	int r;

	term = calloc(1, sizeof(*term));
	if (!term)
		return -ENOMEM;
	term->adjust_size = 1;

	r = wlt_font_new(&term->font);
	if (r < 0)
		goto err_free;

	r = tsm_screen_new(&term->screen, log_tsm, term);
	if (r < 0)
		goto err_font;

	tsm_screen_set_max_sb(term->screen, sb_size > 0 ? sb_size : 0);

	r = tsm_vte_new(&term->vte, term->screen, term_write_cb, term,
			log_tsm, term);
	if (r < 0)
		goto err_screen;

	term->pty_bridge = shl_pty_bridge_new();
	if (term->pty_bridge < 0) {
		r = term->pty_bridge;
		goto err_vte;
	}

	term->bridge_chan = g_io_channel_unix_new(term->pty_bridge);
	term->bridge_src = g_io_add_watch(term->bridge_chan,
					  G_IO_IN,
					  term_bridge_cb, term);

	term->pty_idle = g_idle_source_new();

	term->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(term->window), "Terminal");
	gtk_window_set_has_resize_grip(GTK_WINDOW(term->window), FALSE);
	g_signal_connect(term->window, "destroy", G_CALLBACK(term_destroy_cb),
			 term);
	g_signal_connect(term->window, "key-press-event",
			 G_CALLBACK(term_key_cb), term);
	g_signal_connect(term->window, "button-press-event",
			 G_CALLBACK(term_button_cb), term);
	g_signal_connect(term->window, "button-release-event",
			 G_CALLBACK(term_button_cb), term);
	g_signal_connect(term->window, "motion-notify-event",
			 G_CALLBACK(term_motion_cb), term);

	term->tarea = gtk_drawing_area_new();
	g_signal_connect(term->tarea, "configure-event",
			 G_CALLBACK(term_configure_cb), term);
	g_signal_connect(term->tarea, "draw",
			 G_CALLBACK(term_redraw_cb), term);
	gtk_container_add(GTK_CONTAINER(term->window), term->tarea);

	*out = term;
	return 0;

err_vte:
	tsm_vte_unref(term->vte);
err_screen:
	tsm_screen_unref(term->screen);
err_font:
	wlt_font_unref(term->font);
err_free:
	free(term);
	return r;
}

static void term_show(struct term *term)
{
	if (term->window)
		gtk_widget_show_all(term->window);
}

static void term_hide(struct term *term)
{
	if (term->window)
		gtk_widget_hide(term->window);
}

static GOptionEntry opts[] = {
	{ "show-dirty", 0, 0, G_OPTION_ARG_NONE, &show_dirty, "Mark dirty cells during redraw", NULL },
	{ "snap-size", 0, 0, G_OPTION_ARG_NONE, &snap_size, "Snap to next cell-size when resizing", NULL },
	{ "sb-size", 0, 0, G_OPTION_ARG_INT, &sb_size, "Scroll-back buffer size in lines", NULL },
	{ NULL }
};

int main(int argc, char **argv)
{
	struct term *term;
	int r;
	GOptionContext *opt;
	GError *e = NULL;

	opt = g_option_context_new("- Wayland Terminal Emulator");
	g_option_context_add_main_entries(opt, opts, NULL);
	g_option_context_add_group(opt, gtk_get_option_group(TRUE));
	if (!g_option_context_parse(opt, &argc, &argv, &e)) {
		g_print("cannot parse arguments: %s\n", e->message);
		g_error_free(e);
		r = -EINVAL;
		goto error;
	}

	r = term_new(&term);
	if (r < 0)
		goto error;

	term_show(term);
	gtk_main();
	term->exited = 1;
	term_hide(term);
	term_free(term);

	return 0;

error:
	errno = -r;
	err("cannot initialize terminal: %m");
	return -r;
}
