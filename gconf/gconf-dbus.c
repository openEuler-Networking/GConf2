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
#include "gconf-dbus.h"

void
gconf_dbus_fill_message_from_gconf_value (DBusMessage      *message,
					  const GConfValue *value)
{
  if (!value)
    {
      dbus_message_append_nil (message);
      return;
    }

  switch (value->type)
    {
    case GCONF_VALUE_INT:
      dbus_message_append_int32 (message, gconf_value_get_int (value));
      break;
    case GCONF_VALUE_STRING:
      dbus_message_append_string (message, gconf_value_get_string (value));
      break;
    case GCONF_VALUE_FLOAT:
      dbus_message_append_double (message, gconf_value_get_float (value));
      break;
    case GCONF_VALUE_BOOL:
      dbus_message_append_boolean (message, gconf_value_get_bool (value));
      break;
    case GCONF_VALUE_INVALID:
      dbus_message_append_nil (message);
      break;
    default:
      g_error ("unsupported gconf value type %d", value->type);
    }
}
