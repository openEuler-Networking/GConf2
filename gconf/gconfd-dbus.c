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

#include <dbus/dbus-glib.h>
#include <string.h>
#include "gconf-database-dbus.h"
#include "gconf-dbus-utils.h"
#include "gconfd.h"
#include "gconfd-dbus.h"

static DBusConnection *bus_conn;
static const char *server_path[] = { "org", "gnome", "GConf", "Server", NULL };

static void              server_unregistered_func (DBusConnection *connection,
						   void           *user_data);
static DBusHandlerResult server_message_func      (DBusConnection  *connection,
                                                   DBusMessage     *message,
						   void            *user_data);
static void              server_handle_get_db     (DBusConnection  *connection,
                                                   DBusMessage     *message);
static void              server_handle_shutdown   (DBusConnection  *connection,
                                                   DBusMessage     *message);
static void          server_handle_get_default_db (DBusConnection  *connection,
                                                   DBusMessage     *message);

static DBusObjectPathVTable
server_vtable = {
        server_unregistered_func,
        server_message_func,
        NULL,
};

static void
server_unregistered_func (DBusConnection *connection, void *user_data)
{
        g_print ("Server object unregistered\n");
	/* Free the server object? */
}

static DBusHandlerResult
server_message_func (DBusConnection *connection,
		     DBusMessage    *message,
		     void           *user_data)
{
  if (dbus_message_get_type (message) != DBUS_MESSAGE_TYPE_METHOD_CALL) 
    {
      g_print ("Not a method call\n");
      return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
                                                                                
  if (strcmp (dbus_message_get_interface (message),
	      GCONF_DBUS_SERVER_INTERFACE) != 0) 
    {
      g_print ("Not correct interface: \"%s\"\n",
	       dbus_message_get_interface (message));
      return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
                                                                                
  if (dbus_message_is_method_call (message,
				   GCONF_DBUS_SERVER_INTERFACE,
				   GCONF_DBUS_SERVER_GET_DEFAULT_DB))
    server_handle_get_default_db (connection, message);
  else if (dbus_message_is_method_call (message,
					GCONF_DBUS_SERVER_INTERFACE,
					GCONF_DBUS_SERVER_GET_DB))
    server_handle_get_db (connection, message);
  else if (dbus_message_is_method_call (message,
					GCONF_DBUS_SERVER_INTERFACE,
					GCONF_DBUS_SERVER_SHUTDOWN)) 
    server_handle_shutdown (connection, message);
  else 
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  
  return DBUS_HANDLER_RESULT_HANDLED;
}

static void
server_real_handle_get_db (DBusConnection *connection,
			   DBusMessage    *message,
			   const char     *address)
{
  DBusMessageIter    iter;
  GConfDatabaseDBus *db;
  DBusMessage       *reply;
  GError            *gerror = NULL;
 
  if (gconfd_dbus_check_in_shutdown (connection, message))
    return;

  db = gconf_database_dbus_get (connection, address, &gerror);

  if (gconfd_dbus_set_exception (connection, message, &gerror))
    return;
  
  reply = dbus_message_new_method_return (message);
  if (reply == NULL) 
      g_error ("No memory");
                                                                                
  dbus_message_append_iter_init (reply, &iter);
                                                                                
  if (!dbus_message_iter_append_string (&iter, 
					gconf_database_dbus_get_path (db)))
    g_error ("No memory");

  if (!dbus_connection_send (connection, reply, NULL)) 
    g_error ("No memory");

  dbus_message_unref (reply);
}

static void
server_handle_get_default_db (DBusConnection *connection, 
			      DBusMessage *message)
{
  server_real_handle_get_db (connection, message, NULL);
}

static void
server_handle_get_db (DBusConnection *connection, DBusMessage *message)
{
  char *address;

  if (!gconfd_dbus_get_message_args (connection, message, 
				     DBUS_TYPE_STRING, &address,
				     0))
    return;

  server_real_handle_get_db (connection, message, address);
  dbus_free (address);
}

static void
server_handle_shutdown (DBusConnection *connection, DBusMessage *message)
{
  DBusMessage     *reply;

  if (gconfd_dbus_check_in_shutdown (connection, message))
    return;

  gconf_log(GCL_DEBUG, _("Shutdown request received"));

  gconf_database_dbus_unregister_all ();

  reply = dbus_message_new_method_return (message);
  dbus_connection_send (connection, reply, NULL);
  dbus_message_unref (reply);
  
  dbus_connection_unregister_object_path (connection, server_path);

  gconf_main_quit();
}

static const char *
get_dbus_address (void)
{
  /* Temporary function until we have a session bus */
  return g_getenv ("DBUS_ADDRESS");
}

gboolean
gconfd_dbus_init (void)
{
  DBusError   error;
  const char *bus_address;
  
  bus_address = get_dbus_address ();
  if (!bus_address) 
    {
      gconf_log (GCL_ERR, ("Failed to get the D-BUS bus daemon address"));
      return FALSE;
    }
  
  dbus_error_init (&error);
  bus_conn = dbus_connection_open (get_dbus_address (), &error);
  if (!bus_conn) 
    {
      gconf_log (GCL_ERR, _("Failed to connect to the D-BUS daemon: %s"),
		 error.message);
      return FALSE;
    }

  if (!dbus_bus_register (bus_conn, &error))
    {
      gconf_log (GCL_ERR, _("Failed to register client with the D-BUS bus daemon: %s"),
		 error.message);
      dbus_error_free (&error);
      return FALSE;
    }

  if (!dbus_connection_register_object_path (bus_conn,
					     server_path,
					     &server_vtable,
					     NULL))
    {
      gconf_log (GCL_ERR, _("Failed to register server object with the D-BUS bus daemon"));
      return FALSE;
    }
  
  dbus_bus_acquire_service (bus_conn, GCONF_DBUS_SERVICE, 0, &error);
  if (dbus_error_is_set (&error)) 
    {
      gconf_log (GCL_ERR, _("Failed to acquire gconf service"));
      dbus_error_free (&error);
      return FALSE;
    }

  dbus_connection_setup_with_g_main (bus_conn, NULL);
  
  return TRUE;
}

gboolean 
gconfd_dbus_check_in_shutdown (DBusConnection *connection, DBusMessage *message)
{
  /* FIXME: Copy from andersca's code */
  return FALSE;
}

guint
gconfd_dbus_client_count (void)
{
  /* FIXME: Return 1 if we have the bus running, 
   * if the bus goes down, return 0
   */
  return 1;
}

gboolean
gconfd_dbus_get_message_args (DBusConnection *connection,
			      DBusMessage    *message,
			      int             first_arg_type,
			      ...)
{
  gboolean retval;
  va_list var_args;
                                                                                
  va_start (var_args, first_arg_type);
  retval = dbus_message_get_args_valist (message, NULL, first_arg_type, var_args);
  va_end (var_args);
 
  if (!retval)
    {
      DBusMessage *reply;
       
      reply = dbus_message_new_error (message, GCONF_DBUS_ERROR_FAILED,
				      _("Got a malformed message."));
      dbus_connection_send (connection, reply, NULL);
      dbus_message_unref (reply);
       
      return FALSE;
    }
 
  return TRUE;
}

gboolean 
gconfd_dbus_set_exception (DBusConnection  *connection,
			   DBusMessage     *message,
			   GError         **error)
{
  GConfError en;
  const char *name = NULL;
  DBusMessage *reply;
                                                                                
  if (error == NULL || *error == NULL)
    return FALSE;
                                                                                
  en = (*error)->code;
                                                                                
  /* success is not supposed to get set */
  g_return_val_if_fail(en != GCONF_ERROR_SUCCESS, FALSE);

  switch (en)
    {
    case GCONF_ERROR_FAILED:
      name = GCONF_DBUS_ERROR_FAILED;
      break;
    case GCONF_ERROR_NO_PERMISSION:
      name = GCONF_DBUS_ERROR_NO_PERMISSION;
      break;
    case GCONF_ERROR_BAD_ADDRESS:
      name = GCONF_DBUS_ERROR_BAD_ADDRESS;
      break;
    case GCONF_ERROR_BAD_KEY:
      name = GCONF_DBUS_ERROR_BAD_KEY;
      break;
    case GCONF_ERROR_PARSE_ERROR:
      name = GCONF_DBUS_ERROR_PARSE_ERROR;
      break;
    case GCONF_ERROR_CORRUPT:
      name = GCONF_DBUS_ERROR_CORRUPT;
      break;
    case GCONF_ERROR_TYPE_MISMATCH:
      name = GCONF_DBUS_ERROR_TYPE_MISMATCH;
      break;
    case GCONF_ERROR_IS_DIR:
      name = GCONF_DBUS_ERROR_IS_DIR;
      break;
    case GCONF_ERROR_IS_KEY:
      name = GCONF_DBUS_ERROR_IS_KEY;
      break;
    case GCONF_ERROR_NO_WRITABLE_DATABASE:
      name = GCONF_DBUS_ERROR_NO_WRITABLE_DATABASE;
      break;
    case GCONF_ERROR_IN_SHUTDOWN:
      name = GCONF_DBUS_ERROR_IN_SHUTDOWN;
      break;
    case GCONF_ERROR_OVERRIDDEN:
      name = GCONF_DBUS_ERROR_OVERRIDDEN;
      break;
    case GCONF_ERROR_LOCK_FAILED:
      name = GCONF_DBUS_ERROR_LOCK_FAILED;
      break;
    case GCONF_ERROR_OAF_ERROR:
    case GCONF_ERROR_LOCAL_ENGINE:
    case GCONF_ERROR_NO_SERVER:
    case GCONF_ERROR_SUCCESS:
    default:
      gconf_log (GCL_ERR, "Unhandled error code %d", en);
      g_assert_not_reached();
      break;
    }
                                                                                
  reply = dbus_message_new_error (message, name, (*error)->message);
  dbus_connection_send (connection, reply, NULL);
  dbus_message_unref (reply);
                                                                                
  return TRUE;
}


