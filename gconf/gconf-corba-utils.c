/* GConf
 * Copyright (C) 1999, 2000 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#include "gconf-corba-utils.h"
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <locale.h>
#include <time.h>
#include <math.h>

static gchar* daemon_ior = NULL;

void
gconf_set_daemon_ior(const gchar* ior)
{
  if (daemon_ior != NULL)
    {
      g_free(daemon_ior);
      daemon_ior = NULL;
    }
      
  if (ior != NULL)
    daemon_ior = g_strdup(ior);
}

const gchar*
gconf_get_daemon_ior(void)
{
  return daemon_ior;
}

/*
 * Locks
 */

/*
 * Locks works as follows. We have a lock directory to hold the locking
 * mess, and we have an IOR file inside the lock directory with the
 * gconfd IOR, and we have an fcntl() lock on the IOR file. The IOR
 * file is created atomically using a temporary file, then link()
 */

struct _GConfLock {
  gchar *lock_directory;
  gchar *iorfile;
  int    lock_fd;
};

#define lock_entire_file(fd) \
  lock_reg ((fd), F_SETLK, F_WRLCK, 0, SEEK_SET, 0)
#define unlock_entire_file(fd) \
  lock_reg ((fd), F_SETLK, F_UNLCK, 0, SEEK_SET, 0)

/* Your basic Stevens cut-and-paste */
static int
lock_reg (int fd, int cmd, int type, off_t offset, int whence, off_t len)
{
  struct flock lock;

  lock.l_type = type; /* F_RDLCK, F_WRLCK, F_UNLCK */
  lock.l_start = offset; /* byte offset relative to whence */
  lock.l_whence = whence; /* SEEK_SET, SEEK_CUR, SEEK_END */
  lock.l_len = len; /* #bytes, 0 for eof */

  return fcntl (fd, cmd, &lock);
}

static void
set_close_on_exec (int fd)
{
  int val;

  val = fcntl (fd, F_GETFD, 0);
  if (val < 0)
    {
      gconf_log (GCL_DEBUG, "couldn't F_GETFD: %s\n", g_strerror (errno));
      return;
    }

  val |= FD_CLOEXEC;

  if (fcntl (fd, F_SETFD, val) < 0)
    gconf_log (GCL_DEBUG, "couldn't F_SETFD: %s\n", g_strerror (errno));
}

static char*
unique_filename (const char *directory)
{
  char *guid;
  char *uniquefile;
  
  guid = gconf_unique_key ();
  uniquefile = g_strconcat (directory, "/", guid, NULL);
  g_free (guid);

  return uniquefile;
}

static ConfigServer
read_current_server_and_set_warning (const gchar *iorfile,
                                     GString     *warning)
{
  FILE *fp;
  
  fp = fopen (iorfile, "r");
          
  if (fp == NULL)
    {
      if (warning)
        g_string_append_printf (warning,
                                _("IOR file '%s' not opened successfully, no gconfd located: %s"),
                                iorfile, g_strerror (errno));

      return CORBA_OBJECT_NIL;
    }
  else /* successfully opened IOR file */
    {
      char buf[2048] = { '\0' };
      const char *str = NULL;
      
      fgets (buf, sizeof (buf) - 2, fp);
      fclose (fp);

      /* The lockfile format is <pid>:<ior> for gconfd
       * or <pid>:none for gconftool
       */
      str = buf;
      while (isdigit((unsigned char) *str))
        ++str;

      if (*str == ':')
        ++str;
          
      if (str[0] == 'n' &&
          str[1] == 'o' &&
          str[2] == 'n' &&
          str[3] == 'e')
        {
          if (warning)
            g_string_append_printf (warning,
                                    _("gconftool or other non-gconfd process has the lock file '%s'"),
                                    iorfile); 
        }
      else /* file contains daemon IOR */
        {
          CORBA_ORB orb;
          CORBA_Environment ev;
          ConfigServer server;
          
          CORBA_exception_init (&ev);
                  
          orb = gconf_orb_get ();

          if (orb == NULL)
            {
              if (warning)
                g_string_append_printf (warning,
                                        _("couldn't contact ORB to resolve existing gconfd object reference"));
              return CORBA_OBJECT_NIL;
            }
                  
          server = CORBA_ORB_string_to_object (orb, (char*) str, &ev);
          CORBA_exception_free (&ev);

          if (server == CORBA_OBJECT_NIL &&
              warning)
            g_string_append_printf (warning,
                                    _("Failed to convert IOR '%s' to an object reference"),
                                    str);
          
          return server;
        }

      return CORBA_OBJECT_NIL;
    }
}

static ConfigServer
read_current_server (const gchar *iorfile,
                     gboolean     warn_if_fail)
{
  GString *warning;
  ConfigServer server;
  
  if (warn_if_fail)
    warning = g_string_new (NULL);
  else
    warning = NULL;

  server = read_current_server_and_set_warning (iorfile, warning);

  if (warning->len > 0)
    gconf_log (GCL_WARNING, "%s", warning->str);

  g_string_free (warning, TRUE);

  return server;
}

static int
create_new_locked_file (const gchar *directory,
                        const gchar *filename,
                        GError     **err)
{
  int fd;
  char *uniquefile;
  gboolean got_lock;
  
  got_lock = FALSE;
  
  uniquefile = unique_filename (directory);

  fd = open (uniquefile, O_WRONLY | O_CREAT, 0700);

  /* Lock our temporary file, lock hopefully applies to the
   * inode and so also counts once we link it to the new name
   */
  if (lock_entire_file (fd) < 0)
    {
      g_set_error (err,
                   GCONF_ERROR,
                   GCONF_ERROR_LOCK_FAILED,
                   _("Could not lock temporary file '%s': %s"),
                   uniquefile, g_strerror (errno));
      goto out;
    }
  
  /* Create lockfile as a link to unique file */
  if (link (uniquefile, filename) == 0)
    {
      /* filename didn't exist before, and open succeeded, and we have the lock */
      got_lock = TRUE;
      goto out;
    }
  else
    {
      /* see if the link really succeeded */
      struct stat sb;
      if (stat (uniquefile, &sb) == 0 &&
          sb.st_nlink == 2)
        {
          got_lock = TRUE;
          goto out;
        }
      else
        {
          g_set_error (err,
                       GCONF_ERROR,
                       GCONF_ERROR_LOCK_FAILED,
                       _("Could not create file '%s', probably because it already exists"),
                       filename);
          goto out;
        }
    }
  
 out:
  if (got_lock)
    set_close_on_exec (fd);
  
  unlink (uniquefile);
  g_free (uniquefile);

  if (!got_lock)
    {
      if (fd >= 0)
        close (fd);
      fd = -1;
    }
  
  return fd;
}


static int
open_empty_locked_file (const gchar *directory,
                        const gchar *filename,
                        GError     **err)
{
  int fd;

  fd = create_new_locked_file (directory, filename, NULL);

  if (fd >= 0)
    return fd;
  
  /* We failed to create the file, most likely because it already
   * existed; try to get the lock on the existing file, and if we can
   * get that lock, delete it, then start over.
   */
  fd = open (filename, O_RDWR, 0700);
  if (fd < 0)
    {
      /* File has gone away? */
      g_set_error (err,
                   GCONF_ERROR,
                   GCONF_ERROR_LOCK_FAILED,
                   _("Failed to create or open '%s'"),
                   filename);
      return -1;
    }

  if (lock_entire_file (fd) < 0)
    {
      g_set_error (err,
                   GCONF_ERROR,
                   GCONF_ERROR_LOCK_FAILED,
                   _("Failed to lock '%s': probably another process has the lock, or your operating system has NFS file locking misconfigured (%s)"),
                   filename, strerror (errno));
      close (fd);
      return -1;
    }

  /* We have the lock on filename, so delete it */
  /* FIXME this leaves .nfs32423432 cruft */
  unlink (filename);
  close (fd);
  fd = -1;

  /* Now retry creating our file */
  fd = create_new_locked_file (directory, filename, err);
  
  return fd;
}

static void
gconf_lock_destroy (GConfLock* lock)
{
  if (lock->lock_fd >= 0)
    close (lock->lock_fd);
  g_free (lock->iorfile);
  g_free (lock->lock_directory);
  g_free (lock);
}


static gboolean
file_locked_by_someone_else (int fd)
{
  struct flock lock;

  lock.l_type = F_WRLCK;
  lock.l_start = 0;
  lock.l_whence = SEEK_SET;
  lock.l_len = 0;

  if (fcntl (fd, F_GETLK, &lock) < 0)
    return TRUE; /* pretend it's locked */

  if (lock.l_type == F_UNLCK)
    return FALSE; /* we have the lock */
  else
    return TRUE; /* someone else has it */
}

GConfLock*
gconf_get_lock (const gchar *lock_directory,
                GError     **err)
{
  return gconf_get_lock_or_current_holder (lock_directory, NULL, err);
}

gboolean
gconf_release_lock (GConfLock *lock,
                    GError   **err)
{
  gboolean retval;
  char *uniquefile;
  
  retval = FALSE;
  uniquefile = NULL;
  
  /* A paranoia check to avoid disaster if e.g.
   * some random client code opened and closed the
   * lockfile (maybe Nautilus checking its MIME type or
   * something)
   */
  if (lock->lock_fd < 0 ||
      file_locked_by_someone_else (lock->lock_fd))
    {
      g_set_error (err,
                   GCONF_ERROR,
                   GCONF_ERROR_FAILED,
                   _("We didn't have the lock on file `%s', but we should have"),
                   lock->iorfile);
      goto out;
    }

  /* To avoid annoying .nfs3435314513453145 files on unlink, which keep us
   * from removing the lock directory, we don't want to hold the
   * lockfile open after removing all links to it. But we can't
   * close it then unlink, because then we would be unlinking without
   * holding the lock. So, we create a unique filename and link it too
   * the locked file, then unlink the locked file, then drop our locks
   * and close file descriptors, then unlink the unique filename
   */
  
  uniquefile = unique_filename (lock->lock_directory);

  if (link (lock->iorfile, uniquefile) < 0)
    {
      g_set_error (err,
                   GCONF_ERROR,
                   GCONF_ERROR_FAILED,
                   _("Failed to link '%s' to '%s': %s"),
                   uniquefile, lock->iorfile, g_strerror (errno));

      goto out;
    }
  
  /* Note that we unlink while still holding the lock to avoid races */
  if (unlink (lock->iorfile) < 0)
    {
      g_set_error (err,
                   GCONF_ERROR,
                   GCONF_ERROR_FAILED,
                   _("Failed to remove lock file `%s': %s"),
                   lock->iorfile,
                   g_strerror (errno));
      goto out;
    }

  /* Now drop our lock */
  if (lock->lock_fd >= 0)
    {
      close (lock->lock_fd);
      lock->lock_fd = -1;
    }

  /* Now remove the temporary link we used to avoid .nfs351453 garbage */
  if (unlink (uniquefile) < 0)
    {
      g_set_error (err,
                   GCONF_ERROR,
                   GCONF_ERROR_FAILED,
                   _("Failed to clean up file '%s': %s"),
                   uniquefile, g_strerror (errno));

      goto out;
    }

  /* And finally clean up the directory - this would have failed if
   * we had .nfs323423423 junk
   */
  if (rmdir (lock->lock_directory) < 0)
    {
      g_set_error (err,
                   GCONF_ERROR,
                   GCONF_ERROR_FAILED,
                   _("Failed to remove lock directory `%s': %s"),
                   lock->lock_directory,
                   g_strerror (errno));
      goto out;
    }

  retval = TRUE;
  
 out:

  g_free (uniquefile);
  gconf_lock_destroy (lock);
  return retval;
}

GConfLock*
gconf_get_lock_or_current_holder (const gchar  *lock_directory,
                                  ConfigServer *current_server,
                                  GError      **err)
{
  ConfigServer server;
  GConfLock* lock;
  
  g_return_val_if_fail(lock_directory != NULL, NULL);

  if (current_server)
    *current_server = CORBA_OBJECT_NIL;
  
  if (mkdir (lock_directory, 0700) < 0 &&
      errno != EEXIST)
    {
      gconf_set_error (err,
                       GCONF_ERROR_LOCK_FAILED,
                       _("couldn't create directory `%s': %s"),
                       lock_directory, g_strerror (errno));

      return NULL;
    }

  server = CORBA_OBJECT_NIL;
    
  lock = g_new0 (GConfLock, 1);

  lock->lock_directory = g_strdup (lock_directory);

  lock->iorfile = g_strconcat (lock->lock_directory, "/ior", NULL);

  /* Check the current IOR file and ping its daemon */
  
  lock->lock_fd = open_empty_locked_file (lock->lock_directory,
                                          lock->iorfile,
                                          err);
  
  if (lock->lock_fd < 0)
    {
      /* We didn't get the lock. Read the old server, and provide
       * it to the caller. Error is already set.
       */
      if (current_server)
        *current_server = read_current_server (lock->iorfile, TRUE);

      gconf_lock_destroy (lock);
      
      return NULL;
    }
  else
    {
      /* Write IOR to lockfile */
      const gchar* ior;
      int retval;
      gchar* s;
      
      s = g_strdup_printf ("%u:", (guint) getpid ());
        
      retval = write (lock->lock_fd, s, strlen (s));

      g_free (s);
        
      if (retval >= 0)
        {
          ior = gconf_get_daemon_ior();
            
          if (ior == NULL)
            retval = write (lock->lock_fd, "none", 4);
          else
            retval = write (lock->lock_fd, ior, strlen (ior));
        }

      if (retval < 0)
        {
          gconf_set_error (err,
                           GCONF_ERROR_LOCK_FAILED,
                           _("Can't write to file `%s': %s"),
                           lock->iorfile, g_strerror (errno));

          unlink (lock->iorfile);
          gconf_lock_destroy (lock);

          return NULL;
        }
    }

  return lock;
}

/* This function doesn't try to see if the lock is valid or anything
 * of the sort; it just reads it. It does do the object_to_string
 */
ConfigServer
gconf_get_current_lock_holder  (const gchar *lock_directory,
                                GString     *failure_log)
{
  char *iorfile;
  ConfigServer server;

  iorfile = g_strconcat (lock_directory, "/ior", NULL);
  server = read_current_server_and_set_warning (iorfile, failure_log);
  g_free (iorfile);
  return server;
}

void
gconf_daemon_blow_away_locks (void)
{
  char *lock_directory;
  char *iorfile;
   
  lock_directory = gconf_get_lock_dir ();
 
  iorfile = g_strconcat (lock_directory, "/ior", NULL);
 
  if (unlink (iorfile) < 0)
    g_printerr (_("Failed to unlink lock file %s: %s\n"),
                iorfile, g_strerror (errno));
 
  g_free (iorfile);
  g_free (lock_directory);
}

static CORBA_ORB gconf_orb = CORBA_OBJECT_NIL;      

CORBA_ORB
gconf_orb_get (void)
{
  if (gconf_orb == CORBA_OBJECT_NIL)
    {
      CORBA_Environment ev;
      int argc = 1;
      char *argv[] = { "gconf", NULL };

      _gconf_init_i18n ();
      
      CORBA_exception_init (&ev);
      
      gconf_orb = CORBA_ORB_init (&argc, argv, "orbit-local-orb", &ev);
      g_assert (ev._major == CORBA_NO_EXCEPTION);

      CORBA_exception_free (&ev);
    }

  return gconf_orb;
}

int
gconf_orb_release (void)
{
  int ret = 0;

  if (gconf_orb != CORBA_OBJECT_NIL)
    {
      CORBA_ORB orb = gconf_orb;
      CORBA_Environment ev;

      gconf_orb = CORBA_OBJECT_NIL;

      CORBA_exception_init (&ev);

      CORBA_ORB_destroy (orb, &ev);
      CORBA_Object_release ((CORBA_Object)orb, &ev);

      if (ev._major != CORBA_NO_EXCEPTION)
        {
          ret = 1;
        }
      CORBA_exception_free (&ev);
    }

  return ret;
}

gchar*
gconf_object_to_string (CORBA_Object obj,
                        GError **err)
{
  CORBA_Environment ev;
  gchar *ior;
  gchar *retval;
  
  CORBA_exception_init (&ev);

  ior = CORBA_ORB_object_to_string (gconf_orb_get (), obj, &ev);

  if (ior == NULL)
    {
      gconf_set_error (err,
                       GCONF_ERROR_FAILED,
                       _("Failed to convert object to IOR"));

      return NULL;
    }

  retval = g_strdup (ior);

  CORBA_free (ior);

  return retval;
}

gboolean
gconf_CORBA_Object_equal (gconstpointer a, gconstpointer b)
{
  CORBA_Environment ev;
  CORBA_Object _obj_a = (gpointer)a;
  CORBA_Object _obj_b = (gpointer)b;
  gboolean retval;

  CORBA_exception_init (&ev);
  retval = CORBA_Object_is_equivalent(_obj_a, _obj_b, &ev);
  CORBA_exception_free (&ev);

  return retval;
}

guint
gconf_CORBA_Object_hash (gconstpointer key)
{
  CORBA_Environment ev;
  CORBA_Object _obj = (gpointer)key;
  CORBA_unsigned_long retval;

  CORBA_exception_init (&ev);
  retval = CORBA_Object_hash(_obj, G_MAXUINT, &ev);
  CORBA_exception_free (&ev);

  return retval;
}


/*
 * Activation
 */

static void
set_cloexec (gint fd)
{
  fcntl (fd, F_SETFD, FD_CLOEXEC);
}

static void
close_fd_func (gpointer data)
{
  int *pipes = data;
  
  gint open_max;
  gint i;
  
  open_max = sysconf (_SC_OPEN_MAX);
  for (i = 3; i < open_max; i++)
    {
      /* don't close our write pipe */
      if (i != pipes[1])
        set_cloexec (i);
    }
}

ConfigServer
gconf_activate_server (gboolean  start_if_not_found,
                       GError  **error)
{
  ConfigServer server;
  int p[2] = { -1, -1 };
  char buf[1];
  GError *tmp_err;
  char *argv[3];
  char *gconfd_dir;
  char *lock_dir;
  GString *failure_log;
  CORBA_Environment ev;

  failure_log = g_string_new (NULL);
  
  gconfd_dir = gconf_get_daemon_dir ();
  
  if (mkdir (gconfd_dir, 0700) < 0 && errno != EEXIST)
    gconf_log (GCL_WARNING, _("Failed to create %s: %s"),
               gconfd_dir, g_strerror (errno));

  g_free (gconfd_dir);

  g_string_append (failure_log, " 1: ");
  lock_dir = gconf_get_lock_dir ();
  server = gconf_get_current_lock_holder (lock_dir, failure_log);
  g_free (lock_dir);

  /* Confirm server exists */
  CORBA_exception_init (&ev);

  if (!CORBA_Object_is_nil (server, &ev))
    {
      ConfigServer_ping (server, &ev);
      
      if (ev._major != CORBA_NO_EXCEPTION)
        {
          server = CORBA_OBJECT_NIL;

          g_string_append_printf (failure_log,
                                  _("Server ping error: %s"),
                                  CORBA_exception_id (&ev));
        }
    }

  CORBA_exception_free (&ev);
  
  if (server != CORBA_OBJECT_NIL)
    {
      g_string_free (failure_log, TRUE);
      return server;
    }
  
  if (start_if_not_found)
    {
      /* Spawn server */
      if (pipe (p) < 0)
        {
          g_set_error (error,
                       GCONF_ERROR,
                       GCONF_ERROR_NO_SERVER,
                       _("Failed to create pipe for communicating with spawned gconf daemon: %s\n"),
                       g_strerror (errno));
          goto out;
        }

      argv[0] = g_strconcat (GCONF_SERVERDIR, "/" GCONFD, NULL);
      argv[1] = g_strdup_printf ("%d", p[1]);
      argv[2] = NULL;
  
      tmp_err = NULL;
      if (!g_spawn_async (NULL,
                          argv,
                          NULL,
                          G_SPAWN_LEAVE_DESCRIPTORS_OPEN,
                          close_fd_func,
                          p,
                          NULL,
                          &tmp_err))
        {
          g_free (argv[0]);
          g_free (argv[1]);
          g_set_error (error,
                       GCONF_ERROR,
                       GCONF_ERROR_NO_SERVER,
                       _("Failed to launch configuration server: %s\n"),
                       tmp_err->message);
          g_error_free (tmp_err);
          goto out;
        }
      
      g_free (argv[0]);
      g_free (argv[1]);
  
      /* Block until server starts up */
      read (p[0], buf, 1);

      g_string_append (failure_log, " 2: ");
      lock_dir = gconf_get_lock_dir ();
      server = gconf_get_current_lock_holder (lock_dir, failure_log);
      g_free (lock_dir);
    }
  
 out:
  if (server == CORBA_OBJECT_NIL &&
      error &&
      *error == NULL)
    g_set_error (error,
                 GCONF_ERROR,
                 GCONF_ERROR_NO_SERVER,
                 _("Failed to contact configuration server; some possible causes are that you need to enable TCP/IP networking for ORBit, or you have stale NFS locks due to a system crash. See http://www.gnome.org/projects/gconf/ for information. (Details - %s)"),
                 failure_log->len > 0 ? failure_log->str : _("none"));

  g_string_free (failure_log, TRUE);
  
  close (p[0]);
  close (p[1]);
  
  return server;
}
