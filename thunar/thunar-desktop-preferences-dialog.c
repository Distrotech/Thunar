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
#include <thunar/thunar-desktop-background.h>
#include <thunar/thunar-private.h>



enum
{
  PROP_0,
  PROP_CYCLE_TIME,
};



static void     thunar_desktop_preferences_dialog_get_property              (GObject                        *object,
                                                                             guint                           prop_id,
                                                                             GValue                         *value,
                                                                             GParamSpec                     *pspec);
static void     thunar_desktop_preferences_dialog_set_property              (GObject                        *object,
                                                                             guint                           prop_id,
                                                                             const GValue                   *value,
                                                                             GParamSpec                     *pspec);
static void     thunar_desktop_preferences_dialog_finalize                  (GObject                        *object);
static void     thunar_desktop_preferences_dialog_realize                   (GtkWidget                      *widget);
static void     thunar_desktop_preferences_dialog_screen_changed            (GtkWidget                      *widget,
                                                                             GdkScreen                      *old_screen);
static gboolean thunar_desktop_preferences_dialog_configure_event           (GtkWidget                      *widget,
                                                                             GdkEventConfigure              *event);
static void     thunar_desktop_preferences_dialog_response                  (GtkDialog                      *dialog,
                                                                             gint                            response);
static void     thunar_desktop_preferences_dialog_background_changed        (ThunarDesktopPreferencesDialog *dialog);
static void     thunar_desktop_preferences_dialog_background_prop           (ThunarDesktopPreferencesDialog *dialog);
static gboolean thunar_desktop_preferences_dialog_update                    (gpointer                        data);
static void     thunar_desktop_preferences_dialog_folder_changed            (ThunarDesktopPreferencesDialog *dialog);
static void     thunar_desktop_preferences_dialog_style_changed             (ThunarDesktopPreferencesDialog *dialog);
static void     thunar_desktop_preferences_dialog_color_style_changed       (ThunarDesktopPreferencesDialog *dialog);
static void     thunar_desktop_preferences_dialog_color_changed             (GtkWidget                      *color_button,
                                                                             ThunarDesktopPreferencesDialog *dialog);
static void     thunar_desktop_preferences_dialog_cycle_time_changed        (ThunarDesktopPreferencesDialog *dialog);



struct _ThunarDesktopPreferencesDialogClass
{
  XfceTitledDialogClass __parent__;
};

struct _ThunarDesktopPreferencesDialog
{
  XfceTitledDialog __parent__;

  XfconfChannel *settings;

  gchar         *background_prop;

  guint          dialog_update_timeout;

  GtkWidget     *view;
  GtkWidget     *folder_chooser;
  GtkWidget     *image_style;

  GtkWidget     *color_style;
  GtkWidget     *color_start;
  GtkWidget     *color_end;

  GtkWidget     *cycle_time;
  guint          cycle_time_sec;
};

static struct
{
  guint        seconds;
  const gchar *name;
}
default_cycle_times[] =
{
  { 0,      N_("During login")      },
  { 10,     N_("Every 10 seconds")  },
  { 60,     N_("Every minute")      },
  { 300,    N_("Every 5 minutes")   },
  { 900,    N_("Every 15 minutes")  },
  { 1800,   N_("Every 30 minutes")  },
  { 3600,   N_("Every hour")        },
  { 86400,  N_("Every day")         },
};



G_DEFINE_TYPE (ThunarDesktopPreferencesDialog, thunar_desktop_preferences_dialog, XFCE_TYPE_TITLED_DIALOG)



static void
thunar_desktop_preferences_dialog_class_init (ThunarDesktopPreferencesDialogClass *klass)
{
  GtkDialogClass *gtkdialog_class;
  GObjectClass   *gobject_class;
  GtkWidgetClass *gtkwidget_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = thunar_desktop_preferences_dialog_get_property;
  gobject_class->set_property = thunar_desktop_preferences_dialog_set_property;
  gobject_class->finalize = thunar_desktop_preferences_dialog_finalize;

  gtkwidget_class = GTK_WIDGET_CLASS (klass);
  gtkwidget_class->realize = thunar_desktop_preferences_dialog_realize;
  gtkwidget_class->screen_changed = thunar_desktop_preferences_dialog_screen_changed;
  gtkwidget_class->configure_event = thunar_desktop_preferences_dialog_configure_event;

  gtkdialog_class = GTK_DIALOG_CLASS (klass);
  gtkdialog_class->response = thunar_desktop_preferences_dialog_response;

  g_object_class_install_property (gobject_class,
                                   PROP_CYCLE_TIME,
                                   g_param_spec_uint ("cycle-time",
                                                      NULL, NULL,
                                                      0, 86400,
                                                      DEFAULT_BACKGROUND_CYCLE_TIME,
                                                      EXO_PARAM_READWRITE));
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
  GtkWidget    *seperator;

  dialog->settings = xfconf_channel_get ("thunar-desktop");

  /* configure the dialog properties */
  gtk_window_set_icon_name (GTK_WINDOW (dialog), "preferences-desktop-wallpaper");
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

  dialog->image_style = combo = gtk_combo_box_text_new ();
  gtk_box_pack_start (GTK_BOX (hbox), combo, FALSE, TRUE, 0);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo);
  gtk_widget_show (combo);

  klass = g_type_class_ref (THUNAR_TYPE_BACKGROUND_STYLE);
  for (n = 0; n < klass->n_values; ++n)
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _(klass->values[n].value_nick));
  g_type_class_unref (klass);

  g_signal_connect_swapped (G_OBJECT (combo), "changed",
      G_CALLBACK (thunar_desktop_preferences_dialog_style_changed), dialog);

  hbox = gtk_hbox_new (FALSE, 12);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, TRUE, 0);
  gtk_widget_show (hbox);

  label = gtk_label_new_with_mnemonic ("C_olors:");
  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, TRUE, 0);
  gtk_size_group_add_widget (size_group, label);
  gtk_widget_show (label);

  g_object_unref (size_group);

  dialog->color_style = combo = gtk_combo_box_text_new ();
  gtk_box_pack_start (GTK_BOX (hbox), combo, FALSE, TRUE, 0);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo);
  gtk_widget_show (combo);

  klass = g_type_class_ref (THUNAR_TYPE_BACKGROUND_COLOR_STYLE);
  for (n = 0; n < klass->n_values; ++n)
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _(klass->values[n].value_nick));
  g_type_class_unref (klass);

  g_signal_connect_swapped (G_OBJECT (combo), "changed",
      G_CALLBACK (thunar_desktop_preferences_dialog_color_style_changed), dialog);

  dialog->color_start = button = gtk_color_button_new ();
  gtk_color_button_set_title (GTK_COLOR_BUTTON (button), _("Start background color"));
  gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, TRUE, 0);
  g_signal_connect (G_OBJECT (button), "color-set",
      G_CALLBACK (thunar_desktop_preferences_dialog_color_changed), dialog);
  gtk_widget_show (button);

  dialog->color_end = button = gtk_color_button_new ();
  gtk_color_button_set_title (GTK_COLOR_BUTTON (button), _("End background color"));
  gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, TRUE, 0);
  g_object_bind_property (combo, "active", button, "sensitive", G_BINDING_SYNC_CREATE);
  g_signal_connect (G_OBJECT (button), "color-set",
      G_CALLBACK (thunar_desktop_preferences_dialog_color_changed), dialog);
  gtk_widget_show (button);

  seperator = gtk_hseparator_new ();
  gtk_box_pack_start (GTK_BOX (vbox), seperator, FALSE, TRUE, 0);
  gtk_widget_show (seperator);

  hbox = gtk_hbox_new (FALSE, 12);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, TRUE, 0);
  gtk_widget_show (hbox);

  button = gtk_check_button_new_with_mnemonic (_("Ch_ange background(s)"));
  gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, TRUE, 0);
  xfconf_g_property_bind (dialog->settings, "/background/cycle/enabled",
                          G_TYPE_BOOLEAN, button, "active");
  gtk_widget_show (button);

  combo = dialog->cycle_time = gtk_combo_box_text_new ();
  g_object_bind_property (button, "active", combo, "sensitive", G_BINDING_SYNC_CREATE);
  gtk_box_pack_start (GTK_BOX (hbox), combo, FALSE, TRUE, 0);
  for (n = 0; n < G_N_ELEMENTS (default_cycle_times); ++n)
    {
      gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _(default_cycle_times[n].name));
      if (default_cycle_times[n].seconds == DEFAULT_BACKGROUND_CYCLE_TIME)
        gtk_combo_box_set_active (GTK_COMBO_BOX (combo), n);
    }
  g_signal_connect_swapped (G_OBJECT (combo), "changed",
      G_CALLBACK (thunar_desktop_preferences_dialog_cycle_time_changed), dialog);
  gtk_widget_show (combo);

  xfconf_g_property_bind (dialog->settings, "/background/cycle/time",
                          G_TYPE_UINT, dialog, "cycle-time");

  button2 = gtk_check_button_new_with_mnemonic (_("_Random order"));
  g_object_bind_property (button, "active", button2, "sensitive", G_BINDING_SYNC_CREATE);
  gtk_box_pack_start (GTK_BOX (hbox), button2, FALSE, TRUE, 0);
  xfconf_g_property_bind (dialog->settings, "/background/cycle/random",
                          G_TYPE_BOOLEAN, button2, "active");
  gtk_widget_show (button2);
}



static void
thunar_desktop_preferences_dialog_get_property (GObject    *object,
                                                guint       prop_id,
                                                GValue     *value,
                                                GParamSpec *pspec)
{
  ThunarDesktopPreferencesDialog *dialog = THUNAR_DESKTOP_PREFERENCES_DIALOG (object);

  switch (prop_id)
    {
    case PROP_CYCLE_TIME:
      g_value_set_uint (value, dialog->cycle_time_sec);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static void
thunar_desktop_preferences_dialog_set_property (GObject      *object,
                                                guint         prop_id,
                                                const GValue *value,
                                                GParamSpec   *pspec)
{
  ThunarDesktopPreferencesDialog *dialog = THUNAR_DESKTOP_PREFERENCES_DIALOG (object);
  guint                           n;
  guint                           cycle_time;

  switch (prop_id)
    {
    case PROP_CYCLE_TIME:
      /* find closest item in list */
      cycle_time = g_value_get_uint (value);
      for (n = 0; n < G_N_ELEMENTS (default_cycle_times); n++)
        if (default_cycle_times[n].seconds >= cycle_time)
          break;

       /* update the widget */
       g_signal_handlers_block_by_func (dialog->cycle_time,
           thunar_desktop_preferences_dialog_cycle_time_changed, dialog);
       gtk_combo_box_set_active (GTK_COMBO_BOX (dialog->cycle_time), n);
       g_signal_handlers_unblock_by_func (dialog->cycle_time,
           thunar_desktop_preferences_dialog_cycle_time_changed, dialog);

       /* store last value */
       dialog->cycle_time_sec = default_cycle_times[n].seconds;
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static void
thunar_desktop_preferences_dialog_finalize (GObject *object)
{
  ThunarDesktopPreferencesDialog *dialog = THUNAR_DESKTOP_PREFERENCES_DIALOG (object);

  if (dialog->dialog_update_timeout != 0)
    g_source_remove (dialog->dialog_update_timeout);

  g_free (dialog->background_prop);

  (*G_OBJECT_CLASS (thunar_desktop_preferences_dialog_parent_class)->finalize) (object);
}



static void
thunar_desktop_preferences_dialog_realize (GtkWidget *widget)
{
  ThunarDesktopPreferencesDialog *dialog = THUNAR_DESKTOP_PREFERENCES_DIALOG (widget);

  GTK_WIDGET_CLASS (thunar_desktop_preferences_dialog_parent_class)->realize (widget);

  if (dialog->dialog_update_timeout == 0)
    {
      /* make sure the dialog hold data */
      dialog->dialog_update_timeout =
          g_idle_add (thunar_desktop_preferences_dialog_update, dialog);
    }
}



static void
thunar_desktop_preferences_dialog_screen_changed (GtkWidget *widget,
                                                  GdkScreen *old_screen)
{
  ThunarDesktopPreferencesDialog *dialog = THUNAR_DESKTOP_PREFERENCES_DIALOG (widget);

  if (dialog->dialog_update_timeout == 0)
    {
      dialog->dialog_update_timeout =
          g_idle_add (thunar_desktop_preferences_dialog_update, dialog);
    }
}



static gboolean
thunar_desktop_preferences_dialog_configure_event (GtkWidget         *widget,
                                                   GdkEventConfigure *event)
{
  ThunarDesktopPreferencesDialog *dialog = THUNAR_DESKTOP_PREFERENCES_DIALOG (widget);
  static gint                     old_x = G_MAXINT;
  static gint                     old_y = G_MAXINT;

  /* quick check if the window moved */
  if (old_x == G_MAXINT && old_y == G_MAXINT)
    {
      /* we're about the get the initial position */
      if (dialog->dialog_update_timeout == 0)
        {
          dialog->dialog_update_timeout =
            g_idle_add (thunar_desktop_preferences_dialog_update, dialog);
        }
    }
  else if (old_x != event->x || old_y != event->y)
    {
      /* reschedule update */
      if (dialog->dialog_update_timeout != 0)
        g_source_remove (dialog->dialog_update_timeout);

      dialog->dialog_update_timeout =
        g_timeout_add (250, thunar_desktop_preferences_dialog_update, dialog);
    }

  /* update coordinates */
  old_x = event->x;
  old_y = event->y;

  /* let Gtk+ handle the configure event */
  return (*GTK_WIDGET_CLASS (thunar_desktop_preferences_dialog_parent_class)->configure_event) (widget, event);
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



static gboolean
thunar_desktop_preferences_dialog_update (gpointer data)
{
  ThunarDesktopPreferencesDialog *dialog = THUNAR_DESKTOP_PREFERENCES_DIALOG (data);
  gchar                           prop[128];
  gchar                          *uri;
  ThunarFile                     *file;
  GFile                          *gfile;
  ThunarFile                     *file_parent;
  GList                           fake;
  guint                           style;
  GdkColor                        color;

  /* for timeout */
  dialog->dialog_update_timeout = 0;

  /* update the base property */
  thunar_desktop_preferences_dialog_background_prop (dialog);

  /* background image */
  g_snprintf (prop, sizeof (prop), "%s/uri", dialog->background_prop);
  uri = xfconf_channel_get_string (dialog->settings, prop, DEFAULT_BACKGROUND_URI);
  gfile = g_file_new_for_uri (uri);
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

  /* image style */
  g_snprintf (prop, sizeof (prop), "%s/style", dialog->background_prop);
  style = thunar_desktop_background_settings_enum (dialog->settings,
                                                   THUNAR_TYPE_BACKGROUND_STYLE,
                                                   prop, DEFAULT_BACKGROUND_STYLE);
  gtk_combo_box_set_active (GTK_COMBO_BOX (dialog->image_style), style);

  /* color style */
  g_snprintf (prop, sizeof (prop), "%s/color-style", dialog->background_prop);
  style = thunar_desktop_background_settings_enum (dialog->settings,
                                                   THUNAR_TYPE_BACKGROUND_COLOR_STYLE,
                                                   prop, DEFAULT_BACKGROUND_COLOR_STYLE);
  gtk_combo_box_set_active (GTK_COMBO_BOX (dialog->color_style), style);

  /* colors */
  g_snprintf (prop, sizeof (prop), "%s/color-start", dialog->background_prop);
  thunar_desktop_background_settings_color (dialog->settings, prop, &color,
                                            DEFAULT_BACKGROUND_COLOR_START);
  gtk_color_button_set_color (GTK_COLOR_BUTTON (dialog->color_start), &color);

  g_snprintf (prop, sizeof (prop), "%s/color-end", dialog->background_prop);
  thunar_desktop_background_settings_color (dialog->settings, prop, &color,
                                            DEFAULT_BACKGROUND_COLOR_END);
  gtk_color_button_set_color (GTK_COLOR_BUTTON (dialog->color_end), &color);

  return FALSE;
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
      prop = g_strconcat (dialog->background_prop, "/uri", NULL);
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
  gchar     *nice_name;
  gchar     *title;

  g_free (dialog->background_prop);

  screen = gtk_window_get_screen (GTK_WINDOW (dialog));
  monitor_num = gdk_screen_get_monitor_at_window (screen, gtk_widget_get_window (GTK_WIDGET (dialog)));
  monitor_name = gdk_screen_get_monitor_plug_name (screen, monitor_num);

  if (gdk_display_get_n_screens (gdk_screen_get_display (screen)) > 1)
    {
      /* I18N: multiple screen setup, use this as part of the title */
      nice_name = g_strdup_printf (_("Screen %d"), gdk_screen_get_number (screen));
    }
  else
    {
      if (monitor_name == NULL)
        {
          /* I18N: no output name for title, use number */
          nice_name = g_strdup_printf (_("Monitor %d"), monitor_num);
        }
      else
        {
          nice_name = g_strdup (monitor_name);
        }
    }

  /* I18N: last part is the screen or monitor name */
  title = g_strdup_printf (_("Desktop Preferences - %s"), nice_name);
  gtk_window_set_title (GTK_WINDOW (dialog), title);
  g_free (title);

  /* make sure there is a technical name */
  if (monitor_name == NULL)
    monitor_name = g_strdup_printf ("monitor-%d", monitor_num);

  /* xfconf base property for settings in this dialog */
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



static void
thunar_desktop_preferences_dialog_style_changed (ThunarDesktopPreferencesDialog *dialog)
{
  guint       active;
  GEnumClass *klass;
  gchar      *prop;

  active = gtk_combo_box_get_active (GTK_COMBO_BOX (dialog->image_style));

  prop = g_strconcat (dialog->background_prop, "/style", NULL);
  klass = g_type_class_ref (THUNAR_TYPE_BACKGROUND_STYLE);
  xfconf_channel_set_string (dialog->settings, prop, klass->values[active].value_name);
  g_type_class_unref (klass);
  g_free (prop);
}



static void
thunar_desktop_preferences_dialog_color_style_changed (ThunarDesktopPreferencesDialog *dialog)
{
  guint       active;
  GEnumClass *klass;
  gchar      *prop;

  active = gtk_combo_box_get_active (GTK_COMBO_BOX (dialog->color_style));

  prop = g_strconcat (dialog->background_prop, "/color-style", NULL);
  klass = g_type_class_ref (THUNAR_TYPE_BACKGROUND_COLOR_STYLE);
  xfconf_channel_set_string (dialog->settings, prop, klass->values[active].value_name);
  g_type_class_unref (klass);
  g_free (prop);
}



static void
thunar_desktop_preferences_dialog_color_changed (GtkWidget                      *button,
                                                 ThunarDesktopPreferencesDialog *dialog)
{
  GdkColor     color;
  gchar       *prop;
  gchar       *color_str;
  const gchar *name = button == dialog->color_start ? "start" : "end";

  gtk_color_button_get_color (GTK_COLOR_BUTTON (button), &color);
  color_str = gdk_color_to_string (&color);

  prop = g_strconcat (dialog->background_prop, "/color-", name, NULL);
  xfconf_channel_set_string (dialog->settings, prop, color_str);
  g_free (prop);
  g_free (color_str);
}



static void
thunar_desktop_preferences_dialog_cycle_time_changed (ThunarDesktopPreferencesDialog *dialog)
{
  gint active;

  active = CLAMP (gtk_combo_box_get_active (GTK_COMBO_BOX (dialog->cycle_time)),
                  0, (gint) G_N_ELEMENTS (default_cycle_times));
  dialog->cycle_time_sec = default_cycle_times[active].seconds;

  /* save xfconf setting */
  g_object_notify (G_OBJECT (dialog), "cycle-time");
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

