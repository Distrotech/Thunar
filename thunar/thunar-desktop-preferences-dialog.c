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

#include <libxfce4ui/libxfce4ui.h>
#include <xfconf/xfconf.h>

#include <thunar/thunar-desktop-preferences-dialog.h>
#include <thunar/thunar-enum-types.h>
#include <thunar/thunar-desktop-background-icon-view.h>
#include <thunar/thunar-private.h>



static void thunar_desktop_preferences_dialog_finalize                  (GObject                        *object);
static void thunar_desktop_preferences_dialog_realize                   (GtkWidget                      *widget);
static void thunar_desktop_preferences_dialog_screen_changed            (GtkWidget                      *widget,
                                                                         GdkScreen                      *old_screen);
static void thunar_desktop_preferences_dialog_response                  (GtkDialog                      *dialog,
                                                                         gint                            response);
static void thunar_desktop_preferences_dialog_background_changed        (ThunarDesktopPreferencesDialog *dialog);
static void thunar_desktop_preferences_dialog_background_prop           (ThunarDesktopPreferencesDialog *dialog);
static void thunar_desktop_preferences_dialog_update                    (ThunarDesktopPreferencesDialog *dialog);
static void thunar_desktop_preferences_dialog_folder_changed            (ThunarDesktopPreferencesDialog *dialog);


struct _ThunarDesktopPreferencesDialogClass
{
  XfceTitledDialogClass __parent__;
};

struct _ThunarDesktopPreferencesDialog
{
  XfceTitledDialog __parent__;

  XfconfChannel *settings;

  gchar         *background_prop;

  GtkWidget     *view;
  GtkWidget     *folder_chooser;
};



G_DEFINE_TYPE (ThunarDesktopPreferencesDialog, thunar_desktop_preferences_dialog, XFCE_TYPE_TITLED_DIALOG)



static void
thunar_desktop_preferences_dialog_class_init (ThunarDesktopPreferencesDialogClass *klass)
{
  GtkDialogClass *gtkdialog_class;
  GObjectClass   *gobject_class;
  GtkWidgetClass *gtkwidget_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = thunar_desktop_preferences_dialog_finalize;

  gtkwidget_class = GTK_WIDGET_CLASS (klass);
  gtkwidget_class->realize = thunar_desktop_preferences_dialog_realize;
  gtkwidget_class->screen_changed = thunar_desktop_preferences_dialog_screen_changed;

  gtkdialog_class = GTK_DIALOG_CLASS (klass);
  gtkdialog_class->response = thunar_desktop_preferences_dialog_response;
}



static void
thunar_desktop_preferences_dialog_init (ThunarDesktopPreferencesDialog *dialog)
{
  GtkWidget    *notebook;
  GtkWidget    *label;
  GtkWidget    *vbox;
  GtkWidget    *hbox;
  GtkWidget    *combo;
  GtkWidget    *button;
  GtkWidget    *button2;
  GEnumClass   *klass;
  guint         n;
  GtkSizeGroup *size_group;

  dialog->settings = xfconf_channel_get ("thunar-desktop");

  /* configure the dialog properties */
  gtk_window_set_icon_name (GTK_WINDOW (dialog), "preferences-desktop-wallpaper");
  gtk_window_set_title (GTK_WINDOW (dialog), _("Desktop Preferences"));
  gtk_window_set_default_size (GTK_WINDOW (dialog), 900, 700);

  /* add "Help" and "Close" buttons */
  gtk_dialog_add_buttons (GTK_DIALOG (dialog),
                          GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                          GTK_STOCK_HELP, GTK_RESPONSE_HELP,
                          NULL);

  notebook = gtk_notebook_new ();
  gtk_container_set_border_width (GTK_CONTAINER (notebook), 6);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), notebook, TRUE, TRUE, 0);
  gtk_widget_show (notebook);

  /*
     Display
   */
  label = gtk_label_new (_("Background"));
  vbox = g_object_new (GTK_TYPE_VBOX, "border-width", 12, "spacing", 6, NULL);
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), vbox, label);
  gtk_widget_show (label);
  gtk_widget_show (vbox);

  dialog->view = g_object_new (THUNAR_TYPE_DESKTOP_BACKGROUND_ICON_VIEW, NULL);
  g_signal_connect_swapped (G_OBJECT (dialog->view), "notify::selected-files",
      G_CALLBACK (thunar_desktop_preferences_dialog_background_changed), dialog);
  gtk_box_pack_start (GTK_BOX (vbox), dialog->view, TRUE, TRUE, 0);
  gtk_widget_show (dialog->view);

  size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

  hbox = gtk_hbox_new (FALSE, 12);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, TRUE, 0);
  gtk_widget_show (hbox);

  label = gtk_label_new_with_mnemonic ("_Folder:");
  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, TRUE, 0);
  gtk_size_group_add_widget (size_group, label);
  gtk_widget_show (label);

  dialog->folder_chooser = gtk_file_chooser_button_new (_("Choose a background images folder"), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
  gtk_box_pack_start (GTK_BOX (hbox), dialog->folder_chooser, FALSE, TRUE, 0);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), dialog->folder_chooser);
  gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (dialog->folder_chooser), TRUE);
  g_signal_connect_swapped (G_OBJECT (dialog->folder_chooser), "current-folder-changed",
      G_CALLBACK (thunar_desktop_preferences_dialog_folder_changed), dialog);
  gtk_widget_show (dialog->folder_chooser);

  /* spacer */
  label = g_object_new (GTK_TYPE_LABEL, NULL);
  gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 0);
  gtk_widget_show (label);

  label = gtk_label_new_with_mnemonic ("_Style:");
  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, TRUE, 0);
  gtk_widget_show (label);

  combo = gtk_combo_box_text_new ();
  gtk_box_pack_start (GTK_BOX (hbox), combo, FALSE, TRUE, 0);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo);
  gtk_widget_show (combo);

  klass = g_type_class_ref (THUNAR_TYPE_BACKGROUND_STYLE);
  for (n = 0; n < klass->n_values; ++n)
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _(klass->values[n].value_nick));
  g_type_class_unref (klass);

  hbox = gtk_hbox_new (FALSE, 12);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, TRUE, 0);
  gtk_widget_show (hbox);

  label = gtk_label_new_with_mnemonic ("C_olors:");
  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, TRUE, 0);
  gtk_size_group_add_widget (size_group, label);
  gtk_widget_show (label);

  g_object_unref (size_group);

  combo = gtk_combo_box_text_new ();
  gtk_box_pack_start (GTK_BOX (hbox), combo, FALSE, TRUE, 0);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo);
  gtk_widget_show (combo);

  klass = g_type_class_ref (THUNAR_TYPE_BACKGROUND_COLOR_STYLE);
  for (n = 0; n < klass->n_values; ++n)
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _(klass->values[n].value_nick));
  g_type_class_unref (klass);

  button = gtk_color_button_new ();
  gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, TRUE, 0);
  gtk_widget_show (button);

  button = gtk_color_button_new ();
  gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, TRUE, 0);
  g_object_bind_property (combo, "active", button, "sensitive", G_BINDING_SYNC_CREATE);
  gtk_widget_show (button);

  hbox = gtk_hbox_new (FALSE, 12);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, TRUE, 0);
  gtk_widget_show (hbox);

  button = gtk_check_button_new_with_mnemonic (_("Ch_ange picture"));
  gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, TRUE, 0);
  gtk_widget_show (button);

  combo = gtk_combo_box_text_new ();
  g_object_bind_property (button, "active", combo, "sensitive", G_BINDING_SYNC_CREATE);
  gtk_box_pack_start (GTK_BOX (hbox), combo, FALSE, TRUE, 0);
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("During login"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Every 10 seconds"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Every minute"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Every 5 minutes"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Every 15 minutes"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Every 30 minutes"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Every hour"));
  gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Every day"));
  gtk_widget_show (combo);

  button2 = gtk_check_button_new_with_mnemonic (_("_Random order"));
  g_object_bind_property (button, "active", button2, "sensitive", G_BINDING_SYNC_CREATE);
  gtk_box_pack_start (GTK_BOX (hbox), button2, FALSE, TRUE, 0);
  gtk_widget_show (button2);
}



static void
thunar_desktop_preferences_dialog_finalize (GObject *object)
{
  ThunarDesktopPreferencesDialog *dialog = THUNAR_DESKTOP_PREFERENCES_DIALOG (object);

  g_free (dialog->background_prop);

  (*G_OBJECT_CLASS (thunar_desktop_preferences_dialog_parent_class)->finalize) (object);
}



static void
thunar_desktop_preferences_dialog_realize (GtkWidget *widget)
{
  ThunarDesktopPreferencesDialog *dialog = THUNAR_DESKTOP_PREFERENCES_DIALOG (widget);

  GTK_WIDGET_CLASS (thunar_desktop_preferences_dialog_parent_class)->realize (widget);

  thunar_desktop_preferences_dialog_update (dialog);
}



static void
thunar_desktop_preferences_dialog_screen_changed (GtkWidget *widget,
                                                  GdkScreen *old_screen)
{
  ThunarDesktopPreferencesDialog *dialog = THUNAR_DESKTOP_PREFERENCES_DIALOG (widget);

  thunar_desktop_preferences_dialog_update (dialog);
}



static void
thunar_desktop_preferences_dialog_response (GtkDialog *dialog,
                                            gint       response)
{
  if (G_UNLIKELY (response == GTK_RESPONSE_HELP))
    {
      /* open the preferences section of the user manual */
      xfce_dialog_show_help (GTK_WINDOW (dialog), "thunar",
                             "desktop", NULL);
    }
  else
    {
      /* close the preferences dialog */
      gtk_widget_destroy (GTK_WIDGET (dialog));
    }
}



static void
thunar_desktop_preferences_dialog_update (ThunarDesktopPreferencesDialog *dialog)
{
  gchar       prop[128];
  gchar      *uri;
  ThunarFile *file;
  GFile      *gfile;
  ThunarFile *file_parent;
  GList       fake;

  /* update the base property */
  thunar_desktop_preferences_dialog_background_prop (dialog);

  g_snprintf (prop, sizeof (prop), "%s/uri", dialog->background_prop);
  uri = xfconf_channel_get_string (dialog->settings, prop, DATADIR "/backgrounds/xfce/xfce-blue.jpg");
  gfile = g_file_new_for_commandline_arg (uri);
  g_free (uri);

  /* select the file in the view */
  file = thunar_file_get (gfile, NULL);
  if (file != NULL)
    {
      file_parent = thunar_file_get_parent (file, NULL);
      if (file_parent != NULL)
        {
          /* set view of the directory */
          thunar_navigator_set_current_directory (THUNAR_NAVIGATOR (dialog->view), file_parent);
          gtk_file_chooser_set_current_folder_file (GTK_FILE_CHOOSER (dialog->folder_chooser),
                                                    thunar_file_get_file (file_parent), NULL);
          g_object_unref (file_parent);

          /* create a fake list and select the file */
          fake.prev = fake.next = NULL;
          fake.data = file;
          thunar_component_set_selected_files (THUNAR_COMPONENT (dialog->view), &fake);
        }

      g_object_unref (file);
    }
}



static void
thunar_desktop_preferences_dialog_background_changed (ThunarDesktopPreferencesDialog *dialog)
{
  GList      *files;
  ThunarFile *file;
  gchar      *uri;
  gchar      *prop;

  /* get the selected file */
  files = thunar_component_get_selected_files (THUNAR_COMPONENT (dialog->view));
  _thunar_assert (files == NULL || files->next == NULL);
  file = g_list_nth_data (files, 0);

  if (file != NULL)
    {
      /* save uri */
      uri = thunar_file_dup_uri (file);
      prop = g_strdup_printf ("%s/uri", dialog->background_prop);
      xfconf_channel_set_string (dialog->settings, prop, uri);
      g_free (uri);
      g_free (prop);
    }
}



static void
thunar_desktop_preferences_dialog_background_prop (ThunarDesktopPreferencesDialog *dialog)
{
  GdkScreen *screen;
  gint       monitor_num;
  gchar     *monitor_name;

  g_free (dialog->background_prop);

  screen = gtk_window_get_screen (GTK_WINDOW (dialog));
  monitor_num = gdk_screen_get_monitor_at_window (screen, gtk_widget_get_window (GTK_WIDGET (dialog)));
  monitor_name = gdk_screen_get_monitor_plug_name (screen, monitor_num);

  if (monitor_name == NULL)
    monitor_name = g_strdup_printf ("monitor-%d", monitor_num);

  dialog->background_prop = g_strdup_printf ("/background/screen-%d/%s",
                                             gdk_screen_get_number (screen),
                                             monitor_name);

  g_free (monitor_name);
}



static void
thunar_desktop_preferences_dialog_folder_changed (ThunarDesktopPreferencesDialog *dialog)
{
  GFile      *gfile;
  ThunarFile *current_directory;

  gfile = gtk_file_chooser_get_current_folder_file (GTK_FILE_CHOOSER (dialog->folder_chooser));
  if (G_LIKELY (gfile != NULL))
    {
      current_directory = thunar_file_get (gfile, NULL);
      g_object_unref (gfile);

      thunar_navigator_set_current_directory (THUNAR_NAVIGATOR (dialog->view), current_directory);
      g_object_unref (current_directory);
    }
}



/**
 * thunar_desktop_preferences_dialog_new:
 * @parent : a #GtkWindow or %NULL.
 *
 * Allocates a new #ThunarDesktopPreferencesDialog widget.
 *
 * Return value: the newly allocated #ThunarDesktopPreferencesDialog.
 **/
GtkWidget *
thunar_desktop_preferences_dialog_new (GtkWindow *parent)
{
  return g_object_new (THUNAR_TYPE_DESKTOP_PREFERENCES_DIALOG,
                       "screen", gtk_window_get_screen (parent), NULL);
}

