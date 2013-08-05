/* vi:set et ai sw=2 sts=2 ts=2: */
/*-
 * Copyright (c) 2009-2011 Jannis Pohlmann <jannis@xfce.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gio/gio.h>

#include <thunar/thunar-enum-types.h>
#include <thunar/thunar-gio-extensions.h>
#include <thunar/thunar-tasks.h>
#include <thunar/thunar-private.h>
#include <thunar/thunar-io-scan-directory.h>



typedef struct
{
  ThunarFile *file;
  gchar      *display_name;
}
ThunarTaskRename;



GTask *
thunar_tasks_new (gpointer             source_object,
                  GAsyncReadyCallback  callback,
                  gpointer             callback_data)
{
  GCancellable *cancellable;
  GTask        *task;

  _thunar_return_val_if_fail (source_object == NULL || G_IS_OBJECT (source_object), NULL);

  cancellable = g_cancellable_new ();
  task = g_task_new (source_object, cancellable, callback, callback_data);
  g_object_unref (cancellable);

  return task;
}



void
thunar_tasks_cancel (GTask *task)
{
  GCancellable *cancellable;

  _thunar_return_if_fail (G_IS_TASK (task));

  cancellable = g_task_get_cancellable (task);
  g_cancellable_cancel (cancellable);

  _thunar_assert (g_task_had_error (task));
}



static void
thunar_tasks_list_directory_thread (GTask        *task,
                                    gpointer      source_object,
                                    gpointer      task_data,
                                    GCancellable *cancellable)
{
  GFile  *directory = G_FILE (task_data);
  GList  *file_list;
  GError *err = NULL;

  _thunar_return_if_fail (G_IS_FILE (task_data));

  /* collect directory contents (non-recursively) */
  file_list = thunar_io_scan_directory2 (directory, cancellable,
                                        G_FILE_QUERY_INFO_NONE,
                                        FALSE, FALSE, TRUE, &err);

  /* abort on errors or cancellation */
  if (err != NULL)
    {
      _thunar_assert (file_list == NULL);
      g_task_return_error (task, err);
      return;
    }

  /* we've got the files */
  g_task_return_pointer (task, file_list, NULL);
}



void
thunar_tasks_list_directory (GTask *task,
                             GFile *directory)
{
  _thunar_return_if_fail (G_IS_FILE (directory));
  _thunar_return_if_fail (G_IS_TASK (task));

  g_task_set_task_data (task, g_object_ref (directory), g_object_unref);
  g_task_run_in_thread (task, thunar_tasks_list_directory_thread);
}



static void
thunar_tasks_rename_file_thread (GTask        *task,
                                 gpointer      source_object,
                                 gpointer      task_data,
                                 GCancellable *cancellable)
{
  ThunarTaskRename *data = task_data;
  GError           *err = NULL;

  if (g_task_return_error_if_cancelled (task))
    return;

  if (thunar_file_rename (data->file, data->display_name, cancellable, TRUE, &err))
    g_task_return_pointer (task, NULL, NULL);
  else
    g_task_return_error (task, err);
}



static void
thunar_tasks_rename_file_free (gpointer user_data)
{
  ThunarTaskRename *data = user_data;

  g_object_unref (data->file);
  g_free (data->display_name);
  g_slice_free (ThunarTaskRename, data);
}



void
thunar_tasks_rename_file (GTask       *task,
                          ThunarFile  *file,
                          const gchar *display_name)
{
  ThunarTaskRename *data;

  _thunar_return_if_fail (THUNAR_IS_FILE (file));
  _thunar_return_if_fail (G_IS_TASK (task));

  data = g_slice_new0 (ThunarTaskRename);
  data->file = g_object_ref (file);
  data->display_name = g_strdup (display_name);

  g_task_set_task_data (task, data, thunar_tasks_rename_file_free);
  g_task_run_in_thread (task, thunar_tasks_rename_file_thread);
}
