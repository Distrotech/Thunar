/* vi:set sw=2 sts=2 ts=2 et ai: */
/*-
 * Copyright (c) 2009      Jannis Pohlmann <jannis@xfce.org>
 * Copyright (c) 2012-2013 Nick Schermer <nick@xfce.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include <thunar/thunar-deep-count-job.h>
#include <thunar/thunar-marshal.h>
#include <thunar/thunar-util.h>
#include <thunar/thunar-gio-extensions.h>
#include <thunar/thunar-file.h>
#include <thunar/thunar-tasks.h>
#include <thunar/thunar-private.h>



/* Signal identifiers */
enum
{
  STATUS_UPDATE,
  LAST_SIGNAL,
};



#define DEEP_COUNT_FILE_INFO_NAMESPACE \
  G_FILE_ATTRIBUTE_STANDARD_TYPE "," \
  G_FILE_ATTRIBUTE_STANDARD_SIZE "," \
  G_FILE_ATTRIBUTE_ID_FILESYSTEM



static void     thunar_deep_count_job_finalize   (GObject                 *object);



struct _ThunarDeepCountJobClass
{
  GObjectClass __parent__;

  /* signals */
  void (*status_update) (ThunarDeepCountJob *job,
                         guint64             total_size,
                         guint               file_count,
                         guint               directory_count,
                         guint               unreadable_directory_count);
};

struct _ThunarDeepCountJob
{
  GObject __parent__;

  GTask              *task;

  GList              *files;
  GFileQueryInfoFlags query_flags;

  /* the time of the last "status-update" emission */
  gint64              last_time;

  /* status information */
  guint64             total_size;
  guint               file_count;
  guint               directory_count;
  guint               unreadable_directory_count;
};



static guint deep_count_signals[LAST_SIGNAL];



G_DEFINE_TYPE (ThunarDeepCountJob, thunar_deep_count_job, G_TYPE_OBJECT)



static void
thunar_deep_count_job_class_init (ThunarDeepCountJobClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = thunar_deep_count_job_finalize;

  /**
   * ThunarDeepCountJob::status-update:
   * @job                        : a #ThunarJob.
   * @total_size                 : the total size in bytes.
   * @file_count                 : the number of files.
   * @directory_count            : the number of directories.
   * @unreadable_directory_count : the number of unreadable directories.
   *
   * Emitted by the @job to inform listeners about the number of files,
   * directories and bytes counted so far.
   **/
  deep_count_signals[STATUS_UPDATE] =
    g_signal_new (I_("status-update"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_NO_HOOKS,
                  G_STRUCT_OFFSET (ThunarDeepCountJobClass, status_update),
                  NULL, NULL,
                  _thunar_marshal_VOID__UINT64_UINT_UINT_UINT,
                  G_TYPE_NONE, 4,
                  G_TYPE_UINT64,
                  G_TYPE_UINT,
                  G_TYPE_UINT,
                  G_TYPE_UINT);
}



static void
thunar_deep_count_job_init (ThunarDeepCountJob *job)
{
  job->query_flags = G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS;
}



static void
thunar_deep_count_job_finalize (GObject *object)
{
  ThunarDeepCountJob *job = THUNAR_DEEP_COUNT_JOB (object);

  thunar_g_file_list_free (job->files);

  g_object_unref (job->task);

  (*G_OBJECT_CLASS (thunar_deep_count_job_parent_class)->finalize) (object);
}



static gboolean
thunar_deep_count_job_status_update_main (gpointer user_data)
{
  ThunarDeepCountJob *job = user_data;

  _thunar_return_val_if_fail (THUNAR_IS_DEEP_COUNT_JOB (job), FALSE);

  if (!g_task_had_error (job->task))
    {
      g_signal_emit (job,
                     deep_count_signals[STATUS_UPDATE],
                     0,
                     job->total_size,
                     job->file_count,
                     job->directory_count,
                     job->unreadable_directory_count);
    }

  return FALSE;
}



static void
thunar_deep_count_job_status_update (ThunarDeepCountJob *job)
{
  /* emit in the mainloop */
  if (THUNAR_IS_DEEP_COUNT_JOB (job)
      && !g_task_had_error (job->task))
    {
      g_main_context_invoke_full (NULL, G_PRIORITY_DEFAULT,
                                  thunar_deep_count_job_status_update_main,
                                  g_object_ref (job), g_object_unref);
    }
}



static gboolean
thunar_deep_count_job_process (ThunarDeepCountJob  *job,
                               GFile               *file,
                               GFileInfo           *file_info,
                               const gchar         *toplevel_fs_id,
                               GError             **error)
{
  GFileEnumerator *enumerator;
  GFileInfo       *child_info;
  GFileInfo       *info;
  gboolean         success = TRUE;
  GFile           *child;
  gint64           real_time;
  const gchar     *fs_id;
  gboolean         toplevel_file = (toplevel_fs_id == NULL);
  GCancellable    *cancellable;

  _thunar_return_val_if_fail (G_IS_FILE (file), FALSE);
  _thunar_return_val_if_fail (G_IS_TASK (job->task), FALSE);
  _thunar_return_val_if_fail (file_info == NULL || G_IS_FILE_INFO (file_info), FALSE);
  _thunar_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  cancellable = g_task_get_cancellable (job->task);

  /* abort if job was already cancelled */
  if (g_cancellable_is_cancelled (cancellable))
    return FALSE;

  if (file_info != NULL)
    {
      /* use the enumerator info */
      info = g_object_ref (file_info);
    }
  else
    {
      /* query size and type of the current file */
      info = g_file_query_info (file,
                                DEEP_COUNT_FILE_INFO_NAMESPACE,
                                job->query_flags,
                                cancellable,
                                error);
    }

  /* abort on invalid info or cancellation */
  if (info == NULL)
    return FALSE;

  /* abort on cancellation */
  if (g_cancellable_is_cancelled (cancellable))
    {
      g_object_unref (info);
      return FALSE;
    }

  /* only check files on the same filesystem so no remote mounts or
   * dummy filesystems are counted */
  fs_id = g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_ID_FILESYSTEM);
  if (fs_id == NULL)
    fs_id = "";

  if (toplevel_fs_id == NULL)
    {
      /* first toplevel, so use this id */
      toplevel_fs_id = fs_id;
    }
  else if (strcmp (fs_id, toplevel_fs_id) != 0)
    {
      /* release the file info */
      g_object_unref (info);

      /* other filesystem, continue */
      return TRUE;
    }

  /* recurse if we have a directory */
  if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY)
    {
      /* try to read from the directory */
      enumerator = g_file_enumerate_children (file,
                                              DEEP_COUNT_FILE_INFO_NAMESPACE ","
                                              G_FILE_ATTRIBUTE_STANDARD_NAME,
                                              job->query_flags,
                                              cancellable,
                                              error);

      if (!g_cancellable_is_cancelled (cancellable))
        {
          if (enumerator == NULL)
            {
              /* directory was unreadable */
              job->unreadable_directory_count++;

              if (toplevel_file
                  && g_list_length (job->files) < 2)
                {
                  /* we only bail out if the job file is unreadable */
                  success = FALSE;
                }
              else
                {
                  /* ignore errors from files other than the job file */
                  g_clear_error (error);
                }
            }
          else
            {
              /* directory was readable */
              job->directory_count++;

              while (!g_cancellable_is_cancelled (cancellable))
                {
                  /* query next child info */
                  child_info = g_file_enumerator_next_file (enumerator,
                                                            cancellable,
                                                            error);

                  /* abort on invalid child info (iteration ends) or cancellation */
                  if (child_info == NULL)
                    break;

                  /* generate a GFile for the child */
                  child = g_file_resolve_relative_path (file, g_file_info_get_name (child_info));

                  /* recurse unless the job was cancelled before */
                  thunar_deep_count_job_process (job, child, child_info, toplevel_fs_id, error);

                  /* free resources */
                  g_object_unref (child);
                  g_object_unref (child_info);
                }
            }
        }

      /* destroy the enumerator */
      if (enumerator != NULL)
        g_object_unref (enumerator);

      if (!g_cancellable_is_cancelled (cancellable))
        {
          /* emit status update whenever we've finished a directory,
           * but not more than four times per second */
          real_time = g_get_real_time ();
          if (real_time >= job->last_time)
            {
              if (job->last_time != 0)
                thunar_deep_count_job_status_update (job);
              job->last_time = real_time + (G_USEC_PER_SEC / 4);
            }
        }
    }
  else
    {
      /* we have a regular file or at least not a directory */
      job->file_count++;

      /* add size of the file to the total size */
      job->total_size += g_file_info_get_size (info);
    }

  /* destroy the file info */
  g_object_unref (info);

  /* we've succeeded if there was no error when loading information
   * about the job file itself and the job was not cancelled */
  return !g_cancellable_is_cancelled (cancellable) && success;
}



static void
thunar_deep_count_job_thread (GTask        *task,
                              gpointer      source_object,
                              gpointer      task_data,
                              GCancellable *cancellable)
{
  ThunarDeepCountJob *job = THUNAR_DEEP_COUNT_JOB (task_data);
  gboolean            success = TRUE;
  GError             *err = NULL;
  GList              *lp;
  GFile              *gfile;

  _thunar_return_if_fail (G_IS_TASK (task));
  _thunar_return_if_fail (THUNAR_IS_DEEP_COUNT_JOB (job));
  _thunar_return_if_fail (job->task == task);
  _thunar_return_if_fail (g_task_get_cancellable (job->task) == cancellable);

  /* don't start the job if it was already cancelled */
  if (g_task_return_error_if_cancelled (task))
    return;

  /* reset counters */
  job->total_size = 0;
  job->file_count = 0;
  job->directory_count = 0;
  job->unreadable_directory_count = 0;
  job->last_time = 0;

  /* count files, directories and compute size of the job files */
  for (lp = job->files; lp != NULL; lp = lp->next)
    {
      gfile = thunar_file_get_file (THUNAR_FILE (lp->data));
      success = thunar_deep_count_job_process (job, gfile, NULL, NULL, &err);
      if (G_UNLIKELY (!success))
        break;
    }

  if (!success)
    {
      g_assert (err != NULL || g_cancellable_is_cancelled (cancellable));

      /* set error if the job was cancelled */
      if (!g_task_return_error_if_cancelled (task))
        {
          /* return the error from the process */
          g_task_return_error (task, err);
        }

      return;
    }
  else if (!g_cancellable_is_cancelled (cancellable))
    {
      /* emit final status update at the very end of the computation */
      thunar_deep_count_job_status_update (job);
    }

  /* done */
  g_task_return_pointer (task, NULL, NULL);
}



ThunarDeepCountJob *
thunar_deep_count_job_new (gpointer             source_object,
                           GList               *files,
                           GFileQueryInfoFlags  flags,
                           GAsyncReadyCallback  callback)
{
  ThunarDeepCountJob *job;

  _thunar_return_val_if_fail (G_IS_OBJECT (source_object), NULL);
  _thunar_return_val_if_fail (files != NULL, NULL);

  job = g_object_new (THUNAR_TYPE_DEEP_COUNT_JOB, NULL);
  job->task = thunar_tasks_new (source_object, callback, job);
  job->files = thunar_g_file_list_copy (files);
  job->query_flags = flags;

  /* but lower than folder tasks */
  g_task_set_priority (job->task, G_PRIORITY_DEFAULT_IDLE);

  return job;
}



void
thunar_deep_count_job_run (ThunarDeepCountJob *job)
{
  _thunar_return_if_fail (THUNAR_IS_DEEP_COUNT_JOB (job));
  _thunar_return_if_fail (G_IS_TASK (job->task));

  g_task_set_task_data (job->task, job, NULL);
  g_task_run_in_thread (job->task, thunar_deep_count_job_thread);
}



void
thunar_deep_count_job_cancel (ThunarDeepCountJob *job)
{
  GCancellable *cancellable;

  _thunar_return_if_fail (THUNAR_IS_DEEP_COUNT_JOB (job));

  cancellable = g_task_get_cancellable (job->task);
  g_cancellable_cancel (cancellable);
}
