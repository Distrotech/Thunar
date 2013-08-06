/*-
 * Copyright (c) 2012 Nick Schermer <nick@xfce.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_MATH_H
#include <math.h>
#endif

#include <exo/exo.h>
#include <xfconf/xfconf.h>

#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#endif

#include <thunar/thunar-private.h>
#include <thunar/thunar-desktop-background.h>
#include <thunar/thunar-enum-types.h>
#include <thunar/thunar-tasks.h>
#include <thunar/thunar-gdk-extensions.h>

#define FADE_ANIMATION_USEC ((gdouble) G_USEC_PER_SEC / 2.0) /* 0.5 second */
#define FADE_ANIMATION_FPS  (1000 / 60)



enum
{
  PROP_0,
  PROP_SOURCE_WINDOW,
  N_PROPERTIES
};



static void     thunar_desktop_background_constructed           (GObject                  *object);
static void     thunar_desktop_background_finalize              (GObject                  *object);
static void     thunar_desktop_background_get_property          (GObject                  *object,
                                                                 guint                     prop_id,
                                                                 GValue                   *value,
                                                                 GParamSpec               *pspec);
static void     thunar_desktop_background_set_property          (GObject                  *object,
                                                                 guint                     prop_id,
                                                                 const GValue             *value,
                                                                 GParamSpec               *pspec);
static void     thunar_desktop_background_expose                (ThunarDesktopBackground  *background,
                                                                 gboolean                  pseudo_transparency);
static void     thunar_desktop_background_paint                 (ThunarDesktopBackground  *background,
                                                                 gboolean                  fade_animation);
static void     thunar_desktop_background_settings_changed      (XfconfChannel            *channel,
                                                                 const gchar              *property,
                                                                 const GValue             *value,
                                                                 ThunarDesktopBackground  *background);
static void     thunar_desktop_background_cycle                 (ThunarDesktopBackground  *background,
                                                                 gboolean                  startup);



struct _ThunarDesktopBackgroundClass
{
  GObjectClass __parent__;
};

struct _ThunarDesktopBackground
{
  GObject __parent__;

  XfconfChannel *settings;

  GdkWindow     *source_window;
  GdkPixmap     *pixmap;

  /* async to prepare images in a thread */
  GTask         *task;

  /* fade timeout for paint() */
  guint          fade_timeout_id;

  /* for grouping xfconf changes */
  guint          paint_idle_id;

  /* background cycle timer */
  guint          cycle_timeout_id;
};

typedef struct
{
  ThunarDesktopBackground *background;
  cairo_surface_t         *start_surface;
  cairo_surface_t         *end_surface;
  gint64                   start_time;
}
BackgroundFade;

typedef struct
{
  /* base property for xfconf */
  gchar                  *base_prop;

  /* size of the monitor */
  GdkRectangle            geometry;

  /* bckground style */
  ThunarBackgroundStyle   bg_style;

  /* not per-monitor, but we need this later */
  gboolean                fade_animation;

  /* location loaded during the async */
  GFile                  *image_file;
  GdkInterpType           image_interp;

  /* this is returned from the thread */
  GdkPixbuf              *pixbuf;
}
BackgroundAsync;



static GParamSpec *property_pspecs[N_PROPERTIES] = { NULL, };



G_DEFINE_TYPE (ThunarDesktopBackground, thunar_desktop_background, G_TYPE_OBJECT)



static void
thunar_desktop_background_class_init (ThunarDesktopBackgroundClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->constructed = thunar_desktop_background_constructed;
  gobject_class->finalize = thunar_desktop_background_finalize;
  gobject_class->get_property = thunar_desktop_background_get_property;
  gobject_class->set_property = thunar_desktop_background_set_property;

  property_pspecs[PROP_SOURCE_WINDOW] =
      g_param_spec_object ("source-window",
                           NULL,
                           NULL,
                           GDK_TYPE_WINDOW,
                           EXO_PARAM_READWRITE
                           | G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (gobject_class, N_PROPERTIES, property_pspecs);
}



static void
thunar_desktop_background_init (ThunarDesktopBackground *background)
{
  background->settings = xfconf_channel_get ("thunar-desktop");
}



static void
thunar_desktop_background_constructed (GObject *object)
{
  ThunarDesktopBackground *background = THUNAR_DESKTOP_BACKGROUND (object);
  GdkScreen               *screen;

  _thunar_return_if_fail (GDK_IS_DRAWABLE (background->source_window));

  /* create pixmap we can draw on */
  screen = gdk_window_get_screen (background->source_window);
  background->pixmap = gdk_pixmap_new (GDK_DRAWABLE (background->source_window),
                                       gdk_screen_get_width (screen),
                                       gdk_screen_get_height (screen),
                                       -1);

  /* fire cycle timer */
  thunar_desktop_background_cycle (background, TRUE);

  /* render images and colors */
  thunar_desktop_background_paint (background, FALSE);

  /* set our background */
  gdk_window_set_back_pixmap (background->source_window,
                              background->pixmap, FALSE);

  /* expose views */
  thunar_desktop_background_expose (background, TRUE);

  /* start watching changes */
  g_signal_connect (G_OBJECT (background->settings), "property-changed",
                    G_CALLBACK (thunar_desktop_background_settings_changed), background);
}



static void
thunar_desktop_background_finalize (GObject *object)
{
  ThunarDesktopBackground *background = THUNAR_DESKTOP_BACKGROUND (object);
  GdkWindow               *root_window;

  /* stop running task */
  if (background->task != NULL)
    thunar_tasks_cancel (background->task);

  /* unset the backgrounds */
  gdk_window_set_back_pixmap (background->source_window, NULL, FALSE);
  root_window = gdk_screen_get_root_window (gdk_window_get_screen (background->source_window));
  gdk_window_set_back_pixmap (root_window, NULL, FALSE);

#ifdef GDK_WINDOWING_X11
  /* unset speudo-transparency */
  gdk_error_trap_push ();
  gdk_property_delete (root_window, gdk_atom_intern_static_string ("_XROOTPMAP_ID"));
  gdk_property_delete (root_window, gdk_atom_intern_static_string ("ESETROOT_PMAP_ID"));
  gdk_error_trap_pop ();
#endif

  if (background->cycle_timeout_id != 0)
    g_source_remove (background->cycle_timeout_id);

  if (background->fade_timeout_id != 0)
    g_source_remove (background->fade_timeout_id);

  if (background->paint_idle_id != 0)
    g_source_remove (background->paint_idle_id);

  if (background->source_window != NULL)
    g_object_unref (G_OBJECT (background->source_window));
  if (background->pixmap != NULL)
    g_object_unref (G_OBJECT (background->pixmap));

  g_signal_handlers_disconnect_by_func (G_OBJECT (background->settings),
      G_CALLBACK (thunar_desktop_background_settings_changed), background);

  (*G_OBJECT_CLASS (thunar_desktop_background_parent_class)->finalize) (object);
}



static void
thunar_desktop_background_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  //ThunarDesktopBackground *background = THUNAR_DESKTOP_BACKGROUND (object);

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static void
thunar_desktop_background_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  ThunarDesktopBackground *background = THUNAR_DESKTOP_BACKGROUND (object);

  switch (prop_id)
    {
    case PROP_SOURCE_WINDOW:
      _thunar_return_if_fail (background->source_window == NULL);
      background->source_window = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static gchar *
thunar_desktop_background_get_monitor_name (GdkScreen *screen,
                                            gint       n)
{
  gchar *name;

  name = gdk_screen_get_monitor_plug_name (screen, n);
  if (G_UNLIKELY (name == NULL))
    name = g_strdup_printf ("monitor-%d", n);

  return name;
}



static void
thunar_desktop_background_paint_color_fill (cairo_t                    *cr,
                                            const GdkRectangle         *area,
                                            ThunarBackgroundColorStyle  style,
                                            GdkColor                   *color_start,
                                            GdkColor                   *color_end)
{
  cairo_pattern_t *pattern;
  gint             x1, y1;
  gint             radius;

  switch (style)
    {
    case THUNAR_BACKGROUND_COLOR_STYLE_SOLID:
      gdk_cairo_set_source_color (cr, color_start);
      cairo_rectangle (cr, area->x, area->y, area->width, area->height);
      cairo_fill (cr);
      break;

    case THUNAR_BACKGROUND_COLOR_STYLE_HORIZONTAL:
      x1 = area->x + area->width;
      y1 = area->y;
      goto paint_linear;

    case THUNAR_BACKGROUND_COLOR_STYLE_VERTICAL:
      x1 = area->x;
      y1 = area->y + area->height;

      paint_linear:

      pattern = cairo_pattern_create_linear (area->x, area->y, x1, y1);

      paint_gradient:

      if (cairo_pattern_status (pattern) == CAIRO_STATUS_SUCCESS)
        {
          /* set start and stop color */
          cairo_pattern_add_color_stop_rgb (pattern, 0,
                                            color_start->red / 65535.0,
                                            color_start->green / 65535.0,
                                            color_start->blue / 65535.0);
          cairo_pattern_add_color_stop_rgb (pattern, 1,
                                            color_end->red / 65535.0,
                                            color_end->green / 65535.0,
                                            color_end->blue / 65535.0);

          /* draw rectangle with gradient fill */
          cairo_save (cr);
          cairo_rectangle (cr, area->x, area->y, area->width, area->height);
          cairo_set_source (cr, pattern);
          cairo_fill (cr);
          cairo_restore (cr);
        }
      cairo_pattern_destroy (pattern);
      break;

    case THUNAR_BACKGROUND_COLOR_STYLE_RADIAL:
      x1 = area->x + area->width / 2;
      y1 = area->y + area->height / 2;
      radius = MAX (area->width, area->height) / 2;

      pattern = cairo_pattern_create_radial (x1, y1, radius, x1, y1, 0);
      goto paint_gradient;
      break;
    }
}



static cairo_surface_t *
thunar_desktop_background_get_surface (ThunarDesktopBackground *background,
                                       cairo_t                 *cr_source)
{
  cairo_surface_t *copy;
  cairo_surface_t *target;
  gint             w, h;
  cairo_t         *cr;

  target = cairo_get_target (cr_source);
  gdk_drawable_get_size (GDK_DRAWABLE (background->pixmap), &w, &h);
  copy = cairo_surface_create_similar (target, cairo_surface_get_content (target), w, h);

  cr = cairo_create (copy);
  cairo_set_source_surface (cr, target, 0.0, 0.0);
  cairo_paint (cr);

  if (cairo_status (cr) != CAIRO_STATUS_SUCCESS)
    {
      cairo_surface_destroy (copy);
      copy = NULL;
    }

  cairo_destroy (cr);

  return copy;
}



static void
thunar_desktop_background_expose (ThunarDesktopBackground *background,
                                  gboolean                 pseudo_transparency)
{
  GdkWindow *root_window;
  GdkScreen *screen;
#ifdef GDK_WINDOWING_X11
  Window     pixmap_xid;
  GdkAtom    atom_pixmap;
#endif

  /* invalidate area of our window */
  gdk_window_clear (background->source_window);

  /* leave if we're not updating the fake transparency */
  if (!pseudo_transparency)
    return;

  gdk_error_trap_push ();

  /* root window background */
  screen = gdk_window_get_screen (background->source_window);
  root_window = gdk_screen_get_root_window (screen);
  gdk_window_set_back_pixmap (root_window, background->pixmap, FALSE);
  gdk_window_clear (root_window);

#ifdef GDK_WINDOWING_X11
  /* pseudo atoms for aterm / xterm etc */
  atom_pixmap = gdk_atom_intern_static_string ("PIXMAP");
  pixmap_xid = gdk_x11_drawable_get_xid (GDK_DRAWABLE (background->pixmap));

  gdk_property_change (root_window,
                       gdk_atom_intern_static_string ("ESETROOT_PMAP_ID"),
                       atom_pixmap, 32,
                       GDK_PROP_MODE_REPLACE, (guchar *) &pixmap_xid, 1);

  gdk_property_change (root_window,
                       gdk_atom_intern_static_string ("_XROOTPMAP_ID"),
                       atom_pixmap, 32,
                       GDK_PROP_MODE_REPLACE, (guchar *) &pixmap_xid, 1);
#endif

  gdk_error_trap_pop ();
}



static gboolean
thunar_desktop_background_fade_running (gpointer data)
{
  BackgroundFade *fade = data;
  gdouble         opacity;
  cairo_t        *cr;

  _thunar_return_val_if_fail (THUNAR_IS_DESKTOP_BACKGROUND (fade->background), FALSE);
  _thunar_return_val_if_fail (GDK_IS_DRAWABLE (fade->background->pixmap), FALSE);
  _thunar_return_val_if_fail (GDK_IS_WINDOW (fade->background->source_window), FALSE);

  /* get fade opacity based on system time */
  if (fade->start_time > 0)
    opacity = (g_get_real_time () - fade->start_time) / FADE_ANIMATION_USEC;
  else
    opacity = 0;

  /* prepare cairo context */
  cr = gdk_cairo_create (GDK_DRAWABLE (fade->background->pixmap));
  g_assert (cairo_status (cr) == CAIRO_STATUS_SUCCESS);

  cairo_set_source_surface (cr, fade->start_surface, 0.0, 0.0);
  cairo_paint (cr);

  if (G_LIKELY (opacity > 0))
    {
      cairo_set_source_surface (cr, fade->end_surface, 0.0, 0.0);
      cairo_paint_with_alpha (cr, CLAMP (opacity, 0.0, 1.0));
    }

  cairo_destroy (cr);

  /* expose */
  thunar_desktop_background_expose (fade->background, opacity >= 1.0);

  return opacity < 1.0;
}



static void
thunar_desktop_background_fade_completed (gpointer data)
{
  BackgroundFade *fade = data;

  _thunar_return_if_fail (THUNAR_IS_DESKTOP_BACKGROUND (fade->background));
  _thunar_return_if_fail (GDK_IS_DRAWABLE (fade->background->pixmap));
  _thunar_return_if_fail (GDK_IS_WINDOW (fade->background->source_window));

  fade->background->fade_timeout_id = 0;

  /* cleanup */
  cairo_surface_destroy (fade->start_surface);
  cairo_surface_destroy (fade->end_surface);
  g_slice_free (BackgroundFade, fade);
}



static void
thunar_desktop_background_paint_free (BackgroundAsync *data)
{
  if (G_LIKELY (data != NULL))
    {
      g_clear_object (&data->image_file);
      g_clear_object (&data->pixbuf);
      g_free (data->base_prop);
      g_slice_free (BackgroundAsync, data);
    }
}



static void
thunar_desktop_background_paint_finished (GObject      *source_object,
                                          GAsyncResult *result,
                                          gpointer      user_data)
{
  ThunarDesktopBackground    *background = THUNAR_DESKTOP_BACKGROUND (source_object);
  GPtrArray                  *monitors = user_data;
  cairo_t                    *cr;
  BackgroundAsync            *data = g_ptr_array_index (monitors, 0);
  guint                       n;
  ThunarBackgroundColorStyle  color_style;
  gchar                       prop[128];
  GdkColor                    color_start;
  GdkColor                    color_end;
  cairo_surface_t            *start_surface;
  BackgroundFade             *fade;
  cairo_surface_t            *end_surface;
  cairo_pattern_t            *pattern;
  gint                        dx, dy;
  gboolean                    fade_animation;

  _thunar_return_if_fail (THUNAR_IS_DESKTOP_BACKGROUND (background));
  _thunar_return_if_fail (G_IS_TASK (result));

  /* abort on an error or cancellation */
  if (g_task_had_error (G_TASK (result)))
    {
      for (n = 0; n < monitors->len; n++)
        {
          data = g_ptr_array_index (monitors, n);
          thunar_desktop_background_paint_free (data);
        }

      goto err1;
    }

  /* prepare cairo context */
  cr = gdk_cairo_create (GDK_DRAWABLE (background->pixmap));
  g_assert (cairo_status (cr) == CAIRO_STATUS_SUCCESS);

  /* cache the old surface for the fade animation */
  fade_animation = data->fade_animation;
  if (fade_animation
      && xfconf_channel_get_bool (background->settings, "/background/fade-animation", TRUE))
    start_surface = thunar_desktop_background_get_surface (background, cr);
  else
    start_surface = NULL;

  for (n = 0; n < monitors->len; n++)
    {
      data = g_ptr_array_index (monitors, n);
      if (data == NULL)
        break;

      /* check if we should draw a background color (if we are not sure the background
       * image is not going to overlap the color anyway) */
      if (data->pixbuf == NULL
          || gdk_pixbuf_get_has_alpha (data->pixbuf)
          || data->bg_style == THUNAR_BACKGROUND_STYLE_NONE
          || data->bg_style == THUNAR_BACKGROUND_STYLE_TILED
          || data->bg_style == THUNAR_BACKGROUND_STYLE_CENTERED
          || data->bg_style == THUNAR_BACKGROUND_STYLE_SCALED)
        {
          /* get background style */
          g_snprintf (prop, sizeof (prop), "%s/color-style", data->base_prop);
          color_style = thunar_desktop_background_settings_enum (background->settings,
                                                                 THUNAR_TYPE_BACKGROUND_COLOR_STYLE,
                                                                 prop, DEFAULT_BACKGROUND_COLOR_STYLE);

          /* load the colors */
          g_snprintf (prop, sizeof (prop), "%s/color-start", data->base_prop);
          thunar_desktop_background_settings_color (background->settings, prop,
                                                    &color_start,
                                                    DEFAULT_BACKGROUND_COLOR_START);

          if (color_style != THUNAR_BACKGROUND_COLOR_STYLE_SOLID)
            {
              g_snprintf (prop, sizeof (prop), "%s/color-end", data->base_prop);
              thunar_desktop_background_settings_color (background->settings, prop,
                                                        &color_end,
                                                        DEFAULT_BACKGROUND_COLOR_END);
            }

          /* paint */
          thunar_desktop_background_paint_color_fill (cr, &data->geometry, color_style,
                                                      &color_start, &color_end);
        }

      if (data->pixbuf != NULL)
        {
          dx = data->geometry.x;
          dy = data->geometry.y;

          if (data->bg_style != THUNAR_BACKGROUND_STYLE_TILED)
            {
              /* center on monitor */
              dx += (data->geometry.width - gdk_pixbuf_get_width (data->pixbuf)) / 2;
              dy += (data->geometry.height - gdk_pixbuf_get_height (data->pixbuf)) / 2;
            }

          /* clip the drawing area */
          cairo_save (cr);
          gdk_cairo_rectangle (cr, &data->geometry);
          cairo_clip (cr);

          /* paint the image */
          thunar_gdk_cairo_set_source_pixbuf (cr, data->pixbuf, dx, dy);

          if (data->bg_style == THUNAR_BACKGROUND_STYLE_TILED)
            {
              pattern = cairo_get_source (cr);
              cairo_pattern_set_extend (pattern, CAIRO_EXTEND_REPEAT);
            }

          cairo_paint (cr);
          cairo_restore (cr);
        }

      /* clean the context data */
      thunar_desktop_background_paint_free (data);
    }

    /* if a fade animation was requested, start the timeout */
  if (start_surface != NULL)
    {
      end_surface = thunar_desktop_background_get_surface (background, cr);
      if (G_LIKELY (end_surface != NULL))
        {
          fade = g_slice_new0 (BackgroundFade);
          fade->background = background;
          fade->start_surface = start_surface;
          fade->end_surface = end_surface;

          /* "restore" the old background (start_time == 0) */
          thunar_desktop_background_fade_running (fade);

          /* start animation */
          fade->start_time = g_get_real_time ();
          background->fade_timeout_id = g_timeout_add_full (G_PRIORITY_DEFAULT_IDLE, FADE_ANIMATION_FPS,
                                                            thunar_desktop_background_fade_running, fade,
                                                            thunar_desktop_background_fade_completed);
        }
      else
        {
          /* cairo error, clear the start surface */
          cairo_surface_destroy (start_surface);
          thunar_desktop_background_expose (background, TRUE);
        }
    }
  else if (fade_animation)
    {
      /* animations are disabled, clear the window now */
      thunar_desktop_background_expose (background, TRUE);
    }

  cairo_destroy (cr);

  err1:

  /* unset stored job if owned by this job */
  if (background->task == G_TASK (result))
    background->task = NULL;

  g_ptr_array_free (monitors, TRUE);
  g_object_unref (G_OBJECT (result));
}



static void
thunar_desktop_background_paint_thread (GTask        *task,
                                        gpointer      source_object,
                                        gpointer      task_data,
                                        GCancellable *cancellable)
{
  GPtrArray        *monitors = task_data;
  BackgroundAsync  *data;
  GFileInputStream *stream;
  GError           *error = NULL;
  gchar            *path;
  GdkPixbuf        *pixbuf;
  GdkPixbuf        *scaled;
  guint             n;
  gboolean          scale_image;
  gint              src_w, src_h;
  gint              dst_w, dst_h;
  gdouble           wratio, hratio;

  for (n = 0; n < monitors->len; n++)
    {
      data = g_ptr_array_index (monitors, n);
      if (data == NULL)
        break;

      /* no image on this monitor */
      if (data->image_file == NULL
          || g_cancellable_is_cancelled (cancellable))
        continue;

      pixbuf = NULL;

      /* create the input stream */
      stream = g_file_read (data->image_file, cancellable, &error);
      if (stream != NULL)
        {
          /* load the pixbuf in all its glory */
          pixbuf = gdk_pixbuf_new_from_stream (G_INPUT_STREAM (stream), cancellable, &error);
          g_object_unref (G_OBJECT (stream));

          /* scale if needed */
          if (pixbuf != NULL
              && !g_cancellable_is_cancelled (cancellable))
            {
              src_w = gdk_pixbuf_get_width (pixbuf);
              src_h = gdk_pixbuf_get_height (pixbuf);

              dst_w = data->geometry.width;
              dst_h = data->geometry.height;

              scale_image = FALSE;

              if (src_w != dst_w || src_h != dst_h)
                {
                  switch (data->bg_style)
                    {
                    case THUNAR_BACKGROUND_STYLE_TILED:
                    case THUNAR_BACKGROUND_STYLE_CENTERED:
                    case THUNAR_BACKGROUND_STYLE_NONE:
                      /* no need to scale */
                      break;

                    case THUNAR_BACKGROUND_STYLE_STRETCHED:
                      /* resize to monitor size */
                      scale_image = TRUE;
                      break;

                    case THUNAR_BACKGROUND_STYLE_SCALED:
                      /* fit image inside available area */
                      wratio = (gdouble) src_w / (gdouble) dst_w;
                      hratio = (gdouble) src_h / (gdouble) dst_h;

                      if (hratio > wratio)
                        dst_w = rint (src_w / hratio);
                      else
                        dst_h = rint (src_h / wratio);

                      scale_image = TRUE;
                      break;

                    case THUNAR_BACKGROUND_STYLE_SPANNED:
                    case THUNAR_BACKGROUND_STYLE_ZOOMED:
                      /* fill available area, possibly cut edges */
                      wratio = (gdouble) src_w / (gdouble) dst_w;
                      hratio = (gdouble) src_h / (gdouble) dst_h;

                      if (hratio < wratio)
                        dst_w = rint (src_w / hratio);
                      else
                        dst_h = rint (src_h / wratio);

                      scale_image = TRUE;
                      break;
                    }
                }

              if (scale_image)
                {
                  /* scale now, we do this for the higher
                   * resize quality we can get */
                  scaled = gdk_pixbuf_scale_simple (pixbuf,
                                                    dst_w, dst_h,
                                                    data->image_interp);

                  /* take over */
                  g_object_unref (G_OBJECT (pixbuf));
                  pixbuf = scaled;
                }
            }
        }

      if (G_UNLIKELY (error != NULL))
        {
          /* we don't abort the thread for this, maybe the file is
           * removed or something like that; this does not mean
           * we should not try to render the background for the other
           * screens */
          if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            {
              path = g_file_get_parse_name (data->image_file);
              g_printerr ("Thunar: Unable to load image \"%s\": %s\n",
                          path, error->message);
              g_free (path);
            }
          g_clear_error (&error);
        }
      else if (!g_cancellable_is_cancelled (cancellable))
        {
          /* store the image */
          data->pixbuf = g_object_ref (G_OBJECT (pixbuf));

          /* prepare the cairo surface */
          if (!g_cancellable_is_cancelled (cancellable))
            thunar_gdk_cairo_set_source_pixbuf (NULL, data->pixbuf, 0, 0);
        }

      /* we took a ref if everything was not cancelled */
      g_clear_object (&pixbuf);
    }

  g_task_return_pointer (task, NULL, NULL);
}



static void
thunar_desktop_background_paint (ThunarDesktopBackground *background,
                                 gboolean                 fade_animation)
{
  GdkScreen       *screen;
  gint             n, n_monitors;
  GdkInterpType    interp;
  gchar           *monitor_name;
  gint             screen_num;
  gchar            prop[128];
  gchar           *uri;
  GPtrArray       *monitors;
  BackgroundAsync *data;

  _thunar_return_if_fail (THUNAR_IS_DESKTOP_BACKGROUND (background));
  _thunar_return_if_fail (GDK_IS_DRAWABLE (background->pixmap));

  /* stop pending animation */
  if (background->fade_timeout_id != 0)
    g_source_remove (background->fade_timeout_id);

  /* stop running task */
  if (background->task != NULL)
    {
      thunar_tasks_cancel (background->task);
      background->task = NULL;
    }

  /* screen info */
  screen = gdk_window_get_screen (background->source_window);
  screen_num = gdk_screen_get_number (screen);
  n_monitors = gdk_screen_get_n_monitors (screen);

  /* if the screen has a bit depth of less than 24bpp, using bilinear
   * filtering looks crappy (mainly with gradients). */
  if (gdk_drawable_get_depth (GDK_DRAWABLE (background->source_window)) < 24)
    interp = GDK_INTERP_HYPER;
  else
    interp = GDK_INTERP_BILINEAR;

  /* load loading structure */
  monitors = g_ptr_array_sized_new (n_monitors);

  /* draw each monitor */
  for (n = 0; n < n_monitors; n++)
    {
      /* create the data structure */
      data = g_slice_new0 (BackgroundAsync);
      data->fade_animation = fade_animation;
      data->image_interp = interp;
      g_ptr_array_add (monitors, data);

      /* get the base property for xfconf */
      monitor_name = thunar_desktop_background_get_monitor_name (screen, n);
      data->base_prop = g_strdup_printf ("/background/screen-%d/%s", screen_num, monitor_name);
      g_free (monitor_name);

      /* get background style */
      g_snprintf (prop, sizeof (prop), "%s/style", data->base_prop);
      data->bg_style = thunar_desktop_background_settings_enum (background->settings,
                                                                THUNAR_TYPE_BACKGROUND_STYLE,
                                                                prop, DEFAULT_BACKGROUND_STYLE);

      /* spanning works only on settings of the 1st monitor */
      if (data->bg_style == THUNAR_BACKGROUND_STYLE_SPANNED)
        {
          data->geometry.x = data->geometry.y = 0;
          data->geometry.width = gdk_screen_get_width (screen);
          data->geometry.height = gdk_screen_get_height (screen);
        }
      else
        {
          /* get area of the monitor */
          gdk_screen_get_monitor_geometry (screen, n, &data->geometry);
        }

      if (data->bg_style != THUNAR_BACKGROUND_STYLE_NONE)
        {
          /* get file name */
          g_snprintf (prop, sizeof (prop), "%s/uri", data->base_prop);
          uri = xfconf_channel_get_string (background->settings, prop, DEFAULT_BACKGROUND_URI);

          if (G_LIKELY (uri != NULL))
            {
              /* the image we're going to load in the thread */
              data->image_file = g_file_new_for_uri (uri);
              g_free (uri);
            }
        }

      /* when an image is spanned, we only draw it once */
      if (data->bg_style == THUNAR_BACKGROUND_STYLE_SPANNED)
        break;
    }

  /* start a thread to load and scale the images */
  background->task = thunar_tasks_new (background, thunar_desktop_background_paint_finished, monitors);
  g_task_set_priority (background->task, G_PRIORITY_LOW);
  g_task_set_task_data (background->task, monitors, NULL);
  g_task_run_in_thread (background->task, thunar_desktop_background_paint_thread);
}



static gint
thunar_desktop_background_info_sort (gconstpointer a,
                                     gconstpointer b)
{
  return g_strcmp0 (g_file_info_get_attribute_byte_string (G_FILE_INFO (a), "sortkey"),
                    g_file_info_get_attribute_byte_string (G_FILE_INFO (b), "sortkey"));
}



static gchar *
thunar_desktop_background_next_image (const gchar  *last_uri,
                                      gboolean      choose_random)
{
  GFile           *current_file;
  GFile           *parent_dir;
  GFileEnumerator *enumerator;
  GError          *error = NULL;
  GFileInfo       *info;
  GFileInfo       *current_info = NULL;
  GSList          *image_files = NULL;
  const gchar     *content_type;
  gchar           *key;
  guint            n, n_image_files = 0;
  GSList          *li = NULL;
  const gchar     *filename;
  GFile           *file;
  gchar           *path;
  gint             offset;
  gchar           *uri = NULL;

  _thunar_return_val_if_fail (last_uri != NULL, NULL);
  _thunar_return_val_if_fail (g_str_has_prefix (last_uri, "file:///"), NULL);

  /* get the parent file */
  current_file = g_file_new_for_uri (last_uri);
  parent_dir = g_file_get_parent (current_file);

  /* read the directory contents */
  enumerator = g_file_enumerate_children (parent_dir,
                                          G_FILE_ATTRIBUTE_STANDARD_TYPE ","
                                          G_FILE_ATTRIBUTE_STANDARD_NAME ","
                                          G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME ","
                                          G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
                                          G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN,
                                          G_FILE_QUERY_INFO_NONE,
                                          NULL, &error);

  if (enumerator != NULL)
    {
      for (;;)
        {
          /* query info of the child */
          info = g_file_enumerator_next_file (enumerator, NULL, NULL);
          if (G_UNLIKELY (info == NULL))
            break;

          /* skip hidden files and non-images */
          content_type = g_file_info_get_content_type (info);
          if (g_file_info_get_is_hidden (info)
              || !g_content_type_is_a (content_type, "image/*"))
            {
              g_object_unref (G_OBJECT (info));
              continue;
            }

          filename = g_file_info_get_name (info);

          /* add a collate key for sorting */
          key = g_utf8_collate_key_for_filename (filename, -1);
          g_file_info_set_attribute_byte_string (info, "sortkey", key);
          g_free (key);

          /* find the current file */
          file = g_file_get_child (parent_dir, filename);
          if (g_file_equal (file, current_file))
            current_info = g_object_ref (G_OBJECT (info));
          g_object_unref (G_OBJECT (file));

          /* keep the items in a list */
          image_files = g_slist_insert_sorted (image_files, info, thunar_desktop_background_info_sort);
          n_image_files++;
        }

      g_object_unref (G_OBJECT (enumerator));
    }
  else
    {
      g_critical ("Unable to find next background: %s", error->message);
      g_error_free (error);
    }

  if (image_files != NULL)
    {
      /* pick a place in the list */
      if (choose_random)
        {
          /* choose a random item in the list */
          offset = g_random_int_range (0, n_image_files);
          li = g_slist_nth (image_files, offset);
        }
      else if (current_info != NULL)
        {
          /* find the next item in the list */
          li = g_slist_find (image_files, current_info);
        }
      else
        {
          /* use head */
          li = image_files;
        }

      /* find next suitable image */
      for (n = 0; uri == NULL && n < n_image_files; n++)
        {
          /* check if we've hit the current item (once) */
          if (current_info != NULL
              && li->data == current_info)
            {
              li = g_slist_next (li);

              g_object_unref (G_OBJECT (current_info));
              current_info = NULL;
            }

          /* resume at the start of the list if we reached the tail */
          if (li == NULL)
            li = image_files;

          info = G_FILE_INFO (li->data);

          /* create gfile for reliable uri */
          file = g_file_get_child (parent_dir, g_file_info_get_name (info));

          /* load the image into the memory (exo uses mmap) */
          path = g_file_get_path (file);
          if (gdk_pixbuf_get_file_info (path, NULL, NULL) != NULL)
            uri = g_file_get_uri (file);
          g_free (path);

          g_object_unref (file);
        }
    }

  /* cleanup */
  g_slist_free_full (image_files, g_object_unref);
  g_object_unref (G_OBJECT (current_file));
  g_object_unref (G_OBJECT (parent_dir));
  if (current_info != NULL)
    g_object_unref (G_OBJECT (current_info));

  return uri;
}



static gboolean
thunar_desktop_background_cycle_timeout (gpointer data)
{
  ThunarDesktopBackground *background = THUNAR_DESKTOP_BACKGROUND (data);
  GdkScreen               *screen;
  gint                     screen_num;
  gint                     n, n_monitors;
  gchar                   *monitor_name;
  gchar                   *uri, *new_uri;
  gchar                    prop[128];
  ThunarBackgroundStyle    bg_style = THUNAR_BACKGROUND_STYLE_NONE;
  gboolean                 random_image;

  /* screen info */
  screen = gdk_window_get_screen (background->source_window);
  screen_num = gdk_screen_get_number (screen);
  n_monitors = gdk_screen_get_n_monitors (screen);

  /* whether random is enabled */
  random_image = xfconf_channel_get_bool (background->settings, "/background/cycle/random", FALSE);

  for (n = 0; bg_style != THUNAR_BACKGROUND_STYLE_SPANNED && n < n_monitors; n++)
    {
      /* get the monitor name */
      monitor_name = thunar_desktop_background_get_monitor_name (screen, n);

      g_snprintf (prop, sizeof (prop), "/background/screen-%d/%s/style", screen_num, monitor_name);
      bg_style = thunar_desktop_background_settings_enum (background->settings,
                                                          THUNAR_TYPE_BACKGROUND_STYLE,
                                                          prop, DEFAULT_BACKGROUND_STYLE);

      if (bg_style != THUNAR_BACKGROUND_STYLE_NONE)
        {
          /* get file name */
          g_snprintf (prop, sizeof (prop), "/background/screen-%d/%s/uri", screen_num, monitor_name);
          uri = xfconf_channel_get_string (background->settings, prop, DEFAULT_BACKGROUND_URI);
          if (uri != NULL
              && g_str_has_prefix (uri, "file:///"))
            {
              new_uri = thunar_desktop_background_next_image (uri, random_image);
              if (new_uri != NULL)
                xfconf_channel_set_string (background->settings, prop, new_uri);
              g_free (new_uri);
            }

          g_free (uri);
        }

      g_free (monitor_name);
    }

  return TRUE;
}



static void
thunar_desktop_background_cycle_timeout_destroyed (gpointer data)
{
  THUNAR_DESKTOP_BACKGROUND (data)->cycle_timeout_id = 0;
}



static void
thunar_desktop_background_cycle (ThunarDesktopBackground *background,
                                 gboolean                 startup)
{
  guint timeout;

  /* stop pending timer */
  if (background->cycle_timeout_id != 0)
    g_source_remove (background->cycle_timeout_id);

  if (xfconf_channel_get_bool (background->settings, "/background/cycle/enabled", FALSE))
    {
      timeout = xfconf_channel_get_uint (background->settings, "/background/cycle/time",
                                         DEFAULT_BACKGROUND_CYCLE_TIME);

      if (timeout == 0 && startup)
        {
          /* Cycle during login, so only fire once */
          thunar_desktop_background_cycle_timeout (background);
        }
      else if (timeout >= 10)
        {
          /* start the cycle timer */
          background->cycle_timeout_id =
              g_timeout_add_seconds_full (G_PRIORITY_LOW, timeout,
                  thunar_desktop_background_cycle_timeout, background,
                  thunar_desktop_background_cycle_timeout_destroyed);

          /* show we're doing something */
          if (!startup)
            thunar_desktop_background_cycle_timeout (background);
        }
    }
}



static gboolean
thunar_desktop_background_settings_changed_idle (gpointer data)
{
  ThunarDesktopBackground *background = THUNAR_DESKTOP_BACKGROUND (data);

  background->paint_idle_id = 0;

  thunar_desktop_background_paint (background, TRUE);

  return FALSE;
}



static void
thunar_desktop_background_settings_changed (XfconfChannel           *channel,
                                            const gchar             *property,
                                            const GValue            *value,
                                            ThunarDesktopBackground *background)
{
  _thunar_return_if_fail (THUNAR_IS_DESKTOP_BACKGROUND (background));
  _thunar_return_if_fail (XFCONF_IS_CHANNEL (channel));
  _thunar_return_if_fail (background->source_window == NULL
                          || GDK_IS_DRAWABLE (background->source_window));

  /* only respond to background changes */
  if (g_str_has_prefix (property, "/background/screen-")
      && background->paint_idle_id == 0)
    {
      /* idle to group multiple changes a bit */
      background->paint_idle_id =
          g_idle_add (thunar_desktop_background_settings_changed_idle, background);
    }
  else if (g_str_has_prefix (property, "/background/cycle/"))
    {
      /* restart cycle timer */
      thunar_desktop_background_cycle (background, FALSE);
    }
}



ThunarDesktopBackground *
thunar_desktop_background_new (GdkWindow *source_window)
{
  _thunar_return_val_if_fail (GDK_IS_WINDOW (source_window), NULL);
  return g_object_new (THUNAR_TYPE_DESKTOP_BACKGROUND,
                       "source-window", source_window, NULL);
}



guint
thunar_desktop_background_settings_enum (XfconfChannel *channel,
                                         GType          enum_type,
                                         const gchar   *prop,
                                         guint          default_value)
{
  gchar      *str;
  GEnumClass *klass;
  guint       value = default_value;
  guint       n;

  _thunar_return_val_if_fail (XFCONF_IS_CHANNEL (channel), 0);

  /* get string name from settings */
  str = xfconf_channel_get_string (channel, prop, NULL);
  if (str == NULL)
    return value;

  /* determine the enum value matching the src... */
  klass = g_type_class_ref (enum_type);
  _thunar_assert (G_IS_ENUM_CLASS (klass));
  for (n = 0; n < klass->n_values; ++n)
    {
      if (exo_str_is_equal (klass->values[n].value_name, str))
        {
          value = klass->values[n].value;
          break;
        }
    }
  g_type_class_unref (klass);
  g_free (str);

  return value;
}



void
thunar_desktop_background_settings_color (XfconfChannel *channel,
                                          const gchar   *prop,
                                          GdkColor      *color_return,
                                          const gchar   *default_color)
{
  gchar *str;

  _thunar_return_if_fail (XFCONF_IS_CHANNEL (channel));

  /* get string name from settings */
  str = xfconf_channel_get_string (channel, prop, default_color);
  if (!gdk_color_parse (str, color_return))
    {
      g_warning ("Failed to parse color %s from %s", str, prop);

      /* return black */
      color_return->red = 0;
      color_return->green = 0;
      color_return->blue = 0;
    }
  g_free (str);
}
