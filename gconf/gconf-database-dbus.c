/* GConf
 * Copyright (C) 2003 Imendio HB
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

#include "gconfd.h"
#include "gconf-dbus-utils.h"
#include "gconfd-dbus.h"
#include "gconf-database-dbus.h"

#define DATABASE_OBJECT_PATH "/org/gnome/GConf/Database"

static GHashTable *databases = NULL;
static gint object_nr = 0;

struct _GConfDatabaseDBus {
	GConfDatabase  *db;
	DBusConnection *conn;
	const char     *object_path;

	/* Information about listeners */
};
static void           database_unregistered_func (DBusConnection  *connection,
                                                  GConfDatabaseDBus *db);
static DBusHandlerResult
database_message_func                            (DBusConnection  *connection,
                                                  DBusMessage     *message,
                                                  GConfDatabaseDBus *db);
static void           database_handle_lookup     (DBusConnection  *conn,
                                                  DBusMessage     *message,
                                                  GConfDatabaseDBus *db);
static void           database_handle_lookup_ext (DBusConnection  *conn,
                                                  DBusMessage     *message,
                                                  GConfDatabaseDBus *db);
static void           database_handle_set        (DBusConnection  *conn,
                                                  DBusMessage     *message,
                                                  GConfDatabaseDBus *db);
static void           database_handle_unset      (DBusConnection  *conn,
                                                  DBusMessage     *message,
                                                  GConfDatabaseDBus *db);
static void
database_handle_recursive_unset                  (DBusConnection  *conn,
                                                  DBusMessage     *message,
                                                  GConfDatabaseDBus *db);
static void           database_handle_dir_exists (DBusConnection  *conn,
                                                  DBusMessage     *message,
                                                  GConfDatabaseDBus *db);
static void
database_handle_get_all_entries                  (DBusConnection  *conn,
                                                  DBusMessage     *message,
                                                  GConfDatabaseDBus *db);
static void
database_handle_get_all_dirs                     (DBusConnection  *conn,
                                                  DBusMessage     *message,
                                                  GConfDatabaseDBus *db);
static void           database_handle_set_schema (DBusConnection  *conn,
                                                  DBusMessage     *message,
                                                  GConfDatabaseDBus *db);
static DBusObjectPathVTable
database_vtable = {
        (DBusObjectPathUnregisterFunction) database_unregistered_func,
        (DBusObjectPathMessageFunction)    database_message_func,
        NULL,
};
 
static void
database_unregistered_func (DBusConnection *connection, GConfDatabaseDBus *db)
{
        g_print ("Database object unregistered\n");
}

static DBusHandlerResult
database_message_func (DBusConnection  *connection,
                       DBusMessage     *message,
                       GConfDatabaseDBus *db)
{
        g_print ("In database func\n");
         
        if (dbus_message_is_method_call (message,
                                         GCONF_DBUS_DATABASE_INTERFACE,
					 GCONF_DBUS_DATABASE_LOOKUP)) {
                database_handle_lookup (connection, message, db);
        }
        else if (dbus_message_is_method_call (message,
                                              GCONF_DBUS_DATABASE_INTERFACE,
                                              GCONF_DBUS_DATABASE_LOOKUP_EXTENDED)) {
                database_handle_lookup_ext (connection, message, db);
        }
	else if (dbus_message_is_method_call (message,
                                              GCONF_DBUS_DATABASE_INTERFACE,
                                              GCONF_DBUS_DATABASE_SET)) {
                database_handle_set (connection, message, db);
        }
	else if (dbus_message_is_method_call (message,
                                              GCONF_DBUS_DATABASE_INTERFACE,
                                              GCONF_DBUS_DATABASE_UNSET)) {
                database_handle_unset (connection, message, db);
        }
        else if (dbus_message_is_method_call (message,
                                              GCONF_DBUS_DATABASE_INTERFACE,
                                              GCONF_DBUS_DATABASE_RECURSIVE_UNSET)) {
                database_handle_recursive_unset (connection, message, db);
        }
        else if (dbus_message_is_method_call (message,
                                              GCONF_DBUS_DATABASE_INTERFACE,
                                              GCONF_DBUS_DATABASE_DIR_EXISTS)) {
                database_handle_dir_exists (connection, message, db);
        }
        else if (dbus_message_is_method_call (message,
                                              GCONF_DBUS_DATABASE_INTERFACE,
                                              GCONF_DBUS_DATABASE_GET_ALL_ENTRIES)) {
                database_handle_get_all_entries (connection, message, db);
        }
        else if (dbus_message_is_method_call (message,
                                              GCONF_DBUS_DATABASE_INTERFACE,
                                              GCONF_DBUS_DATABASE_GET_ALL_DIRS)) {
                database_handle_get_all_dirs (connection, message, db);
        }
        else if (dbus_message_is_method_call (message,
                                              GCONF_DBUS_DATABASE_INTERFACE,
                                              GCONF_DBUS_DATABASE_SET_SCHEMA)) {
                database_handle_set_schema (connection, message, db);
        } else {
                return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        }
                                                                                
        return DBUS_HANDLER_RESULT_HANDLED;
}
        
static void
database_handle_lookup (DBusConnection  *conn,
                        DBusMessage     *message,
                        GConfDatabaseDBus *db)
{
  GConfValue *value;
  DBusMessage *reply;
  gchar *key;
  gchar *locale;
  GConfLocaleList *locales;
  gboolean     use_schema_default;
  GError      *gerror = NULL;
  
  if (!gconfd_dbus_get_message_args (conn, message,
				     DBUS_TYPE_STRING, &key,
				     DBUS_TYPE_STRING, &locale,
				     DBUS_TYPE_BOOLEAN, &use_schema_default,
				     0))
    return;
  
  locales = gconfd_locale_cache_lookup (locale);
  dbus_free (locale);
  
  value = gconf_database_query_value (db->db, key, locales->list, 
				      use_schema_default,
				      NULL, NULL, NULL, &gerror);
  dbus_free (key);

  if (gconfd_dbus_set_exception (conn, message, &gerror))
    return;

  reply = dbus_message_new_method_return (message);
  gconf_dbus_message_append_gconf_value (reply, value);
  dbus_connection_send (conn, reply, NULL);
  dbus_message_unref (reply);
}

static void 
database_handle_lookup_ext (DBusConnection  *conn,
			    DBusMessage     *message,
			    GConfDatabaseDBus *db)
{
  GConfValue *value;
  gchar      *schema_name;
  gboolean    value_is_default;
  gboolean    value_is_writable;
  DBusMessage *reply;
  gchar *key;
  gchar *locale;
  GConfLocaleList *locales;
  gboolean     use_schema_default;
  GError      *gerror = NULL;
  
  if (!gconfd_dbus_get_message_args (conn, message,
				     DBUS_TYPE_STRING, &key,
				     DBUS_TYPE_STRING, &locale,
				     DBUS_TYPE_BOOLEAN, &use_schema_default,
				     0))
    return;
  
  locales = gconfd_locale_cache_lookup (locale);
  dbus_free (locale);
  
  value = gconf_database_query_value (db->db, key, locales->list,
				      use_schema_default,
				      &schema_name, &value_is_default, 
				      &value_is_writable, &gerror);

  if (gconfd_dbus_set_exception (conn, message, &gerror))
    return;

  reply = dbus_message_new_method_return (message);
  gconf_dbus_message_append_entry (reply,
				   key,
				   value,
				   value_is_default,
				   value_is_writable,
				   schema_name);
  dbus_free (key);
  gconf_value_free (value);
  g_free (schema_name);

  dbus_connection_send (conn, reply, NULL);
  dbus_message_unref (reply);
}
                                                                      
static void
database_handle_set (DBusConnection *conn,
                     DBusMessage    *message,
                     GConfDatabaseDBus *db)
{
  gchar *key;
  GConfValue *value = NULL; 
  GError *gerror = NULL;
  DBusMessage *reply;
  DBusMessageIter iter;

  dbus_message_iter_init (message, &iter);

  /* FIXME: Error handling */
  key = dbus_message_iter_get_string (&iter);
  value = gconf_dbus_create_gconf_value_from_message_iter (&iter);

  gconf_database_set (db->db, key, value, &gerror);
  dbus_free (key);
  gconf_value_free (value);

  if (gconfd_dbus_set_exception (conn, message, &gerror))
    return;

  reply = dbus_message_new_method_return (message);
  dbus_connection_send (conn, reply, NULL);
  dbus_message_unref (reply);
}

static void
database_handle_unset (DBusConnection *conn,
		       DBusMessage    *message,
		       GConfDatabaseDBus *db)
{
  gchar *key;
  gchar *locale;
  GError *gerror = NULL;
  DBusMessage *reply;

  if (!gconfd_dbus_get_message_args (conn, message,
				     DBUS_TYPE_STRING, &key,
				     DBUS_TYPE_STRING, &locale,
				     0))
    return;

  gconf_database_unset (db->db, key, locale, &gerror);
  dbus_free (key);
  dbus_free (locale);
  
  if (gconfd_dbus_set_exception (conn, message, &gerror))
    return;
 
  reply = dbus_message_new_method_return (message);
  dbus_connection_send (conn, reply, NULL);
  dbus_message_unref (reply);
}
                                                                               
static void
database_handle_recursive_unset  (DBusConnection *conn,
                                  DBusMessage    *message,
                                  GConfDatabaseDBus *db)
{
  gchar *key;
  gchar *locale;
  GError      *gerror = NULL;
  guint32      unset_flags;
  DBusMessage *reply;
  
  if (!gconfd_dbus_get_message_args (conn, message, 
				     DBUS_TYPE_STRING, &key,
				     DBUS_TYPE_STRING, &locale,
				     DBUS_TYPE_UINT32, &unset_flags,
				     0))
    return;

  gconf_database_recursive_unset (db->db, key, locale, unset_flags, &gerror);
  dbus_free (key);
  dbus_free (locale);
  
  if (gconfd_dbus_set_exception (conn, message, &gerror))
    return;

  reply = dbus_message_new_method_return (message);
  dbus_connection_send (conn, reply, NULL);
  dbus_message_unref (reply);
}
                                                                                
static void
database_handle_dir_exists (DBusConnection *conn,
                            DBusMessage    *message,
                            GConfDatabaseDBus *db)
{
  gboolean     exists;
  gchar *dir;
  GError      *gerror = NULL;
  DBusMessage *reply;
 
  if (!gconfd_dbus_get_message_args (conn, message, 
				     DBUS_TYPE_STRING, &dir,
				     0))
    return;

  exists = gconf_database_dir_exists (db->db, dir, &gerror);
  dbus_free (dir);

  if (gconfd_dbus_set_exception (conn, message, &gerror))
    return;

  reply = dbus_message_new_method_return (message);
  dbus_message_append_args (reply,
			    DBUS_TYPE_BOOLEAN, exists,
			    0);
  dbus_connection_send (conn, reply, NULL);
  dbus_message_unref (reply);
}
                                                                                
static void
database_handle_get_all_entries (DBusConnection *conn,
                                 DBusMessage    *message,
                                 GConfDatabaseDBus *db)
{
  GSList *entries, *l;
  gchar  *dir;
  gchar  *locale;
  GError *gerror = NULL;
  GConfLocaleList* locales;
  DBusMessage *reply;

  if (!gconfd_dbus_get_message_args (conn, message, 
				     DBUS_TYPE_STRING, &dir,
				     DBUS_TYPE_STRING, &locale,
				     0)) 
    return;

  locales = gconfd_locale_cache_lookup (locale);
  dbus_free (locale);

  /* FIXME: Create locales from locale */
  entries = gconf_database_all_entries (db->db, dir, 
					locales->list, &gerror);
  dbus_free (dir);

  if (gconfd_dbus_set_exception (conn, message, &gerror))
    return;

  reply = dbus_message_new_method_return (message);
  
  for (l = entries; l; l = l->next)
    {
      GConfEntry *entry = l->data;

      gconf_dbus_message_append_entry (reply,
				       entry->key,
				       gconf_entry_get_value (entry),
				       gconf_entry_get_is_default (entry),
				       gconf_entry_get_is_writable (entry),
				       gconf_entry_get_schema_name (entry));
      gconf_entry_free (entry);
    }
  
  dbus_connection_send (conn, reply, NULL);
  dbus_message_unref (reply);

  g_slist_free (entries);
}
                                                                                
static void
database_handle_get_all_dirs (DBusConnection *conn,
                              DBusMessage    *message,
                              GConfDatabaseDBus *db)
{
  GSList *dirs, *l;
  gchar *dir;
  GError      *gerror = NULL;
  DBusMessage *reply;

  if (!gconfd_dbus_get_message_args (conn, message,
				     DBUS_TYPE_STRING, &dir,
				     0)) 
    return;

  dirs = gconf_database_all_dirs (db->db, dir, &gerror);

  dbus_free (dir);

  if (gconfd_dbus_set_exception (conn, message, &gerror))
    return;
  
  reply = dbus_message_new_method_return (message);
  
  for (l = dirs; l; l = l->next) 
    {
      gchar *str = (gchar *) l->data;
      
      dbus_message_append_args (message,
				DBUS_TYPE_STRING, str,
				0);
      g_free (l->data);
    }
 
  g_slist_free (dirs);
  
  dbus_connection_send (conn, reply, NULL);
  dbus_message_unref (reply);
}
                                                                                
static void
database_handle_set_schema (DBusConnection *conn,
                            DBusMessage    *message,
                            GConfDatabaseDBus *db)
{
  gchar *key;
  gchar *schema_key;
  GError      *gerror = NULL;
  DBusMessage *reply;

  if (!gconfd_dbus_get_message_args (conn, message,
				     DBUS_TYPE_STRING, &key,
				     DBUS_TYPE_STRING, &schema_key,
				     0)) 
    return;

  gconf_database_set_schema (db->db, key, schema_key, &gerror);
  dbus_free (key);
  dbus_free (schema_key);

  if (gconfd_dbus_set_exception (conn, message, &gerror))
    return;

  reply = dbus_message_new_method_return (message);
  dbus_connection_send (conn, reply, NULL);
  dbus_message_unref (reply);
}

GConfDatabaseDBus *
gconf_database_dbus_get (DBusConnection *conn, const gchar *address,
			 GError **gerror)
{
  GConfDatabaseDBus  *dbus_db;
  GConfDatabase      *db;
  gchar             **path;

  dbus_db = g_hash_table_lookup (databases, address);
  if (dbus_db)
    return dbus_db;

  db = gconfd_obtain_database (address, gerror);
  if (!db) 
    return NULL;

  dbus_db = g_new0 (GConfDatabaseDBus, 1);
  dbus_db->db = db;
  dbus_db->conn = conn;
  dbus_db->object_path = g_strdup_printf ("%s/%d", 
					  DATABASE_OBJECT_PATH, 
					  object_nr++);
 
  path = g_strsplit (dbus_db->object_path, "/", -1);
  dbus_connection_register_object_path (conn, (const gchar**) path, 
					&database_vtable, dbus_db);
  g_strfreev (path);

  return dbus_db;
}

gboolean
database_foreach_unregister (gpointer key,
			     GConfDatabaseDBus *db,
			     gpointer user_data)
{
  gchar **path;

  path = g_strsplit (db->object_path, "/", -1);
  dbus_connection_unregister_object_path (db->conn, (const gchar **)path);
  g_strfreev (path);

  return TRUE;
}

void 
gconf_database_dbus_unregister_all (void)
{
  g_hash_table_foreach_remove (databases, 
			       (GHRFunc) database_foreach_unregister, NULL);
}

const char *
gconf_database_dbus_get_path (GConfDatabaseDBus *db)
{
  return db->object_path;
}

void
gconf_database_dbus_notify_listeners (GConfDatabase    *db,
				      const gchar      *key,
				      const GConfValue *value,
				      gboolean          is_default,
				      gboolean          is_writable)
{

}
