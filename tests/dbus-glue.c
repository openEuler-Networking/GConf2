#include "dbus-glue.h"
#include <gconf/gconf.h>
#include <dbus/dbus.h>

static const char *
get_dbus_address (void)
{
  /* FIXME: Change this when we know how to find the message bus. */
  return g_getenv ("DBUS_ADDRESS");
}

gboolean
setup_dbus (void)
{
  static DBusConnection *dbus_conn = NULL;
  const char *dbus_address;
  DBusResultCode result;
  DBusError error;
  
  dbus_address = get_dbus_address ();
  if (!dbus_address)
    {
      g_printerr ("Failed to get the D-BUS bus daemon address.\n");
      return FALSE;
    }

  dbus_error_init (&error);
  dbus_conn = dbus_connection_open (dbus_address, &result);
  if (!dbus_conn)
    {
      g_printerr ("Failed to connect to the D-BUS bus daemon: %s\n",
		  dbus_result_to_string (result));
      return FALSE;
    }

  dbus_error_init (&error);  
  if (!dbus_bus_register (dbus_conn, &error))
    {
      g_printerr ("Failed to register client with the D-BUS bus daemon: %s\n",
		  error.message);
      return FALSE;
    }

  return gconf_init_dbus (dbus_conn);
}

