#include "dbus-glue.h"
#include <gconf/gconf.h>
#include <dbus/dbus.h>

static const char *
get_dbus_address (void)
{
  /* FIXME: Change this when we know how to find the message bus. */
  return g_getenv ("GCONF_DBUS_ADDRESS");
}

gboolean
setup_dbus (void)
{
  static DBusConnection *dbus_conn = NULL;
  const char *dbus_address;
  DBusResultCode result;
  char *name;
  
  dbus_address = get_dbus_address ();
  if (!dbus_address)
    {
      g_printerr ("Failed to get the D-BUS bus daemon address");
      return FALSE;
    }

  dbus_conn = dbus_connection_open (dbus_address, &result);
  if (!dbus_conn)
    {
      g_printerr ("Failed to connect to the D-BUS bus daemon: %s",
		  dbus_result_to_string (result));
      return FALSE;
    }

  name = dbus_bus_register_client (dbus_conn, &result);
  if (!name)
    {
      g_printerr ("Failed to register client with the D-BUS bus daemon: %s",
		  dbus_result_to_string (result));
      return FALSE;
    }

  dbus_free (name);
  
  return gconf_init_dbus (dbus_conn);
}

