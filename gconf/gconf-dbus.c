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

GConfValue *
gconf_dbus_create_gconf_value_from_message (DBusMessageIter *iter)
{
  int arg_type;
  GConfValue *gval;
  GConfValueType type = GCONF_VALUE_INVALID;
  
  arg_type = dbus_message_iter_get_arg_type (iter);

  switch (arg_type)
    {
    case DBUS_TYPE_NIL:
      return NULL;
    case DBUS_TYPE_STRING:
      type = GCONF_VALUE_STRING;
      break;
    default:
      g_error ("unsupported arg type %d\n",
	       arg_type);

    }

  g_assert(GCONF_VALUE_TYPE_VALID(type));

  gval = gconf_value_new(type);

  switch (gval->type)
    {
    case GCONF_VALUE_STRING:
      {
	char *str;
	str = dbus_message_iter_get_string (iter);

	gconf_value_set_string (gval, str);
	dbus_free (str);
	break;
      }
    default:
      g_assert_not_reached ();
      break;
    }

  return gval;
}
