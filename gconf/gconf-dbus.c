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
#include "gconf-internals.h"

static GConfValue *
gconf_value_from_dict_value (DBusDict   *dict,
			     const char *key)
{
  GConfValue *value;
  
  switch (dbus_dict_get_value_type (dict, key))
    {
    case DBUS_TYPE_STRING:
      {
	const char *str;
	value = gconf_value_new (GCONF_VALUE_STRING);

	dbus_dict_get_string (dict, key, &str);
	gconf_value_set_string (value, str);

	return value;
      }
    case DBUS_TYPE_INT32:
      {
	dbus_int32_t val;
	
	value = gconf_value_new (GCONF_VALUE_INT);

	dbus_dict_get_int32 (dict, key, &val);
	gconf_value_set_int (value, val);

	return value;
      }
    case DBUS_TYPE_DOUBLE:
      {
	double val;
	
	value = gconf_value_new (GCONF_VALUE_FLOAT);

	dbus_dict_get_double (dict, key, &val);
	gconf_value_set_float (value, val);

	return value;
      }
    case DBUS_TYPE_BOOLEAN:
      {
	dbus_bool_t val;
	
	value = gconf_value_new (GCONF_VALUE_BOOL);

	dbus_dict_get_boolean (dict, key, &val);
	gconf_value_set_bool (value, val);

	return value;
      }
    default:
      return NULL;
    }
}

static void
set_dict_value_from_gconf_value (DBusDict   *dict,
				 const char *key,
				 GConfValue *value)
{
  switch (value->type)
    {
    case GCONF_VALUE_STRING:
      dbus_dict_set_string (dict, key, gconf_value_get_string (value));
      break;
    case GCONF_VALUE_INT:
      dbus_dict_set_int32 (dict, key, gconf_value_get_int (value));
      break;
    case GCONF_VALUE_FLOAT:
      dbus_dict_set_double (dict, key, gconf_value_get_float (value));
      break;
    case GCONF_VALUE_BOOL:
      dbus_dict_set_boolean (dict, key, gconf_value_get_bool (value));
      break;
    default:
      g_assert_not_reached ();
    }
}

     
static void
gconf_dbus_fill_message_from_gconf_schema (DBusMessage       *message,
					   const GConfSchema *schema)
{
  DBusDict *dict;
  GConfValue* default_val;

  dict = dbus_dict_new ();

  dbus_dict_set_int32 (dict, "type", gconf_schema_get_type (schema));
  dbus_dict_set_int32 (dict, "list_type", gconf_schema_get_list_type (schema));
  dbus_dict_set_int32 (dict, "car_type", gconf_schema_get_car_type (schema));
  dbus_dict_set_int32 (dict, "cdr_type", gconf_schema_get_cdr_type (schema));

  dbus_dict_set_string (dict, "locale", gconf_schema_get_locale (schema) ? gconf_schema_get_locale (schema) : "");
  dbus_dict_set_string (dict, "short_desc", gconf_schema_get_short_desc (schema) ? gconf_schema_get_short_desc (schema) : "");
  dbus_dict_set_string (dict, "long_desc", gconf_schema_get_long_desc (schema) ? gconf_schema_get_long_desc (schema) : "");
  dbus_dict_set_string (dict, "owner", gconf_schema_get_owner (schema) ? gconf_schema_get_owner (schema) : "");
  
  default_val = gconf_schema_get_default_value (schema);
  
  /* We don't need to do this, but it's much simpler */
  if (default_val)
    {
      char *encoded = gconf_value_encode (default_val);
      g_assert (encoded != NULL);
      dbus_dict_set_string (dict, "default_value", encoded);
      g_free (encoded);
    }

  dbus_message_append_dict (message, dict);
  dbus_dict_unref (dict);
}

GConfValue *
gconf_dbus_create_gconf_value_from_dict (DBusDict *dict)
{
  GConfValue *value;
  
  /* Check if we've got a schema dict */
  if (dbus_dict_contains (dict, "type"))
    {
      dbus_int32_t type, list_type, car_type, cdr_type;
      const char *tmp;
      GConfSchema *schema;
      
      dbus_dict_get_int32 (dict, "type", &type);
      dbus_dict_get_int32 (dict, "list_type", &list_type);
      dbus_dict_get_int32 (dict, "car_type", &car_type);
      dbus_dict_get_int32 (dict, "cdr_type", &cdr_type);

      schema = gconf_schema_new ();

      gconf_schema_set_type (schema, type);
      gconf_schema_set_list_type (schema, type);
      gconf_schema_set_car_type (schema, type);
      gconf_schema_set_cdr_type (schema, type);
      
      value = gconf_value_new (GCONF_VALUE_SCHEMA);
      gconf_value_set_schema_nocopy (value, schema);

      dbus_dict_get_string (dict, "locale", &tmp);
      if (*tmp != '\0')
	gconf_schema_set_locale (schema, tmp);

      dbus_dict_get_string (dict, "short_desc", &tmp);
      if (*tmp != '\0')
	gconf_schema_set_short_desc (schema, tmp);

      dbus_dict_get_string (dict, "long_desc", &tmp);
      if (*tmp != '\0')
	gconf_schema_set_long_desc (schema, tmp);

      dbus_dict_get_string (dict, "owner", &tmp);
      if (*tmp != '\0')
	gconf_schema_set_owner (schema, tmp);
      
      dbus_dict_get_string (dict, "default_value", &tmp);
      {
	GConfValue *val;

	val = gconf_value_decode (tmp);

	if (val)
	  gconf_schema_set_default_value_nocopy (schema, val);
      }

      return value;

    }
  else if (dbus_dict_contains (dict, "car"))
    {
      value = gconf_value_new (GCONF_VALUE_PAIR);

      gconf_value_set_car_nocopy (value, gconf_value_from_dict_value (dict, "car"));
      gconf_value_set_cdr_nocopy (value, gconf_value_from_dict_value (dict, "cdr"));

      return value;
    }
  
  return NULL;
}

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
    case GCONF_VALUE_LIST:
      {
	guint i, len;
        GSList* list;

        list = gconf_value_get_list(value);
	
        len = g_slist_length(list);

	switch (gconf_value_get_list_type (value))
	  {
	  case GCONF_VALUE_STRING:
	    {
	      char **str;

	      str = g_new (char *, len + 1);
	      str[len] = NULL;

	      i = 0;
	      while (list)
		{
		  GConfValue *value = list->data;

		  str[i] = g_strdup (gconf_value_get_string (value));
		    
		  list = list->next;
		  ++i;
		}

	      dbus_message_append_string_array (message, (const char **)str, len);
	      g_strfreev (str);
	      break;
	    }
	  case GCONF_VALUE_INT:
	    {
	      int *array;

	      array = g_new (dbus_int32_t, len);

	      i = 0;
	      while (list)
		{
		  GConfValue *value = list->data;
		  
		  array[i] = gconf_value_get_int (value);
		    
		  list = list->next;
		  ++i;
		}

	      dbus_message_append_int32_array (message, array, len);
	      g_free (array);
	      break;
	    }
	  case GCONF_VALUE_FLOAT:
	    {
	      double *array;

	      array = g_new (double, len);

	      i = 0;
	      while (list)
		{
		  GConfValue *value = list->data;
		  
		  array[i] = gconf_value_get_float (value);
		    
		  list = list->next;
		  ++i;
		}

	      dbus_message_append_double_array (message, array, len);
	      g_free (array);
	      break;
	    }
	  case GCONF_VALUE_BOOL:
	    {
	      unsigned char *array;

	      array = g_new (unsigned char, len);

	      i = 0;
	      while (list)
		{
		  GConfValue *value = list->data;
		  
		  array[i] = gconf_value_get_bool (value);
		    
		  list = list->next;
		  ++i;
		}

	      dbus_message_append_boolean_array (message, array, len);
	      g_free (array);
	      break;
	    }
	  default:
	    g_error ("unsupported gconf list value type %d", gconf_value_get_list_type (value));
	  }
	break;	
      }
    case GCONF_VALUE_PAIR:
      {
	DBusDict *dict;

	dict = dbus_dict_new ();
	set_dict_value_from_gconf_value (dict, "car", gconf_value_get_car (value));
	set_dict_value_from_gconf_value (dict, "cdr", gconf_value_get_cdr (value));

	dbus_message_append_dict (message, dict);
	dbus_dict_unref (dict);
	break;
      }
    case GCONF_VALUE_SCHEMA:
      gconf_dbus_fill_message_from_gconf_schema (message, gconf_value_get_schema (value));
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
    case DBUS_TYPE_BOOLEAN:
      type = GCONF_VALUE_BOOL;
      break;
    case DBUS_TYPE_INT32:
      type = GCONF_VALUE_INT;
      break;
    case DBUS_TYPE_DOUBLE:
      type = GCONF_VALUE_FLOAT;
      break;
    case DBUS_TYPE_STRING:
      type = GCONF_VALUE_STRING;
      break;
    case DBUS_TYPE_INT32_ARRAY:
    case DBUS_TYPE_STRING_ARRAY:
    case DBUS_TYPE_DOUBLE_ARRAY:
    case DBUS_TYPE_BOOLEAN_ARRAY:
      type = GCONF_VALUE_LIST;
      break;
    case DBUS_TYPE_DICT:
      {
	DBusDict *dict;
	GConfValue *v;
	dbus_message_iter_get_dict (iter, &dict);

	v = gconf_dbus_create_gconf_value_from_dict (dict);
	dbus_dict_unref (dict);
	return v;
      }
      break;
    default:
      g_error ("unsupported arg type %d\n",
	       arg_type);

    }

  g_assert(GCONF_VALUE_TYPE_VALID(type));

  gval = gconf_value_new(type);

  switch (gval->type)
    {
    case GCONF_VALUE_BOOL:
      gconf_value_set_bool (gval, dbus_message_iter_get_boolean (iter));
      break;
    case GCONF_VALUE_INT:
      gconf_value_set_int (gval, dbus_message_iter_get_int32 (iter));
      break;
    case GCONF_VALUE_FLOAT:
      gconf_value_set_float (gval, dbus_message_iter_get_double (iter));
      break;
    case GCONF_VALUE_STRING:
      {
	char *str;
	str = dbus_message_iter_get_string (iter);

	gconf_value_set_string (gval, str);
	dbus_free (str);
	break;
      }
    case GCONF_VALUE_LIST:
      {
	GSList *list = NULL;
	guint i = 0;
	
	switch (arg_type)
	  {
	  case DBUS_TYPE_BOOLEAN_ARRAY:
	    {
	      unsigned char *array;
	      int len;

	      dbus_message_iter_get_boolean_array (iter, &array, &len);

	      for (i = 0; i < len; i++)
		{
		  GConfValue *value = gconf_value_new (GCONF_VALUE_BOOL);
		  
		  gconf_value_set_bool (value, array[i]);
		  
		  list = g_slist_prepend (list, value);
		}
	      list = g_slist_reverse (list);
	      dbus_free (array);

	      gconf_value_set_list_type (gval, GCONF_VALUE_BOOL);
	      gconf_value_set_list_nocopy (gval, list);
	      break;
	    }
	  case DBUS_TYPE_DOUBLE_ARRAY:
	    {
	      double *array;
	      int len;

	      dbus_message_iter_get_double_array (iter, &array, &len);

	      for (i = 0; i < len; i++)
		{
		  GConfValue *value = gconf_value_new (GCONF_VALUE_FLOAT);
		  
		  gconf_value_set_float (value, array[i]);
		  
		  list = g_slist_prepend (list, value);
		}
	      list = g_slist_reverse (list);
	      dbus_free (array);

	      gconf_value_set_list_type (gval, GCONF_VALUE_FLOAT);
	      gconf_value_set_list_nocopy (gval, list);
	      break;
	    }
	  case DBUS_TYPE_INT32_ARRAY:
	    {
	      dbus_int32_t *array;
	      int len;

	      dbus_message_iter_get_int32_array (iter, &array, &len);

	      for (i = 0; i < len; i++)
		{
		  GConfValue *value = gconf_value_new (GCONF_VALUE_INT);
		  
		  gconf_value_set_int (value, array[i]);
		  
		  list = g_slist_prepend (list, value);
		}
	      list = g_slist_reverse (list);
	      dbus_free (array);

	      gconf_value_set_list_type (gval, GCONF_VALUE_INT);
	      gconf_value_set_list_nocopy (gval, list);
	      break;
	    }
	  case DBUS_TYPE_STRING_ARRAY:
	    {
	      char **array;
	      int len;
	      
	      dbus_message_iter_get_string_array (iter, &array, &len);
	      
	      for (i = 0; i < len; i++)
		{
		  GConfValue *value = gconf_value_new (GCONF_VALUE_STRING);
		  
		  gconf_value_set_string (value, array[i]);
		  
		  list = g_slist_prepend (list, value);
		}
	      list = g_slist_reverse (list);
	      dbus_free_string_array (array);

	      gconf_value_set_list_type (gval, GCONF_VALUE_STRING);
	      gconf_value_set_list_nocopy (gval, list);
	      break;
	    }
	  default:
	    g_error ("unknown list arg type %d", arg_type);
	  }
	break;
      }
    default:
      g_assert_not_reached ();
      break;
    }

  return gval;
}
