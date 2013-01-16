/*-
 * Copyright (c) 2013 Nick Schermer <nick@xfce.org>
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

#include <thunar/thunar-desktop-background-icon-view.h>
#include <thunar/thunar-private.h>



static void         thunar_desktop_background_icon_view_finalize               (GObject             *object);
static AtkObject   *thunar_desktop_background_icon_view_get_accessible         (GtkWidget           *widget);
static gboolean     thunar_desktop_background_icon_view_visible_func           (ThunarFile          *file,
                                                                                gpointer             data);



struct _ThunarDesktopBackgroundIconViewClass
{
  ThunarAbstractIconViewClass __parent__;
};

struct _ThunarDesktopBackgroundIconView
{
  ThunarAbstractIconView __parent__;

  GHashTable *mime_types;
};



G_DEFINE_TYPE (ThunarDesktopBackgroundIconView, thunar_desktop_background_icon_view, THUNAR_TYPE_ABSTRACT_ICON_VIEW)



static void
thunar_desktop_background_icon_view_class_init (ThunarDesktopBackgroundIconViewClass *klass)
{
  ThunarStandardViewClass *thunarstandard_view_class;
  GtkWidgetClass          *gtkwidget_class;
  GObjectClass            *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = thunar_desktop_background_icon_view_finalize;

  gtkwidget_class = GTK_WIDGET_CLASS (klass);
  gtkwidget_class->get_accessible = thunar_desktop_background_icon_view_get_accessible;

  thunarstandard_view_class = THUNAR_STANDARD_VIEW_CLASS (klass);
  thunarstandard_view_class->zoom_level_property_name = NULL;
}



static void
thunar_desktop_background_icon_view_init (ThunarDesktopBackgroundIconView *icon_view)
{
  ThunarListModel  *model;
  GSList           *formats;
  GSList           *lp;
  gchar           **mime_types;
  guint             n;
  GtkWidget        *exo_view;

  /* only a single selection is possible */
  exo_view = gtk_bin_get_child (GTK_BIN (icon_view));
  exo_icon_view_set_selection_mode (EXO_ICON_VIEW (exo_view), GTK_SELECTION_BROWSE);
  exo_icon_view_set_enable_search (EXO_ICON_VIEW (exo_view), TRUE);

  /* setup the icon renderer */
  g_object_set (G_OBJECT (THUNAR_STANDARD_VIEW (icon_view)->icon_renderer),
                "size", THUNAR_ICON_SIZE_LARGEST,
                NULL);

  /* setup the name renderer */
  g_object_set (G_OBJECT (THUNAR_STANDARD_VIEW (icon_view)->name_renderer),
                "visible", FALSE, NULL);

  /* collect gdk supported types */
  icon_view->mime_types = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  /* get a list of all formats supported by GdkPixbuf */
  formats = gdk_pixbuf_get_formats ();
  for (lp = formats; lp != NULL; lp = lp->next)
    {
      /* ignore the disabled ones */
      if (!gdk_pixbuf_format_is_disabled (lp->data))
        {
          /* get a list of MIME types supported by this format */
          mime_types = gdk_pixbuf_format_get_mime_types (lp->data);
          if (G_UNLIKELY (mime_types == NULL))
            continue;

          /* put them all in the unqiue MIME type hash table */
          for (n = 0; mime_types[n] != NULL; ++n)
            g_hash_table_replace (icon_view->mime_types, g_strdup (mime_types[n]), NULL);

          /* free the string array */
          g_strfreev (mime_types);
        }
    }
  g_slist_free (formats);

  /* only show gdkpixbuf supported files */
  model = THUNAR_STANDARD_VIEW (icon_view)->model;
  thunar_list_model_set_visible_func (model, thunar_desktop_background_icon_view_visible_func, icon_view);
}



static void
thunar_desktop_background_icon_view_finalize (GObject *object)
{
  ThunarDesktopBackgroundIconView *icon_view = THUNAR_DESKTOP_BACKGROUND_ICON_VIEW (object);

  g_hash_table_destroy (icon_view->mime_types);

  (*G_OBJECT_CLASS (thunar_desktop_background_icon_view_parent_class)->finalize) (object);
}



static AtkObject*
thunar_desktop_background_icon_view_get_accessible (GtkWidget *widget)
{
  AtkObject *object;

  /* query the atk object for the icon view class */
  object = (*GTK_WIDGET_CLASS (thunar_desktop_background_icon_view_parent_class)->get_accessible) (widget);

  /* set custom Atk properties for the icon view */
  if (G_LIKELY (object != NULL))
    {
      atk_object_set_description (object, _("Icon based directory listing"));
      atk_object_set_name (object, _("Icon view"));
      atk_object_set_role (object, ATK_ROLE_DIRECTORY_PANE);
    }

  return object;
}



static gboolean
thunar_desktop_background_icon_view_visible_func (ThunarFile *file,
                                                  gpointer    data)
{
  ThunarDesktopBackgroundIconView *icon_view = THUNAR_DESKTOP_BACKGROUND_ICON_VIEW (data);

  return g_hash_table_lookup_extended (icon_view->mime_types,
                                       thunar_file_get_content_type (file),
                                       NULL, NULL);
}
