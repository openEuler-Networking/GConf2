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

#ifndef GCONF_DBUS_UTILS_H
#define GCONF_DBUS_UTILS_H

#include <glib.h>
#include <dbus/dbus-glib.h>
#include <gconf/gconf.h>
#include <gconf/gconf-value.h>

#define GCONF_SERVICE_NAME "org.gnome.GConf"
#define GCONF_SERVER_INTERFACE_NAME "org.gnome.GConf.Server"
#define GCONF_DATABASE_INTERFACE_NAME "org.gnome.GConf.Database"

#define FUNC_DB_LOOKUP             "Lookup"
#define FUNC_DB_REQUEST_NOTIFY     "RequestNotify"
#define FUNC_DB_SET                "Set"
#define FUNC_DB_RECURSIVE_UNSET    "RecursiveUnset"
#define FUNC_DB_DIR_EXISTS         "DirExists"
#define FUNC_DB_GET_ALL_ENTRIES    "GetAllEntries"
#define FUNC_DB_GET_ALL_DIRS       "GetAllDirs"
#define FUNC_DB_SET_SCHEMA         "SetSchema"
#define FUNC_SERVER_GET_DEFAULT_DB "GetDefaultDatabase"
#define FUNC_SERVER_GET_DB         "GetDatabase"
#define FUNC_SERVER_SHUTDOWN       "Shutdown"

#define GCONF_DBUS_ERROR_FAILED "org.gnome.GConf.Error.Failed"
#define GCONF_DBUS_ERROR_NO_PERMISSION "org.gnome.GConf.Error.NoPermission"
#define GCONF_DBUS_ERROR_BAD_ADDRESS "org.gnome.GConf.Error.BadAddress"
#define GCONF_DBUS_ERROR_BAD_KEY "org.gnome.GConf.Error.BadKey"
#define GCONF_DBUS_ERROR_PARSE_ERROR "org.gnome.GConf.Error.ParseError"
#define GCONF_DBUS_ERROR_CORRUPT "org.gnome.GConf.Error.Corrupt"
#define GCONF_DBUS_ERROR_TYPE_MISMATCH "org.gnome.GConf.Error.TypeMismatch"
#define GCONF_DBUS_ERROR_IS_DIR "org.gnome.GConf.Error.IsDir"
#define GCONF_DBUS_ERROR_IS_KEY "org.gnome.GConf.Error.IsKey"
#define GCONF_DBUS_ERROR_NO_WRITABLE_DATABASE "org.gnome.GConf.Error.NoWritableDatabase"
#define GCONF_DBUS_ERROR_IN_SHUTDOWN "org.gnome.GConf.Error.InShutdown"
#define GCONF_DBUS_ERROR_OVERRIDDEN "org.gnome.GConf.Error.Overriden"
#define GCONF_DBUS_ERROR_LOCK_FAILED "org.gnome.GConf.Error.LockFailed"


void         gconf_dbus_message_append_gconf_value      (DBusMessage      *message,
							 const GConfValue *value);
GConfValue * gconf_dbus_create_gconf_value_from_message (DBusMessageIter  *iter);


#endif/* GCONF_DBUS_UTILS_H */
