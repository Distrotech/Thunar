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
#include <thunar/thunar-gdk-extensions.h>

#define FADE_ANIMATION_USEC ((gdouble) G_USEC_PER_SEC / 2.0) /* 0.5 second */
#define FADE_ANIMATION_FPS  (1000 / 60)



enum
{
  PROP_0,
  PROP_SOURCE_WINDOW,
};



static void     thunar_desktop_background_constructed           (GObject                 *object);
static void     thunar_desktop_background_finalize              (GObject                 *object);
static void     thunar_desktop_background_get_property          (GObject                 *object,
                                                                 guint                    prop_id,
                                                                 GValue                  *value,
                                                                 GParamSpec              *pspec);
static void     thunar_desktop_background_set_property          (GObject                 *object,
                                                                 guint                    prop_id,
                                                                 const GValue            *value,
                                                                 GParamSpec              *pspec);
static void     thunar_desktop_background_expose                (ThunarDesktopBackground *background,
                                                                 gboolean                 pseudo_transparency);
static void     thunar_desktop_background_paint                 (ThunarDesktopBackground *background,
                                                                 gboolean                 fade_animation);
static void     thunar_desktop_background_settings_changed      (XfconfChannel           *channel,
                                                                 const gchar             *property,
                                                                 const GValue            *value,
                                                                 ThunarDesktopBackground *background);
static guint    thunar_desktop_background_settings_enum         (XfconfChannel           *channel,
                                                                 GType                    enum_type,
                                                                 const gchar             *prop);
static void     thunar_desktop_background_settings_color        (XfconfChannel           *channel,
                                                                 const gchar             *prop,
                                                                 GdkColor                *color_return);



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

  guint          fade_timeout_id;
};

typedef struct
{
  ThunarDesktopBackground *background;
  cairo_surface_t         *start_surface;
  cairo_surface_t         *end_surface;
  gint64                   start_time;
}
BackgroundFade;



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

  g_object_class_install_property (gobject_class,
                                   PROP_SOURCE_WINDOW,
                                   g_param_spec_object ("source-window", NULL, NULL,
                                                        GDK_TYPE_WINDOW,
                                                        EXO_PARAM_READWRITE
                                                        | G_PARAM_CONSTRUCT_ONLY));
}



static void
thunar_desktop_background_init (ThunarDesktopBackground *background)
{
  background->settings = xfconf_channel_get ("thunar-desktop");
  g_signal_connect (G_OBJECT (background->settings), "property-changed",
                    G_CALLBACK (thunar_desktop_background_settings_changed), background);
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

  /* render images and colors */
  thunar_desktop_background_paint (background, FALSE);

  /* set our background */
  gdk_window_set_back_pixmap (background->source_window, background->pixmap, FALSE);

  /* expose views */
  thunar_desktop_background_expose (background, TRUE);
}



static void
thunar_desktop_background_finalize (GObject *object)
{
  ThunarDesktopBackground *background = THUNAR_DESKTOP_BACKGROUND (object);
  GdkWindow               *root_window;

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

  if (background->fade_timeout_id != 0)
    g_source_remove (background->fade_timeout_id);

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



static void
thunar_desktop_background_paint_image (cairo_t               *cr,
                                       const GdkRectangle    *area,
                                       GdkInterpType          interp,
                                       ThunarBackgroundStyle  style,
                                       const GdkPixbuf       *src_pixbuf)
{
  gint             src_w, src_h;
  GdkPixbuf       *dst_pixbuf = NULL;
  gint             dx, dy;
  gint             dst_w, dst_h;
  gdouble          wratio, hratio;
  gboolean         scale_dst = FALSE;
  cairo_pattern_t *pattern;

  _thunar_return_if_fail (GDK_IS_PIXBUF (src_pixbuf));

  src_w = gdk_pixbuf_get_width (src_pixbuf);
  src_h = gdk_pixbuf_get_height (src_pixbuf);

  dx = area->x;
  dy = area->y;

  dst_w = area->width;
  dst_h = area->height;

  switch (style)
    {
    case THUNAR_BACKGROUND_STYLE_NONE:
      return;

    case THUNAR_BACKGROUND_STYLE_TILED:
      break;

    case THUNAR_BACKGROUND_STYLE_CENTERED:
      dx += (area->width - src_w) / 2;
      dy += (area->height - src_h) / 2;
      break;

    case THUNAR_BACKGROUND_STYLE_STRETCHED:
      if (src_w != dst_w || src_h != dst_h)
        {
          /* scale to fill screen */
          scale_dst = TRUE;
        }
      break;

    case THUNAR_BACKGROUND_STYLE_SCALED:
      if (src_w != dst_w || src_h != dst_h)
        {
          /* calculate the new dimensions */
          wratio = (gdouble) src_w / (gdouble) dst_w;
          hratio = (gdouble) src_h / (gdouble) dst_h;

          if (hratio > wratio)
            dst_w = rint (src_w / hratio);
          else
            dst_h = rint (src_h / wratio);

          /* scale to monitor, no corp */
          scale_dst = TRUE;
        }
      break;

    case THUNAR_BACKGROUND_STYLE_SPANNED:
    case THUNAR_BACKGROUND_STYLE_ZOOMED:
      if (src_w != dst_w || src_h != dst_h)
        {
          /* calculate the new dimensions */
          wratio = (gdouble) src_w / (gdouble) dst_w;
          hratio = (gdouble) src_h / (gdouble) dst_h;

          if (hratio < wratio)
            dst_w = rint (src_w / hratio);
          else
            dst_h = rint (src_h / wratio);

          /* scale to monitor, no corp */
          scale_dst = TRUE;
        }
      break;
    }

  if (scale_dst)
    {
      /* scale source */
      dst_pixbuf = gdk_pixbuf_scale_simple (src_pixbuf, MAX (1, dst_w), MAX (1, dst_h), interp);

      /* center on monitor */
      dx += (area->width - dst_w) / 2;
      dy += (area->height - dst_h) / 2;
    }
  else
    {
      /* no scaling was required, ref source */
      dst_pixbuf = g_object_ref (G_OBJECT (src_pixbuf));
    }

  /* clip area */
  cairo_save (cr);
  gdk_cairo_rectangle (cr, area);
  cairo_clip (cr);

  /* paint the image */
  thunar_gdk_cairo_set_source_pixbuf (cr, dst_pixbuf, dx, dy);

  if (style == THUNAR_BACKGROUND_STYLE_TILED)
    {
      pattern = cairo_get_source (cr);
      cairo_pattern_set_extend (pattern, CAIRO_EXTEND_REPEAT);
    }

  cairo_paint (cr);
  cairo_restore (cr);

  g_object_unref (G_OBJECT (dst_pixbuf));
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
thunar_desktop_background_paint (ThunarDesktopBackground *background,
                                 gboolean                 fade_animation)
{
  cairo_t                    *cr;
  GdkScreen                  *screen;
  gint                        n, n_monitors;
  GdkRectangle                area;
  GdkInterpType               interp;
  GdkPixbuf                  *pixbuf;
  cairo_surface_t            *start_surface;
  cairo_surface_t            *end_surface;
  BackgroundFade             *fade;
  ThunarBackgroundStyle       bg_style;
  ThunarBackgroundColorStyle  color_style;
  GError                     *error = NULL;
  gchar                      *monitor_name;
  gint                        screen_num;
  gchar                       prop[128];
  GdkColor                    color_start;
  GdkColor                    color_end;
  gchar                      *filename;
  gchar                      *uri;

  _thunar_return_if_fail (THUNAR_IS_DESKTOP_BACKGROUND (background));
  _thunar_return_if_fail (GDK_IS_DRAWABLE (background->pixmap));

  /* stop pending animation */
  if (background->fade_timeout_id != 0)
    g_source_remove (background->fade_timeout_id);

  /* prepare cairo context */
  cr = gdk_cairo_create (GDK_DRAWABLE (background->pixmap));
  g_assert (cairo_status (cr) == CAIRO_STATUS_SUCCESS);

  /* cache the old surface for the fade animation */
  if (fade_animation
      && xfconf_channel_get_bool (background->settings, "/background/fade-animation", TRUE))
    start_surface = thunar_desktop_background_get_surface (background, cr);
  else
    start_surface = NULL;

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

  /* draw each monitor */
  for (n = 0; n < n_monitors; n++)
    {
      /* get the monitor name */
      monitor_name = gdk_screen_get_monitor_plug_name (screen, n);
      if (G_UNLIKELY (monitor_name == NULL))
        monitor_name = g_strdup_printf ("monitor-%d", n);

      /* get background style */
      g_snprintf (prop, sizeof (prop), "/background/screen-%d/%s/style", screen_num, monitor_name);
      bg_style = thunar_desktop_background_settings_enum (background->settings,
                                                          THUNAR_TYPE_BACKGROUND_STYLE,
                                                          prop);

      /* spanning works only on settings of the 1st monitor */
      if (bg_style == THUNAR_BACKGROUND_STYLE_SPANNED)
        {
          area.x = area.y = 0;
          area.width = gdk_screen_get_width (screen);
          area.height = gdk_screen_get_height (screen);
        }
      else
        {
          /* get area of the monitor */
          gdk_screen_get_monitor_geometry (screen, n, &area);
        }

      pixbuf = NULL;

      if (bg_style != THUNAR_BACKGROUND_STYLE_NONE)
        {
          /* get file name */
          g_snprintf (prop, sizeof (prop), "/background/screen-%d/%s/uri", screen_num, monitor_name);
          uri = xfconf_channel_get_string (background->settings, prop, NULL);

          /* only support local files */
          if (G_LIKELY (uri != NULL
              && g_str_has_prefix (uri, "file:///")))
            {
              filename = g_filename_from_uri (uri, NULL, NULL);
              if (G_LIKELY (filename != NULL))
                {
                  /* load the image into the memory (exo uses mmap) */
                  pixbuf = exo_gdk_pixbuf_new_from_file_at_max_size (filename, G_MAXINT, G_MAXINT, TRUE, &error);
                  if (G_UNLIKELY (pixbuf == NULL))
                    {
                      g_warning ("Unable to load image \"%s\": %s",
                                 filename, error != NULL ? error->message : "No error");
                      g_error_free (error);
                    }
                  g_free (filename);
                }
            }
          g_free (uri);
        }

      /* check if we should draw a background color (if we are not sure the background
       * image is not going to overlap the color anyway) */
      if (pixbuf == NULL
          || gdk_pixbuf_get_has_alpha (pixbuf)
          || bg_style == THUNAR_BACKGROUND_STYLE_NONE
          || bg_style == THUNAR_BACKGROUND_STYLE_TILED
          || bg_style == THUNAR_BACKGROUND_STYLE_CENTERED
          || bg_style == THUNAR_BACKGROUND_STYLE_SCALED)
        {
          /* get background style */
          g_snprintf (prop, sizeof (prop), "/background/screen-%d/%s/color-style", screen_num, monitor_name);
          color_style = thunar_desktop_background_settings_enum (background->settings,
                                                                 THUNAR_TYPE_BACKGROUND_COLOR_STYLE,
                                                                 prop);

          /* load the colors */
          g_snprintf (prop, sizeof (prop), "/background/screen-%d/%s/color-start", screen_num, monitor_name);
          thunar_desktop_background_settings_color (background->settings, prop, &color_start);
          g_snprintf (prop, sizeof (prop), "/background/screen-%d/%s/color-end", screen_num, monitor_name);
          thunar_desktop_background_settings_color (background->settings, prop, &color_end);

          /* paint */
          thunar_desktop_background_paint_color_fill (cr, &area, color_style, &color_start, &color_end);
        }

      if (G_LIKELY (pixbuf != NULL))
        {
          /* paint the image */
          thunar_desktop_background_paint_image (cr, &area, interp, bg_style, pixbuf);
          g_object_unref (G_OBJECT (pixbuf));
        }

      g_free (monitor_name);

      /* only need to draw once for spanning */
      if (bg_style == THUNAR_BACKGROUND_STYLE_SPANNED)
        break;
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
  if (g_str_has_prefix (property, "/background/screen-"))
    thunar_desktop_background_paint (background, TRUE);
}



static guint
thunar_desktop_background_settings_enum (XfconfChannel *channel,
                                         GType          enum_type,
                                         const gchar   *prop)
{
  gchar      *str;
  GEnumClass *klass;
  guint       value = 0;
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



static void
thunar_desktop_background_settings_color (XfconfChannel *channel,
                                          const gchar   *prop,
                                          GdkColor      *color_return)
{
  gchar *str;

  _thunar_return_if_fail (XFCONF_IS_CHANNEL (channel));

  /* get string name from settings */
  str = xfconf_channel_get_string (channel, prop, "#000000");
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



ThunarDesktopBackground *
thunar_desktop_background_new (GdkWindow *source_window)
{
  _thunar_return_val_if_fail (GDK_IS_WINDOW (source_window), NULL);
  return g_object_new (THUNAR_TYPE_DESKTOP_BACKGROUND,
                       "source-window", source_window, NULL);
}
