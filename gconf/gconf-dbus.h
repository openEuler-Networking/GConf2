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

#ifndef GCONF_GCONF_DBUS_H
#define GCONF_GCONF_DBUS_H

#include <dbus/dbus-glib.h>
#include "gconf.h"

#define GCONF_DBUS_CONFIG_SERVER "org.gnome.GConf.Server"

#define GCONF_DBUS_CONFIG_SERVER_SHUTDOWN "org.gnome.GConf.Server.Shutdown"

#define GCONF_DBUS_CONFIG_DATABASE_DIR_EXISTS "org.gnome.GConf.Database.DirExists"
#define GCONF_DBUS_CONFIG_DATABASE_ALL_DIRS "org.gnome.GConf.Database.AllDirs"
#define GCONF_DBUS_CONFIG_DATABASE_ALL_ENTRIES "org.gnome.GConf.Database.AllEntries"
#define GCONF_DBUS_CONFIG_DATABASE_LOOKUP "org.gnome.GConf.Database.Lookup"
#define GCONF_DBUS_CONFIG_DATABASE_SET "org.gnome.GConf.Database.Set"
#define GCONF_DBUS_CONFIG_DATABASE_UNSET "org.gnome.GConf.Database.Unset"
#define GCONF_DBUS_CONFIG_DATABASE_REMOVE_DIR "org.gnome.GConf.Database.RemoveDir"
#define GCONF_DBUS_CONFIG_DATABASE_LOOKUP_DEFAULT_VALUE "org.gnome.GConf.Database.LookupDefaultValue"
#define GCONF_DBUS_CONFIG_DATABASE_ADD_LISTENER "org.gnome.GConf.Database.AddListener"
#define GCONF_DBUS_CONFIG_DATABASE_REMOVE_LISTENER "org.gnome.GConf.Database.RemoveListener"
#define GCONF_DBUS_CONFIG_DATABASE_RECURSIVE_UNSET "org.gnome.GConf.Database.RecursiveUnset"
#define GCONF_DBUS_CONFIG_DATABASE_SET_SCHEMA "org.gnome.GConf.Database.SetSchema"
#define GCONF_DBUS_CONFIG_DATABASE_SYNC "org.gnome.GConf.Database.Sync"
#define GCONF_DBUS_CONFIG_DATABASE_CLEAR_CACHE "org.gnome.GConf.Database.ClearCache"
#define GCONF_DBUS_CONFIG_DATABASE_SYNCHRONOUS_SYNC "org.gnome.GConf.Database.Synchronous.Sync"

#define GCONF_DBUS_CONFIG_LISTENER_NOTIFY "org.gnome.GConf.Listener.Notify"

#define GCONF_DBUS_UNSET_INCLUDING_SCHEMA_NAMES 0x1

GConfValue *gconf_dbus_create_gconf_value_from_message (DBusMessageIter *iter);
void gconf_dbus_fill_message_from_gconf_value (DBusMessage      *message,
					       const GConfValue *value);

#endif
