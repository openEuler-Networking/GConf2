/* -*- mode: C; c-file-style: "gnu" -*- */

/* GConf
 * Copyright (C) 1999, 2000 Red Hat Inc.
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

#include <popt.h>
#include "gconf.h"
#include "gconf-dbus-utils.h"
#include "gconf-internals.h"
#include "gconf-sources.h"
#include "gconf-locale.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <unistd.h>

#include <dbus/dbus.h>

/* Maximum number of times to try re-spawning the server if it's down. */
#define MAX_RETRIES 1

struct _GConfEngine {
  guint refcount;

  gchar *database;

  /* Unused for now. */
  GHashTable *notify_ids;

  /* If non-NULL, this is a local engine;
     local engines don't do notification! */
  GConfSources* local_sources;
  
  /* An address if this is not the default engine;
   * NULL if it's the default
   */
  gchar *address;

  gpointer user_data;
  GDestroyNotify dnotify;

  gpointer owner;
  int owner_use_count;
  
  guint is_default : 1;

  /* If TRUE, this is a local engine (and therefore
   * has no ctable and no notifications)
   */
  guint is_local : 1;
};

struct _GConfCnxn {
  gchar* namespace_section;
  guint client_id;

  GConfEngine* conf;             /* engine we're associated with */
  GConfNotifyFunc func;
  gpointer user_data;
};

/* Object path for D-BUS GConf server */
static const gchar *server = NULL;

static GConfEngine *default_engine = NULL;

static GHashTable  *engines_by_db = NULL;
static GHashTable  *engines_by_address = NULL;


static const gchar *gconf_get_config_server     (gboolean      start_if_not_found,
						 GError      **err);
static void         gconf_engine_detach         (GConfEngine  *conf);
static gboolean     gconf_engine_connect        (GConfEngine  *conf,
						 gboolean      start_if_not_found,
						 GError      **err);
static void         gconf_engine_set_database   (GConfEngine  *conf,
						 const gchar  *db);
static const gchar *gconf_engine_get_database   (GConfEngine  *conf,
						 gboolean      start_if_not_found,
						 GError      **err);
static void         register_engine             (GConfEngine  *conf);
static void         unregister_engine           (GConfEngine  *conf);
static GConfEngine *lookup_engine               (const gchar  *address);
static GConfEngine *lookup_engine_by_database   (const gchar  *db);
static gboolean     gconf_handle_dbus_exception (DBusMessage  *message,
						 GError      **err);
static gboolean     gconf_server_broken         (DBusMessage  *message);
static void         gconf_detach_config_server  (void);




#define CHECK_OWNER_USE(engine)   \
  do { if ((engine)->owner && (engine)->owner_use_count == 0) \
     g_warning ("%s: You can't use a GConfEngine that has an active GConfClient wrapper object. Use GConfClient API instead.", G_GNUC_FUNCTION);  \
  } while (0)



static GConfError
dbus_error_name_to_gconf_errno (const char *name)
{
  int i;
  struct
  {
    const char *name;
    GConfError error;
  } errors [] = {
    { GCONF_DBUS_ERROR_FAILED, GCONF_ERROR_FAILED },
    { GCONF_DBUS_ERROR_NO_PERMISSION, GCONF_ERROR_NO_PERMISSION },
    { GCONF_DBUS_ERROR_BAD_ADDRESS, GCONF_ERROR_BAD_ADDRESS },
    { GCONF_DBUS_ERROR_BAD_KEY, GCONF_ERROR_BAD_KEY },
    { GCONF_DBUS_ERROR_PARSE_ERROR, GCONF_ERROR_PARSE_ERROR },
    { GCONF_DBUS_ERROR_CORRUPT, GCONF_ERROR_CORRUPT },
    { GCONF_DBUS_ERROR_TYPE_MISMATCH, GCONF_ERROR_TYPE_MISMATCH },
    { GCONF_DBUS_ERROR_IS_DIR, GCONF_ERROR_IS_DIR },
    { GCONF_DBUS_ERROR_IS_KEY, GCONF_ERROR_IS_KEY },
    { GCONF_DBUS_ERROR_OVERRIDDEN, GCONF_ERROR_OVERRIDDEN },
    { GCONF_DBUS_ERROR_LOCK_FAILED, GCONF_ERROR_LOCK_FAILED },
    { GCONF_DBUS_ERROR_NO_WRITABLE_DATABASE, GCONF_ERROR_NO_WRITABLE_DATABASE },
    { GCONF_DBUS_ERROR_IN_SHUTDOWN, GCONF_ERROR_IN_SHUTDOWN },
  };

  for (i = 0; i < G_N_ELEMENTS (errors); i++)
    {
      if (strcmp (name, errors[i].name) == 0)
	return errors[i].error;
    }

  g_assert_not_reached ();
  
  return GCONF_ERROR_SUCCESS;
}

/* Just returns TRUE if there's an exception indicating the server is probably
 * hosed; no side effects.
 */
static gboolean
gconf_server_broken (DBusMessage *message)
{
  const char *name;

  /* A server that doesn't reply is a broken server. */
  if (!message)
    {
      /* XXX: should listen to the signal instead, daemon_running = FALSE; */
      return TRUE;
    }
  
  if (dbus_message_get_type (message) != DBUS_MESSAGE_TYPE_ERROR)
    return FALSE;

  name = dbus_message_get_member (message);
  
  if (g_str_has_prefix (name, "org.freedesktop.DBus.Error"))
    {
      /* XXX: listen to signal... daemon_running = FALSE; */
      return TRUE;
    }
  else if (strcmp (name, GCONF_DBUS_ERROR_IN_SHUTDOWN) != 0)
    return TRUE;
  else
    return FALSE;
}

/* Returns TRUE if there was an error, frees dbus_error and sets err. */
static gboolean
gconf_handle_dbus_exception (DBusMessage *message, GError** err)
{
  char *error_string;
  const char *name;
  
  if (dbus_message_get_type (message) != DBUS_MESSAGE_TYPE_ERROR)
    return FALSE;

  name = dbus_message_get_member (message);

  dbus_message_get_args (message, NULL,
			 DBUS_TYPE_STRING, &error_string,
			 0);
  
  if (g_str_has_prefix (name, "org.freedesktop.DBus.Error"))
    {
      if (err)
	*err = gconf_error_new (GCONF_ERROR_NO_SERVER, _("D-BUS error: %s"),
				error_string);
    }
  else if (g_str_has_prefix (name, "org.GConf.Error"))
    {
      if (err)
	{
	  GConfError en;
	  
	  en = dbus_error_name_to_gconf_errno (name);

	  *err = gconf_error_new (en, error_string);
	}
    }
  else
    {
      if (err)
	*err = gconf_error_new (GCONF_ERROR_FAILED, _("Unknown error %s: %s"),
				name, error_string);
    }
  
  return TRUE;
}

static GConfEngine*
gconf_engine_blank (gboolean remote)
{
  GConfEngine* conf;

  _gconf_init_i18n ();
  
  conf = g_new0(GConfEngine, 1);

  conf->refcount = 1;
  
  conf->owner = NULL;
  conf->owner_use_count = 0;
  
  if (remote)
    {
      conf->database = NULL;
      conf->notify_ids = g_hash_table_new (NULL, NULL); // XXX: use the right equal/hash
      conf->local_sources = NULL;
      conf->is_local = FALSE;
      conf->is_default = TRUE;
    }
  else
    {
      conf->database = NULL;
      conf->notify_ids = NULL;
      conf->local_sources = NULL;
      conf->is_local = TRUE;
      conf->is_default = FALSE;
    }
  
  return conf;
}

void
gconf_engine_set_owner (GConfEngine *engine,
                        gpointer     client)
{
  g_return_if_fail (engine->owner_use_count == 0);
  
  engine->owner = client;
}

void
gconf_engine_push_owner_usage (GConfEngine *engine,
                               gpointer     client)
{
  g_return_if_fail (engine->owner == client);

  engine->owner_use_count += 1;
}

void
gconf_engine_pop_owner_usage  (GConfEngine *engine,
                               gpointer     client)
{
  g_return_if_fail (engine->owner == client);
  g_return_if_fail (engine->owner_use_count > 0);

  engine->owner_use_count -= 1;
}

static GConfEngine *
lookup_engine_by_database (const gchar *db)
{
  if (engines_by_db)
    return g_hash_table_lookup (engines_by_db, db);
  else
    return NULL;
}

static void
database_hash_value_destroy (gpointer value)
{
  GConfEngine *conf = value;

  g_free (conf->database);
  conf->database = NULL;
}

/* This takes ownership of the ConfigDatabase */
static void
gconf_engine_set_database (GConfEngine *conf,
                           const gchar *db)
{
  gconf_engine_detach (conf);

  conf->database = g_strdup (db);

  if (engines_by_db == NULL)
    engines_by_db = g_hash_table_new_full (g_str_hash,
					   g_str_equal,
					   NULL,
					   database_hash_value_destroy);
  
  g_hash_table_insert (engines_by_db, conf->database, conf);  
}

static void
gconf_engine_detach (GConfEngine *conf)
{
  if (conf->database != NULL)
    {
      g_hash_table_remove (engines_by_db, conf->database);
    }
}

static gboolean
gconf_engine_connect (GConfEngine *conf,
                      gboolean start_if_not_found,
                      GError **err)
{
  const gchar * cs;
  const gchar *db; /* XXX: const? */
  int tries = 0;
      
  g_return_val_if_fail (!conf->is_local, TRUE);

  if (conf->database != NULL)
    return TRUE;
  
 RETRY:
      
  cs = gconf_get_config_server (start_if_not_found, err);
      
  if (cs == NULL)
    return FALSE; /* Error should already be set */

  if (conf->is_default)
    db = NULL; /* XXX: const gchar *_get_default_database (cs, &ev); */
  else
    db = NULL; /* XXX: const gchar *_get_database (cs, conf->address, &ev); */

  if (gconf_server_broken (NULL)) // XXX
    {
      if (tries < MAX_RETRIES)
        {
          ++tries;
          gconf_detach_config_server ();
          goto RETRY;
        }
    }
  
  if (gconf_handle_dbus_exception (NULL, err)) /* XXX: message */
    return FALSE;

  if (db == NULL)
    {
      if (err)
        *err = gconf_error_new(GCONF_ERROR_BAD_ADDRESS,
                               _("Server couldn't resolve the address `%s'"),
                               conf->address ? conf->address : "default");
          
      return FALSE;
    }

  gconf_engine_set_database (conf, db);
  
  return TRUE;
}

static const gchar *
gconf_engine_get_database (GConfEngine *conf,
                           gboolean start_if_not_found,
                           GError **err)
{
  if (!gconf_engine_connect (conf, start_if_not_found, err))
    return NULL;
  else
    return conf->database;
}

static gboolean
gconf_engine_is_local(GConfEngine* conf)
{
  return conf->is_local;
}

static void
register_engine (GConfEngine *conf)
{
  g_return_if_fail (conf->address != NULL);

  if (engines_by_address == NULL)
    engines_by_address = g_hash_table_new (g_str_hash, g_str_equal);

  g_hash_table_insert (engines_by_address, conf->address, conf);
}

static void
unregister_engine (GConfEngine *conf)
{
  g_return_if_fail (conf->address != NULL);
  g_return_if_fail (engines_by_address != NULL);
  
  g_hash_table_remove (engines_by_address, conf->address);

  if (g_hash_table_size (engines_by_address) == 0)
    {
      g_hash_table_destroy (engines_by_address);
      
      engines_by_address = NULL;
    }
}

static GConfEngine *
lookup_engine (const gchar *address)
{
  if (engines_by_address)
    return g_hash_table_lookup (engines_by_address, address);
  else
    return NULL;
}


/*
 *  Public Interface
 */

GConfEngine*
gconf_engine_get_local      (const gchar* address,
                             GError** err)
{
  GConfEngine* conf;
  GConfSource* source;

  g_return_val_if_fail(address != NULL, NULL);
  g_return_val_if_fail(err == NULL || *err == NULL, NULL);
  
  source = gconf_resolve_address(address, err);

  if (source == NULL)
    return NULL;
  
  conf = gconf_engine_blank(FALSE);

  conf->local_sources = gconf_sources_new_from_source(source);

  g_assert (gconf_engine_is_local (conf));
  
  return conf;
}

GConfEngine*
gconf_engine_get_default (void)
{
  GConfEngine* conf = NULL;
  
  if (default_engine)
    conf = default_engine;

  if (conf == NULL)
    {
      conf = gconf_engine_blank(TRUE);

      conf->is_default = TRUE;

      default_engine = conf;
      
      /* Ignore errors, we never return a NULL default database, and
       * since we aren't starting if it isn't found, we'll probably
       * get errors half the time anyway.
       */
      gconf_engine_connect (conf, FALSE, NULL);
    }
  else
    conf->refcount += 1;
  
  return conf;
}

GConfEngine*
gconf_engine_get_for_address (const gchar* address, GError** err)
{
  GConfEngine* conf;

  conf = lookup_engine (address);

  if (conf == NULL)
    {
      conf = gconf_engine_blank(TRUE);

      conf->is_default = FALSE;
      conf->address = g_strdup (address);

      if (!gconf_engine_connect (conf, TRUE, err))
        {
          gconf_engine_unref (conf);
          return NULL;
        }

      register_engine (conf);
    }
  else
    conf->refcount += 1;
  
  return conf;
}

void
gconf_engine_ref(GConfEngine* conf)
{
  g_return_if_fail(conf != NULL);
  g_return_if_fail(conf->refcount > 0);

  conf->refcount += 1;
}

void         
gconf_engine_unref(GConfEngine* conf)
{
  g_return_if_fail(conf != NULL);
  g_return_if_fail(conf->refcount > 0);

  conf->refcount -= 1;
  
  if (conf->refcount == 0)
    {
      if (gconf_engine_is_local(conf))
        {
          if (conf->local_sources != NULL)
            gconf_sources_free(conf->local_sources);
        }
      else
        {
          /* Remove all connections associated with this GConf */

	  /* FIXME: remove notify_ids from hash. */

          if (conf->dnotify)
            {
              (* conf->dnotify) (conf->user_data);
            }
          
          /* do this after removing the notifications,
             to avoid funky race conditions */
          if (conf->address)
            unregister_engine (conf);

          /* Release the ConfigDatabase */
          gconf_engine_detach (conf);

          if (conf->notify_ids)
	    g_hash_table_destroy (conf->notify_ids);
        }
      
      if (conf == default_engine)
        default_engine = NULL;
      
      g_free(conf);
    }
}

void
gconf_engine_set_user_data  (GConfEngine   *engine,
                             gpointer       data,
                             GDestroyNotify dnotify)
{
  if (engine->dnotify)
    {
      (* engine->dnotify) (engine->user_data);
    }

  engine->dnotify = dnotify;
  engine->user_data = data;
}

gpointer
gconf_engine_get_user_data  (GConfEngine   *engine)
{
  return engine->user_data;
}

guint
gconf_engine_notify_add(GConfEngine* conf,
                        const gchar* namespace_section,
                        GConfNotifyFunc func,
                        gpointer user_data,
                        GError** err)
{
  /* XXX: implement */
#if 0
  ConfigDatabase db;
  ConfigListener cl;
  gulong id;
  CORBA_Environment ev;
  GConfCnxn* cnxn;
  gint tries = 0;
  ConfigDatabase3_PropList properties;
#define NUM_PROPERTIES 1
  ConfigStringProperty properties_buffer[1];
#endif
  g_return_val_if_fail(!gconf_engine_is_local(conf), 0);

  CHECK_OWNER_USE (conf);
  
  if (gconf_engine_is_local(conf))
    {
      if (err)
        *err = gconf_error_new(GCONF_ERROR_LOCAL_ENGINE,
                               _("Can't add notifications to a local configuration source"));

      return 0;
    }
#if 0
  properties._buffer = properties_buffer;
  properties._length = NUM_PROPERTIES;
  properties._maximum = NUM_PROPERTIES;
  properties._release = CORBA_FALSE; /* don't free static buffer */

  properties._buffer[0].key = "name";
  properties._buffer[0].value = g_get_prgname ();
  if (properties._buffer[0].value == NULL)
    properties._buffer[0].value = "unknown";
  
  CORBA_exception_init(&ev);

 RETRY:
  
  db = gconf_engine_get_database (conf, TRUE, err);

  if (db == NULL)
    return 0;

  cl = gconf_get_config_listener ();
  
  /* Should have aborted the program in this case probably */
  g_return_val_if_fail(cl != NULL, 0);
  
  id = ConfigDatabase3_add_listener_with_properties (db,
                                                     (gchar*)namespace_section, 
                                                     cl,
                                                     &properties,
                                                     &ev);
  
  if (ev._major == CORBA_SYSTEM_EXCEPTION &&
      CORBA_exception_id (&ev) &&
      strcmp (CORBA_exception_id (&ev), "IDL:CORBA/BAD_OPERATION:1.0") == 0)
    {
      CORBA_exception_free (&ev);
      CORBA_exception_init (&ev);      
  
      id = ConfigDatabase_add_listener(db,
                                       (gchar*)namespace_section, 
                                       cl, &ev);
    }
  
  if (gconf_server_broken(&ev))
    {
      if (tries < MAX_RETRIES)
        {
          ++tries;
          CORBA_exception_free(&ev);
          gconf_engine_detach (conf);
          goto RETRY;
        }
    }
  
  if (gconf_handle_dbus_exception (NULL, err)) /* XXX: message */
    return 0;

  cnxn = gconf_cnxn_new(conf, namespace_section, id, func, user_data);

  ctable_insert(conf->ctable, cnxn);

  return cnxn->client_id;
  #endif
  return 0;
}

void         
gconf_engine_notify_remove(GConfEngine* conf,
                           guint client_id)
{
#if 0
  GConfCnxn* gcnxn;
  CORBA_Environment ev;
  ConfigDatabase db;
  gint tries = 0;

  CHECK_OWNER_USE (conf);
  
  if (gconf_engine_is_local(conf))
    return;
  
  CORBA_exception_init(&ev);

 RETRY:
  
  db = gconf_engine_get_database (conf, TRUE, NULL);

  if (db == NULL)
    return;

  gcnxn = ctable_lookup_by_client_id(conf->ctable, client_id);

  g_return_if_fail(gcnxn != NULL);

  ConfigDatabase_remove_listener(db,
                                 gcnxn->server_id,
                                 &ev);

  if (gconf_server_broken(&ev))
    {
      if (tries < MAX_RETRIES)
        {
          ++tries;
          CORBA_exception_free(&ev);
          gconf_engine_detach (conf);
          goto RETRY;
        }
    }
  
  if (gconf_handle_dbus_exception (NULL, NULL)) /* XXX: message */
    {
      ; /* do nothing */
    }
  

  /* We want to do this even if the CORBA fails, so if we restart gconfd and 
     reinstall listeners we don't reinstall this one. */
  ctable_remove(conf->ctable, gcnxn);

  gconf_cnxn_destroy(gcnxn);
#endif
}

GConfValue *
gconf_engine_get_fuller (GConfEngine *conf,
                         const gchar *key,
                         const gchar *locale,
                         gboolean use_schema_default,
                         gboolean *is_default_p,
                         gboolean *is_writable_p,
                         gchar   **schema_name_p,
                         GError **err)
{
  GConfValue* val;
  const gchar *db;
  gint tries = 0;
  gboolean is_default = FALSE;
  gboolean is_writable = TRUE;
  gchar *schema_name = NULL;
  
  g_return_val_if_fail(conf != NULL, NULL);
  g_return_val_if_fail(key != NULL, NULL);
  g_return_val_if_fail(err == NULL || *err == NULL, NULL);

  CHECK_OWNER_USE (conf);
  
  if (!gconf_key_check(key, err))
    return NULL;

  if (gconf_engine_is_local(conf))
    {
      gchar** locale_list;
      
      locale_list = gconf_split_locale(locale);
      
      val = gconf_sources_query_value(conf->local_sources,
                                      key,
                                      (const gchar**)locale_list,
                                      use_schema_default,
                                      &is_default,
                                      &is_writable,
                                      schema_name_p ? &schema_name : NULL,
                                      err);

      if (locale_list != NULL)
        g_strfreev(locale_list);
      
      if (is_default_p)
        *is_default_p = is_default;

      if (is_writable_p)
        *is_writable_p = is_writable;

      if (schema_name_p)
        *schema_name_p = schema_name;
      else
        g_free (schema_name);
      
      return val;
    }

  g_assert(!gconf_engine_is_local(conf));

  /* XXX: implement dbus version */
  
 RETRY:
  
  db = gconf_engine_get_database (conf, TRUE, err);

  if (db == NULL)
    {
      g_return_val_if_fail(err == NULL || *err != NULL, NULL);

      return NULL;
    }

  if (schema_name_p)
    *schema_name_p = NULL;

  /*
  cv = ConfigDatabase2_lookup_with_schema_name (db,
                                                (gchar*)key, (gchar*)
                                                (locale ? locale : gconf_current_locale()),
                                                use_schema_default,
                                                &schema_name,
                                                &is_default,
                                                &is_writable,
                                                &ev);
  */
  if (gconf_server_broken(NULL)) // XXX
    {
      if (tries < MAX_RETRIES)
        {
          ++tries;
          gconf_engine_detach (conf);
          goto RETRY;
        }
    }
  
  if (gconf_handle_dbus_exception (NULL, err)) /* XXX: message */
    {
      /* NOTE: don't free cv since we got an exception! */
      return NULL;
    }
  else
    {
      val = NULL;/* XXX: gconf_value_from_corba_value(cv); */

      if (is_default_p)
        *is_default_p = is_default;
      if (is_writable_p)
        *is_writable_p = is_writable;

      /* we can't get a null pointer through corba
       * so the server sent us an empty string
       */
      if (schema_name && schema_name[0] != '/')
        {
          /* XXX: CORBA_free (corba_schema_name); */
          schema_name = NULL;
        }

      if (schema_name_p)
        *schema_name_p = g_strdup (schema_name);
      
      if (schema_name)
        ;/* XXX: CORBA_free (schema_name); */
      
      return val;
    }
}


GConfValue *
gconf_engine_get_full (GConfEngine *conf,
                       const gchar *key,
                       const gchar *locale,
                       gboolean use_schema_default,
                       gboolean *is_default_p,
                       gboolean *is_writable_p,
                       GError **err)
{
  return gconf_engine_get_fuller (conf, key, locale, use_schema_default,
                                  is_default_p, is_writable_p,
                                  NULL, err);
}

GConfEntry*
gconf_engine_get_entry(GConfEngine* conf,
                       const gchar* key,
                       const gchar* locale,
                       gboolean use_schema_default,
                       GError** err)
{
  gboolean is_writable = TRUE;
  gboolean is_default = FALSE;
  GConfValue *val;
  GError *error;
  GConfEntry *entry;
  gchar *schema_name;

  CHECK_OWNER_USE (conf);
  
  schema_name = NULL;
  error = NULL;
  val = gconf_engine_get_fuller (conf, key, locale, use_schema_default,
                                 &is_default, &is_writable,
                                 &schema_name, &error);
  if (error != NULL)
    {
      g_propagate_error (err, error);
      return NULL;
    }

  entry = gconf_entry_new_nocopy (g_strdup (key),
                                  val);

  gconf_entry_set_is_default (entry, is_default);
  gconf_entry_set_is_writable (entry, is_writable);
  gconf_entry_set_schema_name (entry, schema_name);
  g_free (schema_name);

  return entry;
}
     
GConfValue*  
gconf_engine_get (GConfEngine* conf, const gchar* key, GError** err)
{
  return gconf_engine_get_with_locale(conf, key, NULL, err);
}

GConfValue*
gconf_engine_get_with_locale(GConfEngine* conf, const gchar* key,
                             const gchar* locale,
                             GError** err)
{
  return gconf_engine_get_full(conf, key, locale, TRUE,
                               NULL, NULL, err);
}

GConfValue*
gconf_engine_get_without_default(GConfEngine* conf, const gchar* key,
                                 GError** err)
{
  return gconf_engine_get_full(conf, key, NULL, FALSE, NULL, NULL, err);
}

GConfValue*
gconf_engine_get_default_from_schema (GConfEngine* conf,
                                      const gchar* key,
                                      GError** err)
{
  GConfValue* val;
  /* ConfigValue* cv; */
  const gchar *db;
  gint tries = 0;

  g_return_val_if_fail(conf != NULL, NULL);
  g_return_val_if_fail(key != NULL, NULL);
  g_return_val_if_fail(err == NULL || *err == NULL, NULL);

  CHECK_OWNER_USE (conf);
  
  if (!gconf_key_check(key, err))
    return NULL;

  if (gconf_engine_is_local(conf))
    {
      gchar** locale_list;

      locale_list = gconf_split_locale(gconf_current_locale());
      
      val = gconf_sources_query_default_value(conf->local_sources,
                                              key,
                                              (const gchar**)locale_list,
                                              NULL,
                                              err);

      if (locale_list != NULL)
        g_strfreev(locale_list);
      
      return val;
    }

  g_assert(!gconf_engine_is_local(conf));
  
 RETRY:
  
  db = gconf_engine_get_database (conf, TRUE, err);

  if (db == NULL)
    {
      g_return_val_if_fail(err == NULL || *err != NULL, NULL);
      
      return NULL;
    }

  /*
    cv = ConfigDatabase_lookup_default_value(db,
                                           (gchar*)key,
                                           (gchar*)gconf_current_locale(),
                                           &ev);
  */
					   
  if (gconf_server_broken (NULL)) // XXX
    {
      if (tries < MAX_RETRIES)
        {
          ++tries;
          gconf_engine_detach (conf);
          goto RETRY;
        }
    }
  
  if (gconf_handle_dbus_exception (NULL, err)) /* XXX: message */
    {
      /* NOTE: don't free cv since we got an exception! */
      return NULL;
    }
  else
    {
      val = NULL; /* XXX: gconf_value_from_corba_value(cv); */

      return val;
    }
}

gboolean
gconf_engine_set (GConfEngine* conf, const gchar* key,
                  const GConfValue* value, GError** err)
{
  const gchar *db;
  gint tries = 0;

  g_return_val_if_fail(conf != NULL, FALSE);
  g_return_val_if_fail(key != NULL, FALSE);
  g_return_val_if_fail(value != NULL, FALSE);
  g_return_val_if_fail(value->type != GCONF_VALUE_INVALID, FALSE);
  g_return_val_if_fail( (value->type != GCONF_VALUE_STRING) ||
                        (gconf_value_get_string(value) != NULL) , FALSE );
  g_return_val_if_fail( (value->type != GCONF_VALUE_LIST) ||
                        (gconf_value_get_list_type(value) != GCONF_VALUE_INVALID), FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);

  CHECK_OWNER_USE (conf);
  
  if (!gconf_key_check(key, err))
    return FALSE;

  if (!gconf_value_validate (value, err))
    return FALSE;
  
  if (gconf_engine_is_local(conf))
    {
      GError* error = NULL;
      
      gconf_sources_set_value(conf->local_sources, key, value, &error);

      if (error != NULL)
        {
          if (err)
            *err = error;
          else
            {
              g_error_free(error);
            }
          return FALSE;
        }
      
      return TRUE;
    }

  g_assert(!gconf_engine_is_local(conf));
  
 RETRY:
  
  db = gconf_engine_get_database (conf, TRUE, err);

  if (db == NULL)
    {
      g_return_val_if_fail(err == NULL || *err != NULL, FALSE);
      
      return FALSE;
    }
  
  /*cv = XXX: gconf_corba_value_from_gconf_value (value);

    ConfigDatabase_set(db,
                     (gchar*)key, cv,
                     &ev);

  CORBA_free(cv);
  */
  if (gconf_server_broken(NULL)) // XXX
    {
      if (tries < MAX_RETRIES)
        {
          ++tries;
          gconf_engine_detach (conf);
          goto RETRY;
        }
    }
  
  if (gconf_handle_dbus_exception (NULL, err)) /* XXX: message */
    return FALSE;

  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
  
  return TRUE;
}

gboolean
gconf_engine_unset (GConfEngine* conf, const gchar* key, GError** err)
{
  const gchar *db;
  gint tries = 0;

  g_return_val_if_fail (conf != NULL, FALSE);
  g_return_val_if_fail (key != NULL, FALSE);
  g_return_val_if_fail (err == NULL || *err == NULL, FALSE);

  CHECK_OWNER_USE (conf);
  
  if (!gconf_key_check(key, err))
    return FALSE;

  if (gconf_engine_is_local(conf))
    {
      GError* error = NULL;
      
      gconf_sources_unset_value(conf->local_sources, key, NULL, &error);

      if (error != NULL)
        {
          if (err)
            *err = error;
          else
            {
              g_error_free(error);
            }
          return FALSE;
        }
      
      return TRUE;
    }

  g_assert(!gconf_engine_is_local(conf));
  
 RETRY:
  
  db = gconf_engine_get_database (conf, TRUE, err);

  if (db == NULL)
    {
      g_return_val_if_fail(err == NULL || *err != NULL, FALSE);

      return FALSE;
    }

  /*  ConfigDatabase_unset (db,
                        (gchar*)key,
                        &ev);
  */
  if (gconf_server_broken (NULL)) // XXX
    {
      if (tries < MAX_RETRIES)
        {
          ++tries;
          gconf_engine_detach(conf);
          goto RETRY;
        }
    }
  
  if (gconf_handle_dbus_exception (NULL, err)) /* XXX: message */
    return FALSE;

  g_return_val_if_fail (err == NULL || *err == NULL, FALSE);
  
  return TRUE;
}

/**
 * gconf_engine_recursive_unset:
 * @engine: a #GConfEngine
 * @key: a key or directory name
 * @flags: change how the unset is done
 * @err: return location for a #GError, or %NULL to ignore errors
 * 
 * Unsets all keys below @key, including @key itself.  If any unset
 * fails, continues on to unset as much as it can. The first
 * failure is returned in @err.
 *
 * Returns: %FALSE if error is set
 **/
gboolean
gconf_engine_recursive_unset (GConfEngine    *conf,
                              const char     *key,
                              GConfUnsetFlags flags,
                              GError        **err)
{
  const gchar *db;
  gint tries = 0;
  /*ConfigDatabase3_UnsetFlags corba_flags;*/
  
  g_return_val_if_fail (conf != NULL, FALSE);
  g_return_val_if_fail (key != NULL, FALSE);
  g_return_val_if_fail (err == NULL || *err == NULL, FALSE);

  CHECK_OWNER_USE (conf);
  
  if (!gconf_key_check (key, err))
    return FALSE;

  if (gconf_engine_is_local (conf))
    {
      GError* error = NULL;
      
      gconf_sources_recursive_unset (conf->local_sources, key, NULL,
                                     flags, NULL, &error);

      if (error != NULL)
        {
          if (err)
            *err = error;
          else
            {
              g_error_free (error);
            }
          return FALSE;
        }
      
      return TRUE;
    }

  g_assert (!gconf_engine_is_local (conf));
  
  /*corba_flags = 0;
  if (flags & GCONF_UNSET_INCLUDING_SCHEMA_NAMES)
    corba_flags |= ConfigDatabase3_UNSET_INCLUDING_SCHEMA_NAMES;
  */
 RETRY:
  
  db = gconf_engine_get_database (conf, TRUE, err);

  if (db == NULL)
    {
      g_return_val_if_fail (err == NULL || *err != NULL, FALSE);

      return FALSE;
    }

  /* XXX: ConfigDatabase3_recursive_unset (db, key, corba_flags, &ev);*/

  if (gconf_server_broken (NULL)) // XXX
    {
      if (tries < MAX_RETRIES)
        {
          ++tries;
          gconf_engine_detach(conf);
          goto RETRY;
        }
    }
  
  if (gconf_handle_dbus_exception (NULL, err)) /* XXX: message */
    return FALSE;

  g_return_val_if_fail (err == NULL || *err == NULL, FALSE);
  
  return TRUE;
}

gboolean
gconf_engine_associate_schema  (GConfEngine* conf, const gchar* key,
                                const gchar* schema_key, GError** err)
{
  const gchar *db;
  gint tries = 0;

  g_return_val_if_fail (conf != NULL, FALSE);
  g_return_val_if_fail (key != NULL, FALSE);
  g_return_val_if_fail (err == NULL || *err == NULL, FALSE);
  
  if (!gconf_key_check (key, err))
    return FALSE;

  if (schema_key && !gconf_key_check (schema_key, err))
    return FALSE;

  if (gconf_engine_is_local(conf))
    {
      GError* error = NULL;
      
      gconf_sources_set_schema (conf->local_sources, key, schema_key, &error);

      if (error != NULL)
        {
          if (err)
            *err = error;
          else
            {
              g_error_free(error);
            }
          return FALSE;
        }
      
      return TRUE;
    }

  g_assert (!gconf_engine_is_local (conf));
  
 RETRY:
  
  db = gconf_engine_get_database (conf, TRUE, err);

  if (db == NULL)
    {
      g_return_val_if_fail (err == NULL || *err != NULL, FALSE);

      return FALSE;
    }

#if 0
  ConfigDatabase_set_schema (db,
                             key,
                             /* empty string means unset */
                             schema_key ? schema_key : "",
                             &ev);
#endif
  
  if (gconf_server_broken (NULL)) // XXX
    {
      if (tries < MAX_RETRIES)
        {
          ++tries;
          gconf_engine_detach (conf);
          goto RETRY;
        }
    }
  
  if (gconf_handle_dbus_exception (NULL, err)) /* XXX: message */
    return FALSE;

  g_return_val_if_fail (err == NULL || *err == NULL, FALSE);
  
  return TRUE;
}


static void
qualify_entries (GSList *entries, const char *dir)
{
  GSList *tmp = entries;
  while (tmp != NULL)
    {
      GConfEntry *entry = tmp->data;
      gchar *full;

      full = gconf_concat_dir_and_key (dir, entry->key);

      g_free (entry->key);
      entry->key = full;

      tmp = g_slist_next (tmp);
    }
}

GSList*      
gconf_engine_all_entries(GConfEngine* conf, const gchar* dir, GError** err)
{
  GSList* pairs = NULL;
  /*  ConfigDatabase_ValueList* values;
  ConfigDatabase_KeyList* keys;
  ConfigDatabase_IsDefaultList* is_defaults;
  ConfigDatabase_IsWritableList* is_writables;
  ConfigDatabase2_SchemaNameList *schema_names;
  */
  const gchar *db;
  /*  guint i;*/
  gint tries = 0;

  g_return_val_if_fail(conf != NULL, NULL);
  g_return_val_if_fail(dir != NULL, NULL);
  g_return_val_if_fail(err == NULL || *err == NULL, NULL);

  CHECK_OWNER_USE (conf);
  
  if (!gconf_key_check(dir, err))
    return NULL;


  if (gconf_engine_is_local(conf))
    {
      GError* error = NULL;
      gchar** locale_list;
      GSList* retval;
      
      locale_list = gconf_split_locale(gconf_current_locale());
      
      retval = gconf_sources_all_entries(conf->local_sources,
                                         dir,
                                         (const gchar**)locale_list,
                                         &error);

      if (locale_list)
        g_strfreev(locale_list);
      
      if (error != NULL)
        {
          if (err)
            *err = error;
          else
            {
              g_error_free(error);
            }

          g_assert(retval == NULL);
          
          return NULL;
        }

      qualify_entries (retval, dir);
      
      return retval;
    }

  g_assert(!gconf_engine_is_local(conf));
  
 RETRY:
  
  db = gconf_engine_get_database (conf, TRUE, err);

  if (db == NULL)
    {
      g_return_val_if_fail(err == NULL || *err != NULL, NULL);

      return NULL;
    }

  /*
    schema_names = NULL;

  ConfigDatabase2_all_entries_with_schema_name (db,
                                                (gchar*)dir,
                                                (gchar*)gconf_current_locale(),
                                                &keys, &values, &schema_names,
                                                &is_defaults, &is_writables,
                                                &ev);
  */
  
  if (gconf_server_broken (NULL)) // XXX
    {
      if (tries < MAX_RETRIES)
        {
          ++tries;
          gconf_engine_detach (conf);
          goto RETRY;
        }
    }
  
  if (gconf_handle_dbus_exception (NULL, err)) /* XXX: message */
    return NULL;
  
#if 0

  if (keys->_length != values->_length)
    {
      g_warning("Received unmatched key/value sequences in %s",
                G_GNUC_FUNCTION);
      return NULL;
    }

  i = 0;
  while (i < keys->_length)
    {
      GConfEntry* pair;

      pair = 
        gconf_entry_new_nocopy(gconf_concat_dir_and_key (dir, keys->_buffer[i]),
                               gconf_value_from_corba_value(&(values->_buffer[i])));

      gconf_entry_set_is_default (pair, is_defaults->_buffer[i]);
      gconf_entry_set_is_writable (pair, is_writables->_buffer[i]);
      if (schema_names)
        {
          /* empty string means no schema name */
          if (*(schema_names->_buffer[i]) != '\0')
            gconf_entry_set_schema_name (pair, schema_names->_buffer[i]);
        }
      
      pairs = g_slist_prepend(pairs, pair);
      
      ++i;
    }
  
  CORBA_free(keys);
  CORBA_free(values);
  CORBA_free(is_defaults);
  CORBA_free(is_writables);
  if (schema_names)
    CORBA_free (schema_names);
#endif
  
  return pairs;
}


static void
qualify_keys (GSList *keys, const char *dir)
{
  GSList *tmp = keys;
  while (tmp != NULL)
    {
      char *key = tmp->data;
      gchar *full;

      full = gconf_concat_dir_and_key (dir, key);

      g_free (tmp->data);
      tmp->data = full;

      tmp = g_slist_next (tmp);
    }
}

GSList*      
gconf_engine_all_dirs(GConfEngine* conf, const gchar* dir, GError** err)
{
  GSList* subdirs = NULL;
  /*  ConfigDatabase_KeyList* keys;*/
  const gchar *db;
  guint i;
  gint tries = 0;

  g_return_val_if_fail(conf != NULL, NULL);
  g_return_val_if_fail(dir != NULL, NULL);
  g_return_val_if_fail(err == NULL || *err == NULL, NULL);

  CHECK_OWNER_USE (conf);
  
  if (!gconf_key_check(dir, err))
    return NULL;

  if (gconf_engine_is_local(conf))
    {
      GError* error = NULL;
      GSList* retval;
      
      retval = gconf_sources_all_dirs(conf->local_sources,
                                      dir,
                                      &error);
      
      if (error != NULL)
        {
          if (err)
            *err = error;
          else
            {
              g_error_free(error);
            }

          g_assert(retval == NULL);
          
          return NULL;
        }

      qualify_keys (retval, dir);
      
      return retval;
    }

  g_assert(!gconf_engine_is_local(conf));
  
 RETRY:
  
  db = gconf_engine_get_database (conf, TRUE, err);

  if (db == NULL)
    {
      g_return_val_if_fail(((err == NULL) || (*err && ((*err)->code == GCONF_ERROR_NO_SERVER))), NULL);

      return NULL;
    }
  
  /*  ConfigDatabase_all_dirs(db,
                          (gchar*)dir, 
                          &keys,
                          &ev);
  */
  if (gconf_server_broken (NULL)) // XXX
    {
      if (tries < MAX_RETRIES)
        {
          ++tries;
          gconf_engine_detach (conf);
          goto RETRY;
        }
    }

  if (gconf_handle_dbus_exception (NULL, err)) /* XXX: message */
    return NULL;
  
  i = 0;
  /*  while (i < keys->_length)
    {
      gchar* s;

      s = gconf_concat_dir_and_key (dir, keys->_buffer[i]);
      
      subdirs = g_slist_prepend(subdirs, s);
      
      ++i;
    }
  
  CORBA_free(keys);
  */
  return subdirs;
}

/* annoyingly, this is REQUIRED for local sources */
void 
gconf_engine_suggest_sync(GConfEngine* conf, GError** err)
{
  const gchar *db;
  gint tries = 0;

  g_return_if_fail(conf != NULL);
  g_return_if_fail(err == NULL || *err == NULL);

  CHECK_OWNER_USE (conf);
  
  if (gconf_engine_is_local(conf))
    {
      GError* error = NULL;
      
      gconf_sources_sync_all(conf->local_sources,
                             &error);
      
      if (error != NULL)
        {
          if (err)
            *err = error;
          else
            {
              g_error_free(error);
            }
          return;
        }
      
      return;
    }

  g_assert(!gconf_engine_is_local(conf));
  
 RETRY:
  
  db = gconf_engine_get_database (conf, TRUE, err);

  if (db == NULL)
    {
      g_return_if_fail(err == NULL || *err != NULL);

      return;
    }

  /* XXX: ConfigDatabase_sync(db, &ev);*/

  if (gconf_server_broken (NULL)) // XXX
    {
      if (tries < MAX_RETRIES)
        {
          ++tries;
          gconf_engine_detach (conf);
          goto RETRY;
        }
    }
  
  if (gconf_handle_dbus_exception (NULL, err)) /* XXX: message */
    ; /* nothing additional */
}

void 
gconf_clear_cache(GConfEngine* conf, GError** err)
{
  const gchar *db;
  gint tries = 0;

  g_return_if_fail(conf != NULL);
  g_return_if_fail(err == NULL || *err == NULL);

  /* don't disallow non-owner use here since you can't do this
   * via GConfClient API and calling this function won't break
   * GConfClient anyway
   */
  
  if (gconf_engine_is_local(conf))
    {
      GError* error = NULL;
      
      gconf_sources_clear_cache(conf->local_sources);
      
      if (error != NULL)
        {
          if (err)
            *err = error;
          else
            {
              g_error_free(error);
            }
          return;
        }
      
      return;
    }

  g_assert(!gconf_engine_is_local(conf));
  
 RETRY:
  
  db = gconf_engine_get_database (conf, TRUE, err);

  if (db == NULL)
    {
      g_return_if_fail(err == NULL || *err != NULL);

      return;
    }

  /* XXX: ConfigDatabase_clear_cache(db, &ev);*/

  if (gconf_server_broken (NULL)) // XXX
    {
      if (tries < MAX_RETRIES)
        {
          ++tries;
          gconf_engine_detach (conf);
          goto RETRY;
        }
    }
  
  if (gconf_handle_dbus_exception(NULL, err)) /* XXX: message */
    ; /* nothing additional */
}

void 
gconf_synchronous_sync(GConfEngine* conf, GError** err)
{
  const gchar *db;
  gint tries = 0;

  g_return_if_fail(conf != NULL);
  g_return_if_fail(err == NULL || *err == NULL);

  if (gconf_engine_is_local(conf))
    {
      GError* error = NULL;
      
      gconf_sources_sync_all(conf->local_sources, &error);
      
      if (error != NULL)
        {
          if (err)
            *err = error;
          else
            {
              g_error_free(error);
            }
          return;
        }
      
      return;
    }

  g_assert(!gconf_engine_is_local(conf));
  
 RETRY:
  
  db = gconf_engine_get_database (conf, TRUE, err);

  if (db == NULL)
    {
      g_return_if_fail(err == NULL || *err != NULL);

      return;
    }

  /* XXX: ConfigDatabase_synchronous_sync(db, &ev);*/

  if (gconf_server_broken (NULL)) // XXX
    {
      if (tries < MAX_RETRIES)
        {
          ++tries;
          gconf_engine_detach (conf);
          goto RETRY;
        }
    }
  
  if (gconf_handle_dbus_exception (NULL, err)) /* XXX: message */
    ; /* nothing additional */
}

gboolean
gconf_engine_dir_exists(GConfEngine *conf, const gchar *dir, GError** err)
{
  const gchar *db;
  gboolean server_ret;
  gint tries = 0;

  g_return_val_if_fail(conf != NULL, FALSE);
  g_return_val_if_fail(dir != NULL, FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);

  CHECK_OWNER_USE (conf);
  
  if (!gconf_key_check(dir, err))
    return FALSE;
  
  if (gconf_engine_is_local(conf))
    {
      return gconf_sources_dir_exists(conf->local_sources,
                                      dir,
                                      err);
    }

  g_assert(!gconf_engine_is_local(conf));
  
 RETRY:
  
  db = gconf_engine_get_database(conf, TRUE, err);
  
  if (db == NULL)
    {
      g_return_val_if_fail(err == NULL || *err != NULL, FALSE);

      return FALSE;
    }
  
  server_ret = TRUE; /* XXX: ConfigDatabase_dir_exists(db,
                                         (gchar*)dir,
                                         &ev);
  */
  if (gconf_server_broken (NULL)) // XXX
    {
      if (tries < MAX_RETRIES)
        {
          ++tries;
          gconf_engine_detach (conf);
          goto RETRY;
        }
    }
  
  if (gconf_handle_dbus_exception (NULL, err)) /* XXX: message */
    ; /* nothing */

  return (server_ret == TRUE);
}

void
gconf_engine_remove_dir (GConfEngine* conf,
                         const gchar* dir,
                         GError** err)
{
  const gchar *db;
  gint tries = 0;

  g_return_if_fail(conf != NULL);
  g_return_if_fail(dir != NULL);
  g_return_if_fail(err == NULL || *err == NULL);

  /* FIXME we have no GConfClient method for doing this */
  /*   CHECK_OWNER_USE (conf); */
  
  if (!gconf_key_check(dir, err))
    return;

  if (gconf_engine_is_local(conf))
    {
      gconf_sources_remove_dir(conf->local_sources, dir, err);
      return;
    }

 RETRY:
  
  db = gconf_engine_get_database (conf, TRUE, err);

  if (db == NULL)
    {
      g_return_if_fail(err == NULL || *err != NULL);
      return;
    }
  
  /* XXX: ConfigDatabase_remove_dir(db, (gchar*)dir, &ev);*/

  if (gconf_server_broken (NULL)) // XXX
    {
      if (tries < MAX_RETRIES)
        {
          ++tries;
          gconf_engine_detach (conf);
          goto RETRY;
        }
    }
  gconf_handle_dbus_exception (NULL, err); /* XXX: message */
  
  return;
}

/* All errors set in here should be GCONF_ERROR_NO_SERVER; should
   only set errors if start_if_not_found is TRUE */
static const gchar *
gconf_get_config_server(gboolean start_if_not_found, GError** err)
{
  g_return_val_if_fail(err == NULL || *err == NULL, server);
  
  if (server != NULL)
    return server;

  server = NULL; /* XXX: activate the service here, add client if we need to do that */
  
  return server; /* return what we have, NULL or not */
}

const gchar *listener = NULL;

static void
gconf_detach_config_server(void)
{  
  /* XXX: remove listener and

  if (engines_by_db != NULL)
    {
      g_hash_table_destroy (engines_by_db);
      engines_by_db = NULL;
    }

  */
}

/**
 * gconf_debug_shutdown:
 * @void: 
 * 
 * Detach from the config server and release
 * all related resources
 **/
int
gconf_debug_shutdown (void)
{
  gconf_detach_config_server ();

  return 0; /* XXX: 0 is success... */
}


#if 0
static void 
notify (...)
{
  GConfCnxn* cnxn;
  GConfValue* gvalue;
  GConfEngine* conf;
  GConfEntry* entry;
  
  conf = lookup_engine_by_database (db);

  if (conf == NULL)
    {
#ifdef GCONF_ENABLE_DEBUG
      g_warning ("Client received notify for unknown database object");
#endif
      return;
    }
  
  cnxn = ctable_lookup_by_server_id(conf->ctable, server_id);
  
  if (cnxn == NULL)
    {
#ifdef GCONF_ENABLE_DEBUG
      g_warning("Client received notify for unknown connection ID %u",
                (guint)server_id);
#endif
      return;
    }

  gvalue = gconf_value_from_...;
  
  entry = gconf_entry_new_nocopy (g_strdup (key), gvalue);
  gconf_entry_set_is_default (entry, is_default);
  gconf_entry_set_is_writable (entry, is_writable);
  
  gconf_cnxn_notify(cnxn, entry);
  
  gconf_entry_free (entry);
}

static void
ping (...)
{
  /* This one is easy :-) */
  
  return;
}

static void
invalidate_cached_values (...)
{
#if 0
  g_warning ("FIXME process %d received request to invalidate some cached GConf values from the server, but right now we don't know how to do that (not implemented).", (int) getpid());
#endif
}

static void
drop_all_caches (...)
{
#if 0
  g_warning ("FIXME process %d received request to invalidate all cached GConf values from the server, but right now we don't know how to do that (not implemented).", (int) getpid());
#endif
}

static const gchar *
gconf_get_config_listener (void)
{  
  if (listener == NULL)
    {
      /* FIXME: do something */

      listener = NULL;
    }
  
  return listener;
}
#endif

/*
 * Daemon control
 */

void          
gconf_shutdown_daemon (GError** err)
{
  const gchar * cs;

  cs = gconf_get_config_server (FALSE, err); /* Don't want to spawn it if it's already down */

  if (err && *err && (*err)->code == GCONF_ERROR_NO_SERVER)
    {
      /* No server is hardly an error here */
      g_error_free (*err);
      *err = NULL;
    }
  
  if (cs == NULL)
    {      
      
      return;
    }

  //ConfigServer_shutdown (cs, &ev);

  /*  if (ev._major != CORBA_NO_EXCEPTION)
    {
      if (err)
        *err = gconf_error_new (GCONF_ERROR_FAILED, _("Failure shutting down config server: %s"),
	CORBA_exception_id (&ev));
    }
  */
}

gboolean
gconf_ping_daemon(void)
{
  const gchar * cs;
  
  cs = gconf_get_config_server(FALSE, NULL); /* ignore error, since whole point is to see if server is reachable */

  if (cs == NULL)
    return FALSE;
  else
    return TRUE;
}

gboolean
gconf_spawn_daemon(GError** err)
{
  const gchar * cs;

  cs = gconf_get_config_server(TRUE, err);

  if (cs == NULL)
    {
      g_return_val_if_fail(err == NULL || *err != NULL, FALSE);
      return FALSE; /* Failed to spawn, error should be set */
    }
  else
    return TRUE;
}

