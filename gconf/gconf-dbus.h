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

#define GCONF_DBUS_CONFIG_SERVER "org.freedesktop.Config.Server"

#define GCONF_DBUS_CONFIG_SERVER_SHUTDOWN "org.freedesktop.Config.Server.Shutdown"

#define GCONF_DBUS_CONFIG_DATABASE_DIR_EXISTS "org.freedesktop.Config.Database.DirExists"
#define GCONF_DBUS_CONFIG_DATABASE_ALL_DIRS "org.freedesktop.Config.Database.AllDirs"
#define GCONF_DBUS_CONFIG_DATABASE_ALL_ENTRIES "org.freedesktop.Config.Database.AllEntries"
#define GCONF_DBUS_CONFIG_DATABASE_LOOKUP "org.freedesktop.Config.Database.Lookup"


GConfValue *gconf_dbus_create_gconf_value_from_message (DBusMessageIter *iter);
void gconf_dbus_fill_message_from_gconf_value (DBusMessage      *message,
					       const GConfValue *value);

#endif
