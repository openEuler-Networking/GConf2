/* GConf
 * Copyright (C) 2003  CodeFactory AB
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

#include "gconfd-dbus.h"
#include "gconf-internals.h"
#include "gconfd.h"

static const char *config_database_messages[] = {
  GCONFD_CONFIG_DATABASE_DIR_EXISTS,
  GCONFD_CONFIG_DATABASE_ALL_DIRS
};

static const char *config_server_messages[] =
{
  GCONFD_CONFIG_SERVER_SHUTDOWN
};

static void
gconfd_shutdown (DBusConnection *connection,
		 DBusMessage    *message)
{
  if (gconfd_dbus_check_in_shutdown (connection, message))
    return;

  gconf_log(GCL_DEBUG, _("Shutdown request received"));

  gconf_main_quit();
}

static DBusHandlerResult
gconfd_config_server_handler (DBusMessageHandler *handler,
			      DBusConnection     *connection,
			      DBusMessage        *message,
			      void               *user_data)
{
  if (dbus_message_name_is (message, GCONFD_CONFIG_SERVER_SHUTDOWN))
    {
      gconfd_shutdown (connection, message);
      
      return DBUS_HANDLER_RESULT_REMOVE_MESSAGE;
    }
  
  return DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

static gboolean
gconf_dbus_set_exception (DBusConnection *connection,
			  DBusMessage    *message,
			  GError         *error)
{
  if (error)
    printf ("error: %s\n", error->message);
  
  g_warning ("FIXME: Check GError and maybe send back an error message");
  /* FIXME: Check GError and maybe send back an error message */
  return FALSE;
}
static GConfDatabase *
gconf_database_from_id (DBusConnection *connection,
			DBusMessage    *message,
			guint           id)
{
  if (id == 0)
    return gconfd_lookup_database (NULL);
  else
    {
      /* FIXME: Send error message */
      return NULL;
    }
}

/* Convenience function that returns an error if the message is malformed */
static gboolean
gconf_dbus_get_message_args (DBusConnection *connection,
			     DBusMessage    *message,
			     int             first_arg_type,
			     ...)
{
  DBusResultCode retval;
  va_list var_args;

  va_start (var_args, first_arg_type);
  retval = dbus_message_get_args_valist (message, first_arg_type, var_args);
  va_end (var_args);

  if (retval != DBUS_RESULT_SUCCESS)
    {
      /* FIXME: Send error message */
      return FALSE;      
    }

  return TRUE;
}

static void
gconfd_config_database_dir_exists (DBusConnection *connection,
				   DBusMessage    *message)
{
  gboolean retval;
  int id;
  char *dir;
  GConfDatabase *db;
  DBusMessage *reply;
  GError *error = NULL;
  
  if (gconfd_dbus_check_in_shutdown (connection, message))
    return;

  if (!gconf_dbus_get_message_args (connection, message,
				    DBUS_TYPE_UINT32, &id,
				    DBUS_TYPE_STRING, &dir,
				    0))
      return;

  if (!(db = gconf_database_from_id (connection, message, id)))
    {
      dbus_free (dir);
      return;
    }

  retval =
    gconf_database_dir_exists (db, dir, &error);

  dbus_free (dir);
  
  if (gconf_dbus_set_exception (connection, message, error))
    return;

  reply = dbus_message_new_reply (message);
  dbus_message_append_boolean (reply, retval);
  dbus_connection_send_message (connection, reply, NULL, NULL);
  dbus_message_unref (reply);
}

static void
gconfd_config_database_all_dirs (DBusConnection *connection,
				 DBusMessage    *message)
{
  char *dir;
  int id;
  GConfDatabase *db;
  DBusMessage *reply;
  GSList *subdirs, *tmp;
  int i, len;
  char **dirs;
  GError* error = NULL;
  
  if (gconfd_dbus_check_in_shutdown (connection, message))
    return;
  
  if (!gconf_dbus_get_message_args (connection, message,
				    DBUS_TYPE_UINT32, &id,
				    DBUS_TYPE_STRING, &dir,
				    0))
    return;
  
  if (!(db = gconf_database_from_id (connection, message, id)))
    {
      dbus_free (dir);
      return;
    }

  printf ("woohoo, all dirs!!!\n");
  
  subdirs = gconf_database_all_dirs (db, dir, &error);
  printf ("subdirs is: %p\n", subdirs);
  if (gconf_dbus_set_exception (connection, message, error))
    return;  

  len = g_slist_length (subdirs);
  dirs = g_new (char *, len + 1);
  dirs[len] = NULL;
  
  tmp = subdirs;
  i = 0;

  while (tmp != NULL)
    {
      gchar *subdir = tmp->data;

      printf ("adding: %s\n", subdir);
      dirs[i] = subdir;

      ++i;
      tmp = tmp->next;
    }

  g_assert (i == len);
  g_slist_free (subdirs);
  
  reply = dbus_message_new_reply (message);
  dbus_message_append_string_array (reply, dirs, len);
  dbus_connection_send_message (connection, reply, NULL, NULL);
  dbus_message_unref (reply);
  
  g_strfreev (dirs);
}

static DBusHandlerResult
gconfd_config_database_handler (DBusMessageHandler *handler,
				DBusConnection     *connection,
				DBusMessage        *message,
				void               *user_data)
{
  if (dbus_message_name_is (message, GCONFD_CONFIG_DATABASE_DIR_EXISTS))
    {
      gconfd_config_database_dir_exists (connection, message);
      return DBUS_HANDLER_RESULT_REMOVE_MESSAGE;
    }
  else if (dbus_message_name_is (message, GCONFD_CONFIG_DATABASE_ALL_DIRS))
    {
      gconfd_config_database_all_dirs (connection, message);
      return DBUS_HANDLER_RESULT_REMOVE_MESSAGE;
    }
  
  return DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

static const char *
get_dbus_address (void)
{
  /* FIXME: Change this when we know how to find the message bus. */
  return g_getenv ("GCONFD_DBUS_ADDRESS");
}

gboolean
gconfd_dbus_init (void)
{
  const char *dbus_address;
  DBusResultCode result;
  DBusMessageHandler *handler;
  DBusConnection *connection;
  char *name;

  dbus_address = get_dbus_address ();
  if (!dbus_address)
    {
      gconf_log (GCL_ERR, _("Failed to get the D-BUS bus daemon address"));
      return FALSE;
    }
  
  connection = dbus_connection_open (dbus_address, &result);

  if (!connection)
    {
      gconf_log (GCL_ERR, _("Failed to connect to the D-BUS bus daemon: %s"),
		 dbus_result_to_string (result));
      return FALSE;
    }

  name = dbus_bus_register_client (connection, &result);
  if (!name)
    {
      gconf_log (GCL_ERR, _("Failed to register client with the D-BUS bus daemon: %s"),
		 dbus_result_to_string (result));
      return FALSE;
    }

  if (dbus_bus_acquire_service (connection, "org.freedesktop.Config.Server",
				DBUS_SERVICE_FLAG_PROHIBIT_REPLACEMENT,
				&result) != DBUS_SERVICE_REPLY_PRIMARY_OWNER)
    {
      gconf_log (GCL_ERR, _("Failed to acquire resource"));
      return FALSE;
    }

  dbus_connection_setup_with_g_main (connection);

  /* Add the config server handler */
  handler = dbus_message_handler_new (gconfd_config_server_handler, NULL, NULL);
  dbus_connection_register_handler (connection, handler, config_server_messages,
				    G_N_ELEMENTS (config_server_messages));

  /* Add the config database handler */
  handler = dbus_message_handler_new (gconfd_config_database_handler, NULL, NULL);
  dbus_connection_register_handler (connection, handler, config_database_messages,
				    G_N_ELEMENTS (config_database_messages));
  
  return TRUE;
}

gboolean
gconfd_dbus_check_in_shutdown (DBusConnection *connection,
			       DBusMessage    *message)
{
  if (gconfd_in_shutdown ())
    {
      /* FIXME: Send back an error */
      return TRUE;
    }
  else
    return FALSE;
}
