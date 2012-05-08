/* $Id$ */
/*-
 * Copyright (c) 2005-2006 Benedikt Meurer <benny@xfce.org>
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

#include <thunar/thunar-clipboard-manager.h>
#include <thunar/thunar-gobject-extensions.h>
#include <thunar/thunar-icon-factory.h>
#include <thunar/thunar-icon-renderer.h>
#include <thunar/thunar-private.h>



enum
{
  PROP_0,
  PROP_DROP_FILE,
  PROP_FILE,
  PROP_EMBLEMS,
  PROP_FOLLOW_STATE,
  PROP_SIZE,
};



static void thunar_icon_renderer_finalize      (GObject                 *object);
static void thunar_icon_renderer_get_property  (GObject                 *object,
                                                guint                    prop_id,
                                                GValue                  *value,
                                                GParamSpec              *pspec);
static void thunar_icon_renderer_set_property  (GObject                 *object,
                                                guint                    prop_id,
                                                const GValue            *value,
                                                GParamSpec              *pspec);
static void thunar_icon_renderer_get_size      (GtkCellRenderer         *cell,
                                                GtkWidget               *widget,
                                                const GdkRectangle      *cell_area,
                                                gint                    *x_offset,
                                                gint                    *y_offset,
                                                gint                    *width,
                                                gint                    *height);
static void thunar_icon_renderer_render        (GtkCellRenderer         *cell,
                                                cairo_t                 *cr,
                                                GtkWidget               *widget,
                                                const GdkRectangle      *background_area,
                                                const GdkRectangle      *cell_area,
                                                GtkCellRendererState     flags);



G_DEFINE_TYPE (ThunarIconRenderer, thunar_icon_renderer, GTK_TYPE_CELL_RENDERER)



static void
thunar_icon_renderer_class_init (ThunarIconRendererClass *klass)
{
  GtkCellRendererClass *gtkcell_renderer_class;
  GObjectClass         *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = thunar_icon_renderer_finalize;
  gobject_class->get_property = thunar_icon_renderer_get_property;
  gobject_class->set_property = thunar_icon_renderer_set_property;

  gtkcell_renderer_class = GTK_CELL_RENDERER_CLASS (klass);
  gtkcell_renderer_class->get_size = thunar_icon_renderer_get_size;
  gtkcell_renderer_class->render = thunar_icon_renderer_render;

  /**
   * ThunarIconRenderer:drop-file:
   *
   * The file which should be rendered in the drop
   * accept state.
   **/
  g_object_class_install_property (gobject_class,
                                   PROP_DROP_FILE,
                                   g_param_spec_object ("drop-file",
                                                        "drop-file",
                                                        "drop-file",
                                                        THUNAR_TYPE_FILE,
                                                        EXO_PARAM_READWRITE));

  /**
   * ThunarIconRenderer:file:
   *
   * The file whose icon to render.
   **/
  g_object_class_install_property (gobject_class,
                                   PROP_FILE,
                                   g_param_spec_object ("file", "file", "file",
                                                        THUNAR_TYPE_FILE,
                                                        EXO_PARAM_READWRITE));

  /**
   * ThunarIconRenderer:emblems:
   *
   * Specifies whether to render emblems in addition to the file icons.
   **/
  g_object_class_install_property (gobject_class,
                                   PROP_EMBLEMS,
                                   g_param_spec_boolean ("emblems",
                                                         "emblems",
                                                         "emblems",
                                                         TRUE,
                                                         G_PARAM_CONSTRUCT | EXO_PARAM_READWRITE));

  /**
   * ThunarIconRenderer:follow-state:
   *
   * Specifies whether the icon renderer should render icons
   * based on the selection state of the items. This is necessary
   * for #ExoIconView, which doesn't draw any item state indicators
   * itself.
   **/
  g_object_class_install_property (gobject_class,
                                   PROP_FOLLOW_STATE,
                                   g_param_spec_boolean ("follow-state",
                                                         "follow-state",
                                                         "follow-state",
                                                         FALSE,
                                                         EXO_PARAM_READWRITE));

  /**
   * ThunarIconRenderer:size:
   *
   * The size at which icons should be rendered by this
   * #ThunarIconRenderer instance.
   **/
  g_object_class_install_property (gobject_class,
                                   PROP_SIZE,
                                   g_param_spec_enum ("size", "size", "size",
                                                      THUNAR_TYPE_ICON_SIZE,
                                                      THUNAR_ICON_SIZE_SMALL,
                                                      G_PARAM_CONSTRUCT | EXO_PARAM_READWRITE));
}



static void
thunar_icon_renderer_init (ThunarIconRenderer *icon_renderer)
{
  /* use 1px padding */
  gtk_cell_renderer_set_padding (GTK_CELL_RENDERER (icon_renderer), 1, 1);
}



static void
thunar_icon_renderer_finalize (GObject *object)
{
  ThunarIconRenderer *icon_renderer = THUNAR_ICON_RENDERER (object);

  /* free the icon data */
  if (G_UNLIKELY (icon_renderer->drop_file != NULL))
    g_object_unref (G_OBJECT (icon_renderer->drop_file));
  if (G_LIKELY (icon_renderer->file != NULL))
    g_object_unref (G_OBJECT (icon_renderer->file));

  (*G_OBJECT_CLASS (thunar_icon_renderer_parent_class)->finalize) (object);
}



static void
thunar_icon_renderer_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  ThunarIconRenderer *icon_renderer = THUNAR_ICON_RENDERER (object);

  switch (prop_id)
    {
    case PROP_DROP_FILE:
      g_value_set_object (value, icon_renderer->drop_file);
      break;

    case PROP_FILE:
      g_value_set_object (value, icon_renderer->file);
      break;

    case PROP_EMBLEMS:
      g_value_set_boolean (value, icon_renderer->emblems);
      break;

    case PROP_FOLLOW_STATE:
      g_value_set_boolean (value, icon_renderer->follow_state);
      break;

    case PROP_SIZE:
      g_value_set_enum (value, icon_renderer->size);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static void
thunar_icon_renderer_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  ThunarIconRenderer *icon_renderer = THUNAR_ICON_RENDERER (object);

  switch (prop_id)
    {
    case PROP_DROP_FILE:
      if (G_LIKELY (icon_renderer->drop_file != NULL))
        g_object_unref (G_OBJECT (icon_renderer->drop_file));
      icon_renderer->drop_file = (gpointer) g_value_dup_object (value);
      break;

    case PROP_FILE:
      if (G_LIKELY (icon_renderer->file != NULL))
        g_object_unref (G_OBJECT (icon_renderer->file));
      icon_renderer->file = (gpointer) g_value_dup_object (value);
      break;

    case PROP_EMBLEMS:
      icon_renderer->emblems = g_value_get_boolean (value);
      break;

    case PROP_FOLLOW_STATE:
      icon_renderer->follow_state = g_value_get_boolean (value);
      break;

    case PROP_SIZE:
      icon_renderer->size = g_value_get_enum (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static void
thunar_icon_renderer_get_size (GtkCellRenderer    *renderer,
                               GtkWidget          *widget,
                               const GdkRectangle *rectangle,
                               gint               *x_offset,
                               gint               *y_offset,
                               gint               *width,
                               gint               *height)
{
  ThunarIconRenderer *icon_renderer = THUNAR_ICON_RENDERER (renderer);
  gint                xpad, ypad;
  gfloat              xalign, yalign;

  if (rectangle != NULL)
    {
      gtk_cell_renderer_get_padding (renderer, &xpad, &ypad);
      gtk_cell_renderer_get_alignment (renderer, &xalign, &yalign);
      
      if (x_offset != NULL)
        {
          *x_offset = ((gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL) ? 1.0 - xalign : xalign)
                    * (rectangle->width - icon_renderer->size);
          *x_offset = MAX (*x_offset, 0) + xpad;
        }

      if (y_offset != NULL)
        {
          *y_offset = yalign * (rectangle->height - icon_renderer->size);
          *y_offset = MAX (*y_offset, 0) + ypad;
        }
    }
  else
    {
      if (x_offset != NULL)
        *x_offset = 0;

      if (y_offset != NULL)
        *y_offset = 0;
    }

  if (G_LIKELY (width != NULL))
    *width = xpad * 2 + icon_renderer->size;

  if (G_LIKELY (height != NULL))
    *height = ypad * 2 + icon_renderer->size;
}



static void 
thunar_icon_renderer_render (GtkCellRenderer      *renderer,
                             cairo_t              *cr,
                             GtkWidget            *widget,
                             const GdkRectangle   *background_area,
                             const GdkRectangle   *cell_area,
                             GtkCellRendererState  flags)
{
  ThunarClipboardManager *clipboard;
  ThunarFileIconState     icon_state;
  ThunarIconRenderer     *icon_renderer = THUNAR_ICON_RENDERER (renderer);
  ThunarIconFactory      *icon_factory;
  GtkIconSource          *icon_source;
  GtkIconTheme           *icon_theme;
  GdkRectangle            emblem_area;
  GdkRectangle            icon_area;
  GdkRectangle            draw_area;
  GdkPixbuf              *emblem;
  GdkPixbuf              *icon;
  GdkPixbuf              *temp;
  GList                  *emblems;
  GList                  *lp;
  gint                    max_emblems;
  gint                    position;
  gboolean                is_expanded;
  GtkStyleContext         *style_context;

  if (G_UNLIKELY (icon_renderer->file == NULL))
    return;

  /* determine the icon state */
  g_object_get (icon_renderer, "is-expanded", &is_expanded, NULL);
  icon_state = (icon_renderer->drop_file != icon_renderer->file)
             ? is_expanded
              ? THUNAR_FILE_ICON_STATE_OPEN
              : THUNAR_FILE_ICON_STATE_DEFAULT
             : THUNAR_FILE_ICON_STATE_DROP;

  /* load the main icon */
  icon_theme = gtk_icon_theme_get_for_screen (gtk_widget_get_screen (widget));
  icon_factory = thunar_icon_factory_get_for_icon_theme (icon_theme);
  icon = thunar_icon_factory_load_file_icon (icon_factory, icon_renderer->file, icon_state, icon_renderer->size);
  if (G_UNLIKELY (icon == NULL))
    {
      g_object_unref (G_OBJECT (icon_factory));
      return;
    }

  /* pre-light the item if we're dragging about it */
  if (G_UNLIKELY (icon_state == THUNAR_FILE_ICON_STATE_DROP))
    flags |= GTK_CELL_RENDERER_PRELIT;

  /* determine the real icon size */
  icon_area.width = gdk_pixbuf_get_width (icon);
  icon_area.height = gdk_pixbuf_get_height (icon);

  /* scale down the icon on-demand */
  if (G_UNLIKELY (icon_area.width > cell_area->width || icon_area.height > cell_area->height))
    {
      /* scale down to fit */
      temp = exo_gdk_pixbuf_scale_down (icon, TRUE, cell_area->width, cell_area->height);
      g_object_unref (G_OBJECT (icon));
      icon = temp;

      /* determine the icon dimensions again */
      icon_area.width = gdk_pixbuf_get_width (icon);
      icon_area.height = gdk_pixbuf_get_height (icon);
    }

  icon_area.x = cell_area->x + (cell_area->width - icon_area.width) / 2;
  icon_area.y = cell_area->y + (cell_area->height - icon_area.height) / 2;

  style_context = gtk_widget_get_style_context (widget);
  gtk_style_context_save (style_context);

  /* check whether the icon is affected by the expose event */
  if (gdk_rectangle_intersect (cell_area, &icon_area, &draw_area))
    {
      /* use a translucent icon to represent cutted and hidden files to the user */
      clipboard = thunar_clipboard_manager_get_for_display (gtk_widget_get_display (widget));
      if (thunar_clipboard_manager_has_cutted_file (clipboard, icon_renderer->file))
        {
          /* 50% translucent for cutted files */
          temp = exo_gdk_pixbuf_lucent (icon, 50);
          g_object_unref (G_OBJECT (icon));
          icon = temp;
        }
      else if (thunar_file_is_hidden (icon_renderer->file))
        {
          /* 75% translucent for hidden files */
          temp = exo_gdk_pixbuf_lucent (icon, 75);
          g_object_unref (G_OBJECT (icon));
          icon = temp;
        }
      g_object_unref (G_OBJECT (clipboard));

      /* colorize the icon if we should follow the selection state */
      if ((flags & (GTK_CELL_RENDERER_SELECTED | GTK_CELL_RENDERER_PRELIT)) != 0 && icon_renderer->follow_state)
        {
          if ((flags & GTK_CELL_RENDERER_SELECTED) != 0)
            {
              /* TODO state = gtk_widget_has_focus (widget) ? GTK_STATE_SELECTED : GTK_STATE_ACTIVE;
              temp = exo_gdk_pixbuf_colorize (icon, &widget->style->base[state]);
              g_object_unref (G_OBJECT (icon));
              icon = temp; */
            }

          if ((flags & GTK_CELL_RENDERER_PRELIT) != 0)
            {
              temp = exo_gdk_pixbuf_spotlight (icon);
              g_object_unref (G_OBJECT (icon));
              icon = temp;
            }
        }

      /* check if we should render an insensitive icon */
      if (G_UNLIKELY (gtk_widget_get_state (widget) == GTK_STATE_INSENSITIVE
          || !gtk_cell_renderer_get_sensitive (renderer)))
        {
          /* allocate an icon source */
          icon_source = gtk_icon_source_new ();
          gtk_icon_source_set_pixbuf (icon_source, icon);
          gtk_icon_source_set_size_wildcarded (icon_source, FALSE);
          gtk_icon_source_set_size (icon_source, GTK_ICON_SIZE_SMALL_TOOLBAR);

          /* render the insensitive icon */
          temp = gtk_render_icon_pixbuf (style_context, icon_source, -1);
          g_object_unref (G_OBJECT (icon));
          icon = temp;

          /* release the icon source */
          gtk_icon_source_free (icon_source);
        }

      /* render the invalid parts of the icon */
      gtk_render_icon (style_context, cr, icon, icon_area.x, icon_area.y);
    }

  /* release the file's icon */
  g_object_unref (G_OBJECT (icon));

  /* check if we should render emblems as well */
  if (G_LIKELY (icon_renderer->emblems))
    {
      /* display the primary emblem as well (if any) */
      emblems = thunar_file_get_emblem_names (icon_renderer->file);
      if (G_UNLIKELY (emblems != NULL))
        {
          /* render up to four emblems for sizes from 48 onwards, else up to 2 emblems */
          max_emblems = (icon_renderer->size < 48) ? 2 : 4;

          /* render the emblems */
          for (lp = emblems, position = 0; lp != NULL && position < max_emblems; lp = lp->next)
            {
              /* check if we have the emblem in the icon theme */
              emblem = thunar_icon_factory_load_icon (icon_factory, lp->data, icon_renderer->size, NULL, FALSE);
              if (G_UNLIKELY (emblem == NULL))
                continue;

              /* determine the dimensions of the emblem */
              emblem_area.width = gdk_pixbuf_get_width (emblem);
              emblem_area.height = gdk_pixbuf_get_height (emblem);

              /* shrink insane emblems */
              if (G_UNLIKELY (MAX (emblem_area.width, emblem_area.height) > (gint) MIN ((2 * icon_renderer->size) / 3, 32)))
                {
                  /* scale down the emblem */
                  temp = exo_gdk_pixbuf_scale_ratio (emblem, MIN ((2 * icon_renderer->size) / 3, 32));
                  g_object_unref (G_OBJECT (emblem));
                  emblem = temp;

                  /* determine the size again */
                  emblem_area.width = gdk_pixbuf_get_width (emblem);
                  emblem_area.height = gdk_pixbuf_get_height (emblem);
                }

              /* determine a good position for the emblem, depending on the position index */
              switch (position)
                {
                case 0: /* right/bottom */
                  emblem_area.x = MIN (icon_area.x + icon_area.width - emblem_area.width / 2,
                                       cell_area->x + cell_area->width - emblem_area.width);
                  emblem_area.y = MIN (icon_area.y + icon_area.height - emblem_area.height / 2,
                                       cell_area->y + cell_area->height -emblem_area.height);
                  break;

                case 1: /* left/bottom */
                  emblem_area.x = MAX (icon_area.x - emblem_area.width / 2,
                                       cell_area->x);
                  emblem_area.y = MIN (icon_area.y + icon_area.height - emblem_area.height / 2,
                                       cell_area->y + cell_area->height -emblem_area.height);
                  break;

                case 2: /* left/top */
                  emblem_area.x = MAX (icon_area.x - emblem_area.width / 2,
                                       cell_area->x);
                  emblem_area.y = MAX (icon_area.y - emblem_area.height / 2,
                                       cell_area->y);
                  break;

                case 3: /* right/top */
                  emblem_area.x = MIN (icon_area.x + icon_area.width - emblem_area.width / 2,
                                       cell_area->x + cell_area->width - emblem_area.width);
                  emblem_area.y = MAX (icon_area.y - emblem_area.height / 2,
                                       cell_area->y);
                  break;

                default:
                  _thunar_assert_not_reached ();
                }

              /* render the emblem */
              if (gdk_rectangle_intersect (cell_area, &emblem_area, &draw_area))
                {
                  gtk_render_icon (style_context, cr, emblem, emblem_area.x, emblem_area.y);
                }

              /* release the emblem */
              g_object_unref (G_OBJECT (emblem));

              /* advance the position index */
              ++position;
            }

          /* release the emblem name list */
          g_list_free (emblems);
        }
    }

  gtk_style_context_restore (style_context);

  /* release our reference on the icon factory */
  g_object_unref (G_OBJECT (icon_factory));
}



/**
 * thunar_icon_renderer_new:
 *
 * Creates a new #ThunarIconRenderer. Adjust rendering
 * parameters using object properties. Object properties can be
 * set globally with #g_object_set. Also, with #GtkTreeViewColumn,
 * you can bind a property to a value in a #GtkTreeModel.
 *
 * Return value: the newly allocated #ThunarIconRenderer.
 **/
GtkCellRenderer*
thunar_icon_renderer_new (void)
{
  return g_object_new (THUNAR_TYPE_ICON_RENDERER, NULL);
}


