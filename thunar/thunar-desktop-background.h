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

#ifndef __THUNAR_DESKTOP_BACKGROUND_H__
#define __THUNAR_DESKTOP_BACKGROUND_H__

#include <gtk/gtk.h>
#include <xfconf/xfconf.h>

/* defaults for the background */
#define DEFAULT_BACKGROUND_URI         "file://" DATADIR "/backgrounds/xfce/xfce-blue.jpg"
#define DEFAULT_BACKGROUND_STYLE       THUNAR_BACKGROUND_STYLE_ZOOMED
#define DEFAULT_BACKGROUND_COLOR_STYLE THUNAR_BACKGROUND_COLOR_STYLE_SOLID
#define DEFAULT_BACKGROUND_COLOR_START "#152233"
#define DEFAULT_BACKGROUND_COLOR_END   "#152233"
#define DEFAULT_BACKGROUND_CYCLE_TIME  900 /* 15 minutes */

G_BEGIN_DECLS

typedef struct _ThunarDesktopBackgroundClass ThunarDesktopBackgroundClass;
typedef struct _ThunarDesktopBackground      ThunarDesktopBackground;

#define THUNAR_TYPE_DESKTOP_BACKGROUND            (thunar_desktop_background_get_type ())
#define THUNAR_DESKTOP_BACKGROUND(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), THUNAR_TYPE_DESKTOP_BACKGROUND, ThunarDesktopBackground))
#define THUNAR_DESKTOP_BACKGROUND_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), THUNAR_TYPE_DESKTOP_BACKGROUND, ThunarDesktopBackgroundClass))
#define THUNAR_IS_DESKTOP_BACKGROUND(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), THUNAR_TYPE_DESKTOP_BACKGROUND))
#define THUNAR_IS_DESKTOP_BACKGROUND_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), THUNAR_TYPE_DESKTOP_BACKGROUND))
#define THUNAR_DESKTOP_BACKGROUND_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), THUNAR_TYPE_DESKTOP_BACKGROUND, ThunarDesktopBackgroundClass))

GType                    thunar_desktop_background_get_type         (void) G_GNUC_CONST;

ThunarDesktopBackground *thunar_desktop_background_new              (GdkWindow     *source_window);

guint                    thunar_desktop_background_settings_enum    (XfconfChannel *channel,
                                                                     GType          enum_type,
                                                                     const gchar   *prop,
                                                                     guint          default_value);

void                     thunar_desktop_background_settings_color   (XfconfChannel *channel,
                                                                     const gchar   *prop,
                                                                     GdkColor      *color_return,
                                                                     const gchar   *default_color);

G_END_DECLS

#endif /* !__THUNAR_DESKTOP_BACKGROUND_H__ */
