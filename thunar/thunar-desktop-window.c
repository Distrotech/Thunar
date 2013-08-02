/*-
 * Copyright (c) 2006 Benedikt Meurer <benny@xfce.org>
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

#include <libxfce4util/libxfce4util.h>

#include <thunar/thunar-private.h>
#include <thunar/thunar-desktop-window.h>
#include <thunar/thunar-desktop-background.h>
#include <thunar/thunar-application.h>

#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif



static void     thunar_desktop_window_size_request              (GtkWidget                *widget,
                                                                 GtkRequisition           *requisition);
static void     thunar_desktop_window_realize                   (GtkWidget                *widget);
static void     thunar_desktop_window_unrealize                 (GtkWidget                *widget);
static void     thunar_desktop_window_style_set                 (GtkWidget                *widget,
                                                                 GtkStyle                 *old_style);
static gboolean thunar_desktop_window_expose_event              (GtkWidget                *widget,
                                                                 GdkEventExpose           *event);
static gboolean thunar_desktop_window_button_press_event        (GtkWidget                *widget,
                                                                 GdkEventButton           *event);
static gboolean thunar_desktop_window_scroll_event              (GtkWidget                *widget,
                                                                 GdkEventScroll           *event);
static gboolean thunar_desktop_window_delete_event              (GtkWidget                *widget,
                                                                 GdkEventAny              *event);



struct _ThunarDesktopWindowClass
{
  GtkWindowClass __parent__;
};

struct _ThunarDesktopWindow
{
  GtkWindow __parent__;

  ThunarDesktopBackground *background;
};



G_DEFINE_TYPE (ThunarDesktopWindow, thunar_desktop_window, GTK_TYPE_WINDOW)



static void
thunar_desktop_window_class_init (ThunarDesktopWindowClass *klass)
{
  GtkWidgetClass *gtkwidget_class;

  gtkwidget_class = GTK_WIDGET_CLASS (klass);
  gtkwidget_class->size_request = thunar_desktop_window_size_request;
  gtkwidget_class->realize = thunar_desktop_window_realize;
  gtkwidget_class->unrealize = thunar_desktop_window_unrealize;
  gtkwidget_class->style_set = thunar_desktop_window_style_set;
  gtkwidget_class->expose_event = thunar_desktop_window_expose_event;
  gtkwidget_class->button_press_event = thunar_desktop_window_button_press_event;
  gtkwidget_class->scroll_event = thunar_desktop_window_scroll_event;
  gtkwidget_class->delete_event = thunar_desktop_window_delete_event;
}



static void
thunar_desktop_window_init (ThunarDesktopWindow *window)
{
  gtk_widget_add_events (GTK_WIDGET (window),
                         GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);

  gtk_window_move (GTK_WINDOW (window), 0, 0);
  gtk_window_set_type_hint (GTK_WINDOW (window), GDK_WINDOW_TYPE_HINT_DESKTOP);
  gtk_widget_set_double_buffered (GTK_WIDGET (window), FALSE);
  gtk_window_set_accept_focus (GTK_WINDOW (window), FALSE);
  gtk_window_set_title (GTK_WINDOW (window), _("Desktop"));
}



static void
thunar_desktop_window_size_request (GtkWidget      *widget,
                                    GtkRequisition *requisition)
{
  GdkScreen *screen = gtk_widget_get_screen (widget);

  requisition->width = gdk_screen_get_width (screen);
  requisition->height = gdk_screen_get_height (screen);
}



static void
thunar_desktop_window_screen_changed (GdkScreen           *screen,
                                      ThunarDesktopWindow *window)
{
  GdkWindow *gdk_window;

  /* release background */
  if (window->background != NULL)
    {
      g_object_unref (G_OBJECT (window->background));
      window->background = NULL;
    }

  /* allocate bg and set it on the window */
  gdk_window = gtk_widget_get_window (GTK_WIDGET (window));
  window->background = thunar_desktop_background_new (gdk_window);
}



static void
thunar_desktop_window_realize (GtkWidget *widget)
{
  GdkScreen *screen;
  GdkWindow *root;
  Window     xid;

  GTK_WIDGET_CLASS (thunar_desktop_window_parent_class)->realize (widget);

  screen = gtk_widget_get_screen (widget);
  root = gdk_screen_get_root_window (screen);

  /* tell the root window that we have a new "desktop" window */
  xid = GDK_DRAWABLE_XID (gtk_widget_get_window (widget));
  gdk_property_change (root,
                       gdk_atom_intern ("NAUTILUS_DESKTOP_WINDOW_ID", FALSE),
                       gdk_atom_intern ("WINDOW", FALSE), 32,
                       GDK_PROP_MODE_REPLACE, (gpointer) &xid, 1);
  gdk_property_change (root,
                       gdk_atom_intern ("XFCE_DESKTOP_WINDOW", FALSE),
                       gdk_atom_intern ("WINDOW", FALSE), 32,
                       GDK_PROP_MODE_REPLACE, (gpointer) &xid, 1);

  /* watch screen changes */
  g_signal_connect_swapped (G_OBJECT (screen), "size-changed",
    G_CALLBACK (thunar_desktop_window_screen_changed), widget);
  g_signal_connect_swapped (G_OBJECT (screen), "monitors-changed",
    G_CALLBACK (thunar_desktop_window_screen_changed), widget);

  /* prepare bg */
  thunar_desktop_window_screen_changed (screen, THUNAR_DESKTOP_WINDOW (widget));
}



static void
thunar_desktop_window_unrealize (GtkWidget *widget)
{
  ThunarDesktopWindow *window = THUNAR_DESKTOP_WINDOW (widget);
  GdkScreen           *screen;
  GdkWindow           *root;

  /* drop background */
  if (window->background != NULL)
    {
      g_object_unref (G_OBJECT (window->background));
      window->background = NULL;
    }
  gdk_window_set_back_pixmap (gtk_widget_get_window (widget), NULL, FALSE);

  /* disconnect the XRandR support handler */
  screen = gtk_widget_get_screen (widget);
  g_signal_handlers_disconnect_by_func (G_OBJECT (screen),
      thunar_desktop_window_screen_changed, widget);

  /* unset root window properties */
  root = gdk_screen_get_root_window (screen);
  gdk_property_delete (root, gdk_atom_intern ("NAUTILUS_DESKTOP_WINDOW_ID", FALSE));
  gdk_property_delete (root, gdk_atom_intern ("XFCE_DESKTOP_WINDOW", FALSE));

  GTK_WIDGET_CLASS (thunar_desktop_window_parent_class)->unrealize (widget);
}



static void
thunar_desktop_window_style_set (GtkWidget *widget,
                                 GtkStyle  *old_style)
{
  if (old_style != NULL)
    {
      /* redraw to apply on top of new style */
      gtk_widget_queue_draw (widget);
    }
}



static gboolean
thunar_desktop_window_expose_event (GtkWidget      *widget,
                                    GdkEventExpose *event)
{
  /* leave on multiple events */
  if (event->count != 0)
    return FALSE;

  /* paint background */
  gdk_window_clear_area (gtk_widget_get_window (widget),
                         event->area.x, event->area.y,
                         event->area.width, event->area.height);

  /* TODO propagate children */

  return FALSE;
}



static gboolean
thunar_desktop_window_button_press_event (GtkWidget      *widget,
                                          GdkEventButton *event)
{
  //ThunarDesktopWindow *window = THUNAR_DESKTOP_WINDOW (widget);

  if (event->type == GDK_BUTTON_PRESS)
    {
      if (event->button == 3
          || (event->button == 1 && (event->state & GDK_SHIFT_MASK) != 0))
        {

        }
    }

  return FALSE;
}



static gboolean
thunar_desktop_window_scroll_event (GtkWidget      *widget,
                                    GdkEventScroll *event)
{
#ifdef GDK_WINDOWING_X11
  XButtonEvent  xev;
  gint          button;
  GdkWindow    *root_window;

  root_window = gdk_screen_get_root_window (gtk_widget_get_screen (widget));

  /* create an event for the root window */
  xev.window = GDK_WINDOW_XWINDOW (root_window);
  xev.button = event->direction + 4;
  xev.x = event->x; /* needed for icewm */
  xev.y = event->y;
  xev.x_root = event->x_root;
  xev.y_root = event->y_root;
  xev.state = event->state;
  xev.root =  xev.window;
  xev.subwindow = None;
  xev.time = event->time;
  xev.same_screen = True;

  /* send a button press and release */
  for (button = ButtonPress; button < ButtonRelease; button++)
    {
      xev.type = button;

      /* forward event to root window */
      XSendEvent (GDK_WINDOW_XDISPLAY (root_window),
                  xev.window, False,
                  ButtonPressMask | ButtonReleaseMask,
                  (XEvent *) &xev);
    }
#endif

  return TRUE;
}



static gboolean
thunar_desktop_window_delete_event (GtkWidget   *widget,
                                    GdkEventAny *event)
{
  /* TODO: hoopup session logout handler here */

  /* never close the window */
  return TRUE;
}



/**
 * thunar_desktop_window_show_all:
 *
 * Create a new window for each screen.
 **/
void
thunar_desktop_window_show_all (void)
{
  GdkDisplay        *display = gdk_display_get_default ();
  gint               n_screens, n;
  GdkScreen         *screen;
  GtkWidget         *desktop;
  ThunarApplication *application;
  gboolean           managing_desktop;

  /* check if there are already desktop windows */
  application = thunar_application_get ();
  managing_desktop = thunar_application_has_desktop_windows (application);
  g_object_unref (application);

  if (!managing_desktop)
    {
      n_screens = gdk_display_get_n_screens (display);
      for (n = 0; n < n_screens; n++)
        {
          screen = gdk_display_get_screen (display, n);
          desktop = thunar_desktop_window_new_with_screen (screen);
          gtk_widget_show (desktop);
        }
    }
}



/**
 * thunar_desktop_window_new_with_screen:
 * @screen : a #GdkScreen.
 *
 * Allocates a new #ThunarDesktopWindow instance and
 * associates it with the given @screen.
 *
 * Return value: the newly allocated #ThunarDesktopWindow.
 **/
GtkWidget*
thunar_desktop_window_new_with_screen (GdkScreen *screen)
{
  GtkWidget         *desktop;
  ThunarApplication *application;

  g_return_val_if_fail (GDK_IS_SCREEN (screen), NULL);
  desktop = g_object_new (THUNAR_TYPE_DESKTOP_WINDOW, "screen", screen, NULL);

  application = thunar_application_get ();
  thunar_application_take_window (application, GTK_WINDOW (desktop));
  g_object_unref (application);

  return desktop;
}
