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
#include "gconf-locale.h"
#include "gconfd.h"
#include "gconf-dbus.h"
#include <time.h>

static const char *config_database_messages[] = {
  GCONF_DBUS_CONFIG_DATABASE_DIR_EXISTS,
  GCONF_DBUS_CONFIG_DATABASE_ALL_DIRS,
  GCONF_DBUS_CONFIG_DATABASE_ALL_ENTRIES,
  GCONF_DBUS_CONFIG_DATABASE_LOOKUP,
  GCONF_DBUS_CONFIG_DATABASE_LOOKUP_DEFAULT_VALUE,
  GCONF_DBUS_CONFIG_DATABASE_REMOVE_DIR,
  GCONF_DBUS_CONFIG_DATABASE_ADD_LISTENER,
  GCONF_DBUS_CONFIG_DATABASE_SET,
};

static const char *config_server_messages[] =
{
  GCONF_DBUS_CONFIG_SERVER_SHUTDOWN
};

typedef struct {
  GConfDatabaseListener parent;
  char *who;
} Listener;

static Listener* listener_new     (const char *who,
				   const char *name);
static void      listener_destroy (Listener   *l);

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
  if (dbus_message_name_is (message, GCONF_DBUS_CONFIG_SERVER_SHUTDOWN))
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
  /* FIXME: Check GError and maybe send back an error message */
  if (error)
    g_warning ("FIXME: Check GError and send back an error message");    
  

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
      g_warning ("malformed message of type %s\n", dbus_message_get_name (message));
      
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
gconfd_config_database_all_entries (DBusConnection *connection,
				    DBusMessage    *message)
{
  char *dir;
  int id;
  GConfDatabase *db;
  GConfLocaleList* locale_list;  
  DBusMessage *reply;
  GSList *pairs, *tmp;
  int i, len;
  GError* error = NULL;
  char *locale;
  char **keys;
  char **schema_names;
  unsigned char *is_defaults;
  unsigned char *is_writables;
  
  if (gconfd_dbus_check_in_shutdown (connection, message))
    return;
  
  if (!gconf_dbus_get_message_args (connection, message,
				    DBUS_TYPE_UINT32, &id,
				    DBUS_TYPE_STRING, &dir,
				    DBUS_TYPE_STRING, &locale,				    
				    0))
    return;
  
  if (!(db = gconf_database_from_id (connection, message, id)))
    {
      dbus_free (dir);
      return;
    }

  locale_list = gconfd_locale_cache_lookup (locale);
  pairs = gconf_database_all_entries(db, dir, locale_list->list, &error);

  dbus_free (dir);
  if (gconf_dbus_set_exception (connection, message, error))
    return;

  len = g_slist_length(pairs);

  keys = g_new (char *, len + 1);
  keys[len] = NULL;

  schema_names = g_new (char *, len + 1);
  schema_names[len] = NULL;

  is_defaults = g_new (unsigned char, len);
  is_writables = g_new (unsigned char, len);  
    
  tmp = pairs;
  i = 0;
  while (tmp != NULL)
    {
      GConfEntry *p = tmp->data;

      g_assert(p != NULL);
      g_assert(p->key != NULL);

      schema_names[i] = g_strdup (gconf_entry_get_schema_name (p));
      if (!schema_names[i])
	schema_names[i] = g_strdup ("");
      
      keys[i] = g_strdup (p->key);
      is_defaults[i] = gconf_entry_get_is_default (p);
      is_writables[i] = gconf_entry_get_is_writable (p);
      ++i;

      tmp = tmp->next;
    }
  
  reply = dbus_message_new_reply (message);
  
  dbus_message_append_args (reply,
			    DBUS_TYPE_STRING_ARRAY, (const char **)keys, len,
			    DBUS_TYPE_STRING_ARRAY, (const char **)schema_names, len,
			    DBUS_TYPE_BOOLEAN_ARRAY, (const char **)is_defaults, len,
			    DBUS_TYPE_BOOLEAN_ARRAY, (const char **)is_writables, len,			    
			    0);
  
  g_strfreev (keys);
  g_strfreev (schema_names);
  g_free (is_defaults);
  g_free (is_writables);

  /* Now append the message values */
  tmp = pairs;
  i = 0;
  while (tmp != NULL)
    {
      GConfEntry *p = tmp->data;

      gconf_dbus_fill_message_from_gconf_value (reply, gconf_entry_get_value (p));

      g_assert(p != NULL);
      g_assert(p->key != NULL);

      gconf_entry_free (p);
      
      tmp = tmp->next;
    }
  g_slist_free(pairs);
  
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

  subdirs = gconf_database_all_dirs (db, dir, &error);
  dbus_free (dir);
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

      dirs[i] = subdir;

      ++i;
      tmp = tmp->next;
    }

  g_assert (i == len);
  g_slist_free (subdirs);
  
  reply = dbus_message_new_reply (message);
  dbus_message_append_string_array (reply, (const char **)dirs, len);
  dbus_connection_send_message (connection, reply, NULL, NULL);
  dbus_message_unref (reply);
  
  g_strfreev (dirs);
}

static void
gconfd_config_database_lookup (DBusConnection *connection,
			       DBusMessage    *message)
{
  char *key;
  char *locale;
  gboolean use_schema_default;
  int id;
  GConfDatabase *db;
  GConfLocaleList* locale_list;
  GConfValue *val;
  char *s;
  gboolean is_default = FALSE;
  gboolean is_writable = TRUE;
  GError* error = NULL;
  DBusMessage *reply;
  
  if (gconfd_dbus_check_in_shutdown (connection, message))
    return;

  if (!gconf_dbus_get_message_args (connection, message,
				    DBUS_TYPE_UINT32, &id,
				    DBUS_TYPE_STRING, &key,
				    DBUS_TYPE_STRING, &locale,
				    DBUS_TYPE_BOOLEAN, &use_schema_default,
				    0))
    return;

  if (!(db = gconf_database_from_id (connection, message, id)))
    {
      dbus_free (key);
      dbus_free (locale);
      return;
    }

  locale_list = gconfd_locale_cache_lookup (locale);

  s = NULL;  
  val = gconf_database_query_value (db, key, locale_list->list,
				    use_schema_default,
				    &s,
				    &is_default,
				    &is_writable,
				    &error);

  gconf_log (GCL_DEBUG, "In lookup_with_schema_name returning schema name '%s' error '%s'",
             s, error ? error->message : "none");
  
  if (gconf_dbus_set_exception (connection, message, error))
    return;  

  reply = dbus_message_new_reply (message);
  gconf_dbus_fill_message_from_gconf_value (reply, val);
  gconf_value_free (val);
  
  dbus_message_append_string (reply, s ? s : "");
  dbus_message_append_boolean (reply, is_default);
  dbus_message_append_boolean (reply, is_writable);
  
  dbus_connection_send_message (connection, reply, NULL, NULL);
  dbus_message_unref (reply);
}

static void
gconfd_config_database_lookup_default_value (DBusConnection *connection,
					     DBusMessage    *message)
{
  char *key, *locale;
  int id;
  GConfDatabase *db;
  GConfLocaleList* locale_list;
  GConfValue *val;
  GError* error = NULL;
  DBusMessage *reply;
  
  if (gconfd_dbus_check_in_shutdown (connection, message))
    return;

  if (!gconf_dbus_get_message_args (connection, message,
				    DBUS_TYPE_UINT32, &id,
				    DBUS_TYPE_STRING, &key,
				    DBUS_TYPE_STRING, &locale,
				    0))
    return;

  if (!(db = gconf_database_from_id (connection, message, id)))
    {
      dbus_free (key);
      dbus_free (locale);
      return;
    }

  locale_list = gconfd_locale_cache_lookup(locale);
  
  val = gconf_database_query_default_value(db, key,
                                           locale_list->list,
                                           NULL,
                                           &error);

  gconf_locale_list_unref(locale_list);

  if (gconf_dbus_set_exception (connection, message, error))
    return;  

  reply = dbus_message_new_reply (message);
  gconf_dbus_fill_message_from_gconf_value (reply, val);
  gconf_value_free (val);
  dbus_connection_send_message (connection, reply, NULL, NULL);
  dbus_message_unref (reply);
}

static void
gconfd_config_database_remove_dir (DBusConnection *connection,
				   DBusMessage    *message)
{
  GError* error = NULL;
  GConfDatabase *db;
  DBusMessage *reply;
  char *dir;
  int id;

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
  
  gconf_database_remove_dir(db, dir, &error);
  dbus_free (dir);

  if (gconf_dbus_set_exception (connection, message, error))
    return;  

  /* This really sucks, but we need to ack that the removal was successful */
  reply = dbus_message_new_reply (message);
  dbus_connection_send_message (connection, reply, NULL, NULL);
  dbus_message_unref (reply);

}

static guint
gconf_database_dbus_add_listener (GConfDatabase *db,
				  const char    *who,
				  const char    *name,
				  const char    *where)
{
  guint cnxn;
  Listener *l;
  
  db->last_access = time(NULL);
  
  l = listener_new (who, name);

  cnxn = gconf_listeners_add (db->listeners, where, l,
			      (GFreeFunc)listener_destroy);

  if (l->parent.name == NULL)
    l->parent.name = g_strdup_printf ("%u", cnxn);

  gconf_log (GCL_DEBUG, "Added listener %s (%u)", l->parent.name, cnxn);
  
  return cnxn;
}


static void
gconfd_config_database_add_listener (DBusConnection *connection,
				     DBusMessage    *message)
{
  GConfDatabase *db;
  DBusMessage *reply;
  DBusDict *dict;
  char *dir;
  int id;
  int cnxn;
  const char *name = NULL;
  
  if (gconfd_dbus_check_in_shutdown (connection, message))
    return;

  if (!gconf_dbus_get_message_args (connection, message,
				    DBUS_TYPE_UINT32, &id,
				    DBUS_TYPE_STRING, &dir,
				    DBUS_TYPE_DICT, &dict,
				    0))
    return;
  
  if (!(db = gconf_database_from_id (connection, message, id)))
    {
      dbus_free (dir);
      dbus_dict_unref (dict);
      return;
    }

  dbus_dict_get_string (dict, "name", &name);

  cnxn = gconf_database_dbus_add_listener (db, dbus_message_get_sender (message), name, dir);
  reply = dbus_message_new_reply (message);
  dbus_message_append_uint32 (reply, cnxn);
  dbus_connection_send_message (connection, reply, NULL, NULL);
  dbus_message_unref (reply);
  dbus_dict_unref (dict);  
}

static void
gconfd_config_database_set (DBusConnection *connection,
			    DBusMessage    *message)
{
  GConfDatabase *db;
  DBusMessage *reply;
  char *key;
  int id;
  GConfValue *value;
  DBusMessageIter *iter;
  GError *error = NULL;
  
  if (gconfd_dbus_check_in_shutdown (connection, message))
    return;

  if (!gconf_dbus_get_message_args (connection, message,
				    DBUS_TYPE_UINT32, &id,
				    DBUS_TYPE_STRING, &key,
				    0))
    return;

  if (!(db = gconf_database_from_id (connection, message, id)))
    {
      dbus_free (key);
      return;
    }  
  
  iter = dbus_message_get_args_iter (message);
  dbus_message_iter_next (iter);
  dbus_message_iter_next (iter);
  
  value = gconf_dbus_create_gconf_value_from_message (iter);
  dbus_message_iter_unref (iter);

  gconf_database_set (db, key, value, &error);
  gconf_value_free (value);
  
  if (gconf_dbus_set_exception (connection, message, error))
    return;

  /* This really sucks, but we need to ack that the setting was successful */
  reply = dbus_message_new_reply (message);
  dbus_connection_send_message (connection, reply, NULL, NULL);
  dbus_message_unref (reply);
}

static DBusHandlerResult
gconfd_config_database_handler (DBusMessageHandler *handler,
				DBusConnection     *connection,
				DBusMessage        *message,
				void               *user_data)
{
  if (dbus_message_name_is (message, GCONF_DBUS_CONFIG_DATABASE_DIR_EXISTS))
    {
      gconfd_config_database_dir_exists (connection, message);
      return DBUS_HANDLER_RESULT_REMOVE_MESSAGE;
    }
  else if (dbus_message_name_is (message, GCONF_DBUS_CONFIG_DATABASE_ALL_DIRS))
    {
      gconfd_config_database_all_dirs (connection, message);
      return DBUS_HANDLER_RESULT_REMOVE_MESSAGE;
    }
  else if (dbus_message_name_is (message, GCONF_DBUS_CONFIG_DATABASE_ALL_ENTRIES))
    {
      gconfd_config_database_all_entries (connection, message);
      return DBUS_HANDLER_RESULT_REMOVE_MESSAGE;
    }
  else if (dbus_message_name_is (message, GCONF_DBUS_CONFIG_DATABASE_LOOKUP))
    {
      gconfd_config_database_lookup (connection, message);
      return DBUS_HANDLER_RESULT_REMOVE_MESSAGE;
    }
  else if (dbus_message_name_is (message, GCONF_DBUS_CONFIG_DATABASE_LOOKUP_DEFAULT_VALUE))
    {
      gconfd_config_database_lookup_default_value (connection, message);
      return DBUS_HANDLER_RESULT_REMOVE_MESSAGE;
    }
  else if (dbus_message_name_is (message, GCONF_DBUS_CONFIG_DATABASE_REMOVE_DIR))
    {
      gconfd_config_database_remove_dir (connection, message);
      return DBUS_HANDLER_RESULT_REMOVE_MESSAGE;
    }
  else if (dbus_message_name_is (message, GCONF_DBUS_CONFIG_DATABASE_ADD_LISTENER))
    {
      gconfd_config_database_add_listener (connection, message);
      return DBUS_HANDLER_RESULT_REMOVE_MESSAGE;
    }
  else if (dbus_message_name_is (message, GCONF_DBUS_CONFIG_DATABASE_SET))
    {
      gconfd_config_database_set (connection, message);
      return DBUS_HANDLER_RESULT_REMOVE_MESSAGE;
    }
  
  return DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

static const char *
get_dbus_address (void)
{
  /* FIXME: Change this when we know how to find the message bus. */
  return g_getenv ("GCONF_DBUS_ADDRESS");
}

static DBusConnection *dbus_conn = NULL;

gboolean
gconfd_dbus_init (void)
{
  const char *dbus_address;
  DBusResultCode result;
  DBusMessageHandler *handler;

  char *name;

  dbus_address = get_dbus_address ();
  if (!dbus_address)
    {
      gconf_log (GCL_ERR, _("Failed to get the D-BUS bus daemon address"));
      return FALSE;
    }
  
  dbus_conn = dbus_connection_open (dbus_address, &result);

  if (!dbus_conn)
    {
      gconf_log (GCL_ERR, _("Failed to connect to the D-BUS bus daemon: %s"),
		 dbus_result_to_string (result));
      return FALSE;
    }

  name = dbus_bus_register_client (dbus_conn, &result);
  if (!name)
    {
      gconf_log (GCL_ERR, _("Failed to register client with the D-BUS bus daemon: %s"),
		 dbus_result_to_string (result));
      return FALSE;
    }

  if (dbus_bus_acquire_service (dbus_conn, "org.freedesktop.Config.Server",
				DBUS_SERVICE_FLAG_PROHIBIT_REPLACEMENT,
				&result) != DBUS_SERVICE_REPLY_PRIMARY_OWNER)
    {
      gconf_log (GCL_ERR, _("Failed to acquire resource"));
      return FALSE;
    }

  dbus_connection_setup_with_g_main (dbus_conn);

  /* Add the config server handler */
  handler = dbus_message_handler_new (gconfd_config_server_handler, NULL, NULL);
  dbus_connection_register_handler (dbus_conn, handler, config_server_messages,
				    G_N_ELEMENTS (config_server_messages));

  /* Add the config database handler */
  handler = dbus_message_handler_new (gconfd_config_database_handler, NULL, NULL);
  dbus_connection_register_handler (dbus_conn, handler, config_database_messages,
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

typedef struct {
  GConfDatabase* db;
  const GConfValue *value;
  gboolean is_default;
  gboolean is_writable;
} ListenerNotifyClosure;

static void
notify_listeners_cb(GConfListeners* listeners,
                    const gchar* all_above_key,
                    guint cnxn_id,
                    gpointer listener_data,
                    gpointer user_data)
{
  Listener* l = listener_data;
  ListenerNotifyClosure* closure = user_data;
  DBusMessage *message;
  
  if (l->parent.type != GCONF_DATABASE_LISTENER_DBUS)
    return;

  message = dbus_message_new (l->who, GCONF_DBUS_CONFIG_LISTENER_NOTIFY);

  dbus_message_append_args (message,
			    DBUS_TYPE_UINT32, 0, /* FIXME: Use the correct database id */
			    DBUS_TYPE_UINT32, cnxn_id,
			    DBUS_TYPE_STRING, all_above_key,
			    DBUS_TYPE_BOOLEAN, closure->is_default,
			    DBUS_TYPE_BOOLEAN, closure->is_writable,
			    0);

  gconf_dbus_fill_message_from_gconf_value (message, closure->value);

  dbus_connection_send_message (dbus_conn, message, NULL, NULL);
  dbus_message_unref (message);
}

void
gconf_database_dbus_notify_listeners (GConfDatabase    *db,
				      const gchar      *key,
				      const GConfValue *value,
				      gboolean          is_default,
				      gboolean          is_writable)
{
  ListenerNotifyClosure closure;

  closure.db = db;
  closure.value = value;
  closure.is_default = is_default;
  closure.is_writable = is_writable;

  gconf_listeners_notify (db->listeners, key, notify_listeners_cb, &closure);
  
}

/*
 * The listener object
 */

static Listener* 
listener_new (const char *who,
              const char *name)
{
  Listener* l;
  l = g_new0 (Listener, 1);

  l->who = g_strdup (who);
  l->parent.name = g_strdup (name);
  l->parent.type = GCONF_DATABASE_LISTENER_DBUS;
  
  return l;
}

static void      
listener_destroy (Listener* l)
{  
  g_free (l->parent.name);
  g_free (l->who);
  g_free (l);
}


