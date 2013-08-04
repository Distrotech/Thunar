/* $Id$ */
/*-
 * Copyright (c) 2005-2006 Benedikt Meurer <benny@xfce.org>
 * Copyright (c) 2009 Jannis Pohlmann <jannis@xfce.org>
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

#include <gio/gio.h>

#include <thunar/thunar-icon-factory.h>
#include <thunar/thunar-tasks.h>
#include <thunar/thunar-private.h>
#include <thunar/thunar-templates-action.h>



/* Signal identifiers */
enum
{
  CREATE_EMPTY_FILE,
  CREATE_TEMPLATE,
  LAST_SIGNAL,
};



static void       thunar_templates_action_finalize          (GObject                    *object);
static GtkWidget *thunar_templates_action_create_menu_item  (GtkAction                  *action);
static void       thunar_templates_action_menu_shown        (GtkWidget                  *menu,
                                                             ThunarTemplatesAction      *templates_action);



struct _ThunarTemplatesActionClass
{
  GtkActionClass __parent__;

  void (*create_empty_file) (ThunarTemplatesAction *templates_action);
  void (*create_template)   (ThunarTemplatesAction *templates_action,
                             const ThunarFile      *file);
};

struct _ThunarTemplatesAction
{
  GtkAction  __parent__;

  GTask *task;
};



static guint templates_action_signals[LAST_SIGNAL];



G_DEFINE_TYPE (ThunarTemplatesAction, thunar_templates_action, GTK_TYPE_ACTION)



static void
thunar_templates_action_class_init (ThunarTemplatesActionClass *klass)
{
  GtkActionClass *gtkaction_class;
  GObjectClass   *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = thunar_templates_action_finalize;

  gtkaction_class = GTK_ACTION_CLASS (klass);
  gtkaction_class->create_menu_item = thunar_templates_action_create_menu_item;

  /**
   * ThunarTemplatesAction::create-empty-file:
   * @templates_action : a #ThunarTemplatesAction.
   *
   * Emitted by @templates_action whenever the user requests to
   * create a new empty file.
   **/
  templates_action_signals[CREATE_EMPTY_FILE] =
    g_signal_new (I_("create-empty-file"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ThunarTemplatesActionClass, create_empty_file),
                  NULL, NULL, g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  /**
   * ThunarTemplatesAction::create-template:
   * @templates_action : a #ThunarTemplatesAction.
   * @file             : the #ThunarFile of the template file.
   *
   * Emitted by @templates_action whenever the user requests to
   * create a new file based on the template referred to by
   * @file.
   **/
  templates_action_signals[CREATE_TEMPLATE] =
    g_signal_new (I_("create-template"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ThunarTemplatesActionClass, create_template),
                  NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1, THUNAR_TYPE_FILE);
}



static void
thunar_templates_action_init (ThunarTemplatesAction *templates_action)
{
}



static void
thunar_templates_action_finalize (GObject *object)
{
  ThunarTemplatesAction *templates_action = THUNAR_TEMPLATES_ACTION (object);

  if (templates_action->task != NULL)
    g_object_unref (templates_action->task);

  (*G_OBJECT_CLASS (thunar_templates_action_parent_class)->finalize) (object);
}



static GtkWidget*
thunar_templates_action_create_menu_item (GtkAction *action)
{
  GtkWidget *item;
  GtkWidget *menu;

  _thunar_return_val_if_fail (THUNAR_IS_TEMPLATES_ACTION (action), NULL);

  /* let GtkAction allocate the menu item */
  item = (*GTK_ACTION_CLASS (thunar_templates_action_parent_class)->create_menu_item) (action);

  /* associate an empty submenu with the item (will be filled when shown) */
  menu = gtk_menu_new ();
  g_signal_connect (G_OBJECT (menu), "show", G_CALLBACK (thunar_templates_action_menu_shown), action);
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), menu);

  return item;
}



static void
item_activated (GtkWidget             *item,
                ThunarTemplatesAction *templates_action)
{
  const ThunarFile *file;

  _thunar_return_if_fail (THUNAR_IS_TEMPLATES_ACTION (templates_action));
  _thunar_return_if_fail (GTK_IS_WIDGET (item));

  /* check if a file is set for the item (else it's the "Empty File" item) */
  file = g_object_get_data (G_OBJECT (item), I_("thunar-file"));
  if (G_UNLIKELY (file != NULL))
    {
      g_signal_emit (G_OBJECT (templates_action),
                     templates_action_signals[CREATE_TEMPLATE], 0, file);
    }
  else
    {
      g_signal_emit (G_OBJECT (templates_action),
                     templates_action_signals[CREATE_EMPTY_FILE], 0);
    }
}



static GtkWidget *
find_parent_menu (ThunarFile *file,
                  GList      *dirs,
                  GList      *items)
{
  GtkWidget *parent_menu = NULL;
  GFile     *parent;
  GList     *lp;
  GList     *ip;

  /* determine the parent of the file */
  parent = g_file_get_parent (thunar_file_get_file (file));

  /* check if the file has a parent at all */
  if (parent == NULL)
    return NULL;

  /* iterate over all dirs and menu items */
  for (lp = g_list_first (dirs), ip = g_list_first (items);
       parent_menu == NULL && lp != NULL && ip != NULL;
       lp = lp->next, ip = ip->next)
    {
      /* check if the current dir/item is the parent of our file */
      if (g_file_equal (parent, thunar_file_get_file (lp->data)))
        {
          /* we want to insert an item for the file in this menu */
          parent_menu = gtk_menu_item_get_submenu (ip->data);
        }
    }

  /* destroy the parent GFile */
  g_object_unref (parent);

  return parent_menu;
}



static gint
compare_files (ThunarFile *a,
               ThunarFile *b)
{
  GFile *file_a;
  GFile *file_b;
  GFile *parent_a;
  GFile *parent_b;

  file_a = thunar_file_get_file (a);
  file_b = thunar_file_get_file (b);

  /* check whether the files are equal */
  if (g_file_equal (file_a, file_b))
    return 0;

  /* directories always come first */
  if (thunar_file_get_kind (a) == G_FILE_TYPE_DIRECTORY
      && thunar_file_get_kind (b) != G_FILE_TYPE_DIRECTORY)
    {
      return -1;
    }
  else if (thunar_file_get_kind (a) != G_FILE_TYPE_DIRECTORY
           && thunar_file_get_kind (b) == G_FILE_TYPE_DIRECTORY)
    {
      return 1;
    }

  /* ancestors come first */
  if (g_file_has_prefix (file_b, file_a))
    return -1;
  else if (g_file_has_prefix (file_a, file_b))
    return 1;

  parent_a = g_file_get_parent (file_a);
  parent_b = g_file_get_parent (file_b);

  if (g_file_equal (parent_a, parent_b))
    {
      g_object_unref (parent_a);
      g_object_unref (parent_b);

      /* compare siblings by their display name */
      return g_utf8_collate (thunar_file_get_display_name (a),
                             thunar_file_get_display_name (b));
    }

  /* again, ancestors come first */
  if (g_file_has_prefix (file_b, parent_a))
    {
      g_object_unref (parent_a);
      g_object_unref (parent_b);

      return -1;
    }
  else if (g_file_has_prefix (file_a, parent_b))
    {
      g_object_unref (parent_a);
      g_object_unref (parent_b);

      return 1;
    }

  g_object_unref (parent_a);
  g_object_unref (parent_b);

  return 0;
}



static void
thunar_templates_action_files_ready (GtkWidget             *menu,
                                     GList                 *files,
                                     ThunarTemplatesAction *templates_action)
{
  ThunarIconFactory *icon_factory;
  ThunarFile        *file;
  GdkPixbuf         *icon;
  GtkWidget         *parent_menu;
  GtkWidget         *submenu;
  GtkWidget         *image;
  GtkWidget         *item;
  GList             *lp;
  GList             *dirs = NULL;
  GList             *items = NULL;
  GList             *parent_menus = NULL;
  GList             *pp;

  /* get the icon factory */
  icon_factory = thunar_icon_factory_get_default ();

  /* sort items so that directories come before files and ancestors come
   * before descendants */
  files = g_list_sort (files, (GCompareFunc) compare_files);

  for (lp = g_list_first (files); lp != NULL; lp = lp->next)
    {
      file = lp->data;

      /* determine the parent menu for this file/directory */
      parent_menu = find_parent_menu (file, dirs, items);
      parent_menu = parent_menu == NULL ? menu : parent_menu;

      if (thunar_file_get_kind (file) == G_FILE_TYPE_DIRECTORY)
        {
          /* allocate a new submenu for the directory */
          submenu = gtk_menu_new ();
          g_object_ref_sink (G_OBJECT (submenu));
          gtk_menu_set_screen (GTK_MENU (submenu), gtk_widget_get_screen (menu));

          /* allocate a new menu item for the directory */
          item = gtk_image_menu_item_new_with_label (thunar_file_get_display_name (file));
          gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), submenu);

          /* prepend the directory, its item and the parent menu it should
           * later be added to to the respective lists */
          dirs = g_list_prepend (dirs, file);
          items = g_list_prepend (items, item);
          parent_menus = g_list_prepend (parent_menus, parent_menu);
        }
      else
        {
          /* allocate a new menu item */
          item = gtk_image_menu_item_new_with_label (thunar_file_get_display_name (file));
          g_object_set_data_full (G_OBJECT (item), I_("thunar-file"),
                                  g_object_ref (file), g_object_unref);
          g_signal_connect (item, "activate", G_CALLBACK (item_activated),
                            templates_action);
          gtk_menu_shell_append (GTK_MENU_SHELL (parent_menu), item);
          gtk_widget_show (item);
        }

      /* determine the icon for this file/directory */
      icon = thunar_icon_factory_load_file_icon (icon_factory, file,
                                                 THUNAR_FILE_ICON_STATE_DEFAULT,
                                                 16);

      /* allocate an image based on the icon */
      image = gtk_image_new_from_pixbuf (icon);
      gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);

      /* release the icon reference */
      g_object_unref (icon);
    }

  /* add all non-empty directory items to their parent menu */
  for (lp = items, pp = parent_menus;
       lp != NULL && pp != NULL;
       lp = lp->next, pp = pp->next)
    {
      /* determine the submenu for this directory item */
      submenu = gtk_menu_item_get_submenu (GTK_MENU_ITEM (lp->data));

      if (GTK_MENU_SHELL (submenu)->children == NULL)
        {
          /* the directory submenu is empty, destroy it */
          gtk_widget_destroy (lp->data);
        }
      else
        {
          /* the directory has template files, so add it to its parent menu */
          gtk_menu_shell_prepend (GTK_MENU_SHELL (pp->data), lp->data);
          gtk_widget_show (lp->data);
        }
    }

  /* destroy lists */
  g_list_free (dirs);
  g_list_free (items);
  g_list_free (parent_menus);

  /* release the icon factory */
  g_object_unref (icon_factory);
}



static void
thunar_templates_action_append_empty_file (ThunarTemplatesAction *templates_action,
                                           GtkWidget             *menu)
{
  GtkWidget *image;
  GtkWidget *item;

  /* add the "Empty File" item */
  item = gtk_image_menu_item_new_with_mnemonic (_("_Empty File"));
  g_signal_connect (G_OBJECT (item), "activate", G_CALLBACK (item_activated),
                    templates_action);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  gtk_widget_show (item);

  /* add the icon for the emtpy file item */
  image = gtk_image_new_from_stock (GTK_STOCK_NEW, GTK_ICON_SIZE_MENU);
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
}



static void
thunar_templates_action_load_finished (GObject      *source_object,
                                       GAsyncResult *result,
                                       gpointer      user_data)
{
  ThunarTemplatesAction *templates_action = THUNAR_TEMPLATES_ACTION (source_object);
  GtkWidget             *item;
  GtkWidget             *menu = GTK_WIDGET (user_data);
  GList                 *files;
  GError                *error = NULL;

  _thunar_return_if_fail (THUNAR_IS_TEMPLATES_ACTION (templates_action));
  _thunar_return_if_fail (templates_action->task == G_TASK (result));
  _thunar_return_if_fail (GTK_IS_MENU (menu));

  files = g_task_propagate_pointer (templates_action->task, &error);
  if (error != NULL || files == NULL)
    {
      /* tell the user that no templates were found */
      item = gtk_menu_item_new_with_label (error != NULL ? error->message : _("No templates installed"));
      gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
      gtk_widget_set_sensitive (item, FALSE);
      gtk_widget_show (item);

      g_clear_error (&error);
    }
  else
    {
      /* add the files */
      thunar_templates_action_files_ready (menu, files, templates_action);
      thunar_g_file_list_free (files);
    }

  /* append a menu separator */
  item = gtk_separator_menu_item_new ();
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  gtk_widget_show (item);

  thunar_templates_action_append_empty_file (templates_action, menu);

  /* remove task */
  g_object_unref (templates_action->task);
  templates_action->task = NULL;
}



static GFile *
thunar_templates_action_get_template_directory (void)
{
  GFile       *homedir;
  GFile       *templates_dir;
  const gchar *path;

  homedir = thunar_g_file_new_for_home ();

  path = g_get_user_special_dir (G_USER_DIRECTORY_TEMPLATES);
  if (G_LIKELY (path != NULL))
    templates_dir = g_file_new_for_path (path);
  else
    templates_dir = g_file_resolve_relative_path (homedir, "Templates");

  /* don't load templates if this is disabled */
  if (g_file_equal (templates_dir, homedir))
    {
      g_object_unref (templates_dir);
      templates_dir = NULL;
    }

  g_object_unref (homedir);

  return templates_dir;
}



static void
thunar_templates_action_menu_shown (GtkWidget             *menu,
                                    ThunarTemplatesAction *templates_action)
{
  GList *children;
  GFile *templates_dir;

  _thunar_return_if_fail (THUNAR_IS_TEMPLATES_ACTION (templates_action));
  _thunar_return_if_fail (GTK_IS_MENU_SHELL (menu));

  /* drop all existing children of the menu first */
  children = gtk_container_get_children (GTK_CONTAINER (menu));
  g_list_free_full (children, (GDestroyNotify) gtk_widget_destroy);

  if (G_LIKELY (templates_action->task == NULL))
    {
      templates_dir = thunar_templates_action_get_template_directory ();
      if (templates_dir != NULL)
        {
          /* start the collection task */
          templates_action->task = thunar_tasks_new (templates_action, thunar_templates_action_load_finished, menu);
          thunar_tasks_list_directory (templates_action->task, templates_dir);
          g_object_unref (templates_dir);
        }
      else
        {
          /* only show our empty file option */
          thunar_templates_action_append_empty_file (templates_action, menu);
        }
    }
}



/**
 * thunar_templates_action_new:
 * @name  : the internal name of the action.
 * @label : the label for the action.
 *
 * Allocates a new #ThunarTemplatesAction with the given
 * @name and @label.
 *
 * Return value: the newly allocated #ThunarTemplatesAction.
 **/
GtkAction*
thunar_templates_action_new (const gchar *name,
                             const gchar *label)
{
  _thunar_return_val_if_fail (name != NULL, NULL);
  _thunar_return_val_if_fail (label != NULL, NULL);

  return g_object_new (THUNAR_TYPE_TEMPLATES_ACTION,
                       "hide-if-empty", FALSE,
                       "label", label,
                       "name", name,
                       NULL);
}


