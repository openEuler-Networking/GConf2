/* GConf
 * Copyright (C) 2003 CodeFactory AB
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
#include <gconf/gconf-client.h>

int
main (int argc, char **argv)
{
  GConfClient *client;
  DBusConnection *connection;
  DBusResultCode result;
  const char *address;
  char *name;

  address = g_getenv ("GCONF_DBUS_ADDRESS");  
  connection = dbus_connection_open (address, &result);

  if (!connection)
    {
      g_printerr ("Failed to connect to the D-BUS bus daemon: %s",
		  dbus_result_to_string (result));
      return 1;
    }
  
  name = dbus_bus_register_client (connection, &result);
  if (!name)
    {
      g_printerr ("Failed to register client with the D-BUS bus daemon: %s",
		  dbus_result_to_string (result));
      return 1;
    }

  g_type_init ();
  
  gconf_init_dbus (connection);
  
  client = gconf_client_get_default ();

  g_print ("foo: %s\n", gconf_client_get_string (client, "/desktop/gnome/interface/icon_theme", NULL));

  return 0;
}
