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

static GHashTable *databases = NULL;

struct _GConfDatabaseDBus {
	GConfDatabase   *db;
	DBusConnection  *conn;
	const char     **object_path;

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
        
static GConfValue *
database_do_lookup (DBusConnection  *conn,
		    DBusMessage     *message,
		    char           **schema_name,
		    gboolean        *value_is_default,
		    gboolean        *value_is_writable, 
		    GConfDatabaseDBus *db)
{
  const gchar *key;
  const gchar *locale;
  const gchar **locales = NULL;
  gboolean     use_schema_default;
  GConfValue  *value;
  GError      *gerror = NULL;

  if (!gconfd_dbus_get_message_args (conn, message,
				     DBUS_TYPE_STRING, &key,
				     DBUS_TYPE_STRING, &locale,
				     DBUS_TYPE_BOOLEAN, &use_schema_default,
				     0)) 
    return NULL;
  
  /* FIXME: Locales! */
  
  value = gconf_database_query_value (db->db, key, locales, use_schema_default,
				      schema_name, value_is_default, 
				      value_is_writable, &gerror);

  if (gconfd_dbus_set_exception (conn, message, &gerror))
    return NULL;

  return value;
}

static void
database_handle_lookup (DBusConnection  *conn,
                        DBusMessage     *message,
                        GConfDatabaseDBus *db)
{
  GConfValue *value;
  DBusMessage *reply;
  
  value = database_do_lookup (conn, message, NULL, NULL, NULL, db);
  if (!value) /* Error reply sent by database_do_lookup */
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

  value = database_do_lookup (conn, message, 
			      &schema_name, 
			      &value_is_default, &value_is_writable, db);
  if (!value) /* Error reply sent by database_do_lookup */
    return;

  /* FIXME: Finish */
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

 /* FIXME: Fetch value correctly */
  if (!gconfd_dbus_get_message_args (conn, message, 
				     DBUS_TYPE_STRING, &key,
				     DBUS_TYPE_STRING, &value,
				     0))
    return;
 
  gconf_database_set (db->db, key, value, &gerror);

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
  const gchar *key;
  const gchar *locale;
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
  const gchar *dir;
  GError      *gerror = NULL;
  DBusMessage *reply;
 
  if (!gconfd_dbus_get_message_args (conn, message, 
				     DBUS_TYPE_STRING, &dir,
				     0))
    return;

  exists = gconf_database_dir_exists (db->db, dir, &gerror);

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
  GSList *entries;
  const gchar *dir;
  const gchar *locale;
  const gchar  **locales = NULL;
  GError       *gerror = NULL;

  if (!gconfd_dbus_get_message_args (conn, message, 
				     DBUS_TYPE_STRING, &dir,
				     DBUS_TYPE_STRING, &locale,
				     0)) 
    return;

  /* FIXME: Create locales from locale */
  entries = gconf_database_all_entries (db->db, dir, locales, &gerror);

  if (gconfd_dbus_set_exception (conn, message, &gerror))
    return;

  /* FIXME: FINISH */
  /* FIXME Free entries? */
}
                                                                                
static void
database_handle_get_all_dirs (DBusConnection *conn,
                              DBusMessage    *message,
                              GConfDatabaseDBus *db)
{
  GSList *dirs, *l;
  const gchar *dir;
  GError      *gerror = NULL;
  DBusMessage *reply;

  if (!gconfd_dbus_get_message_args (conn, message,
				     DBUS_TYPE_STRING, &dir,
				     0)) 
    return;

  dirs = gconf_database_all_dirs (db->db, dir, &gerror);

  if (gconfd_dbus_set_exception (conn, message, &gerror))
    return;
  
  reply = dbus_message_new_method_return (message);
  
  for (l = dirs; l; l = l->next) 
    {
      gchar *str = (gchar *) l->data;
      
      dbus_message_append_args (message,
				DBUS_TYPE_STRING, str,
				0);
    }
/* FIXME: Free dirs? */
  dbus_connection_send (conn, reply, NULL);
  dbus_message_unref (reply);
}
                                                                                
static void
database_handle_set_schema (DBusConnection *conn,
                            DBusMessage    *message,
                            GConfDatabaseDBus *db)
{
  const gchar *key;
  const gchar *schema_key;
  GError      *gerror = NULL;
  DBusMessage *reply;

  if (!gconfd_dbus_get_message_args (conn, message,
				     DBUS_TYPE_STRING, &key,
				     DBUS_TYPE_STRING, &schema_key,
				     0)) 
    return;

  gconf_database_set_schema (db->db, key, schema_key, &gerror);

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
  GConfDatabaseDBus *dbus_db;
  GConfDatabase     *db;

  dbus_db = g_hash_table_lookup (databases, address);
  if (dbus_db)
    return dbus_db;

  db = gconfd_obtain_database (address, gerror);
  if (!db) 
    return NULL;

  dbus_db = g_new0 (GConfDatabaseDBus, 1);
  dbus_db->db = db;
  dbus_db->conn = conn;

  /* Come up with a object_path */
  if (!dbus_connection_register_object_path (conn, dbus_db->object_path, 
					     &database_vtable, dbus_db)) 
    {
      gconf_log (GCL_ERR, _("Failed to register database object with the D-BUS bus daemon"));
      return NULL;
    }

  return dbus_db;
}

gboolean
database_foreach_unregister (gpointer key,
			     GConfDatabaseDBus *db,
			     gpointer user_data)
{
  dbus_connection_unregister_object_path (db->conn, db->object_path);

  return TRUE;
}

void 
gconf_database_dbus_unregister_all (void)
{
  g_hash_table_foreach_remove (databases, 
			       (GHRFunc) database_foreach_unregister, NULL);
}

const char **
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
