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

static void
list_entries (GConfClient *client, char *dir, int indent)
{
  GSList *pairs, *list;
  gchar* whitespace;
  
  pairs = gconf_client_all_entries(client, dir, NULL);

  list = pairs;
  whitespace = g_strnfill(indent, ' ');

  while (list != NULL)
    {
      GConfEntry* pair = list->data;
      gchar* s;
      
      if (gconf_entry_get_value (pair))
	s = gconf_value_to_string (gconf_entry_get_value (pair));
      else
	s = g_strdup("(no value set)");
      
      g_print(" %s%s = %s\n", whitespace,
	      gconf_entry_get_key (pair),
	      s);

      g_free (s);
      gconf_entry_free(pair);
      
      list = list->next;
    }

  if (pairs)
    g_slist_free (pairs);
  g_free (whitespace);
}

static void
recurse (GConfClient *client, char *dir, int indent)
{
  GSList *dirs, *list;

  dirs = gconf_client_all_dirs (client, dir, NULL);

  list = dirs;
  while (list)
    {
      int i;
      for (i = 0; i < indent; i++)
	g_print (" ");
      g_print ("%s\n", list->data);
      list_entries (client, list->data, indent + 1);
      recurse (client, list->data, indent + 1);
      g_free (list->data);
      list = list->next;
    }
  g_slist_free (dirs);
}

static void
notify_func (GConfClient* client,
	     guint cnxn_id,
	     GConfEntry *entry,
	     gpointer user_data)
{
  char *s;
  
  if (gconf_entry_get_value (entry))
    s = gconf_value_to_string (gconf_entry_get_value (entry));

  gconf_client_unset (client, gconf_entry_get_key (entry), NULL);
  printf ("key changed: %s to %s\n", gconf_entry_get_key (entry), s);
  g_free (s);
}

static gboolean
idle_func (GConfClient *client)
{
  gconf_client_set_string (client, "/desktop/gnome/interface/icon_theme", "Anders rocks", NULL);
  return FALSE;
}

int
main (int argc, char **argv)
{
  GConfClient *client;
  DBusConnection *connection;
  DBusResultCode result;
  const char *address;
  char *name;
  GMainLoop *loop;
  
  
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
  g_idle_add (idle_func, client);

  gconf_client_add_dir (client, "/desktop/gnome",
			GCONF_CLIENT_PRELOAD_ONELEVEL, NULL);

  gconf_client_notify_add (client, "/desktop/gnome",
			   notify_func, NULL, NULL, NULL);
  loop = g_main_loop_new (NULL, FALSE);
  
  dbus_connection_setup_with_g_main (connection);
  
  g_main_loop_run (loop);

#if 0
  g_print ("foo: %s\n", gconf_client_get_string (client, "/desktop/gnome/interface/icon_theme", NULL));

  g_print ("foo: %d\n", gconf_client_dir_exists (client, "/desktop/gnome/interface", NULL));

  recurse (client, "/", 0);
#endif  
  return 0;
}
