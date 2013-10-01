/*
  Copyright (C) 2013 Paul Davis
    
  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or (at
  your option) any later version.
    
  This program is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
  License for more details.
    
  You should have received a copy of the GNU Lesser General Public License
  along with this program; if not, write to the Free Software Foundation,
  Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#include <string.h>
#include <db.h>
#include <limits.h>

#include <jack/metadata.h>
#include <jack/uuid.h>

#include "internal.h"
#include "local.h"

static DB* db = NULL;

static int
jack_property_init (const char* server_name)
{
        int ret;
        char dbpath[PATH_MAX+1];
        char server_dir[PATH_MAX+1];

        /* idempotent */

        if (db) {
                return 0;
        }

        if ((ret = db_create (&db, NULL, 0)) != 0) {
                jack_error ("Cannot initialize metadata DB (%s)", db_strerror (ret));
                return -1;
        }

        snprintf (dbpath, sizeof (dbpath), "%s/%s", jack_server_dir (server_name, server_dir), "metadata.db");

        if ((ret = db->open (db, NULL, dbpath, NULL, DB_HASH, DB_CREATE|DB_THREAD, 0666)) != 0) {
                jack_error ("Cannot open metadata DB at %s: %s", dbpath, db_strerror (ret));
                db->close (db, 0);
                db = NULL;
                return -1;
        }

        return 0;
}

static void jack_properties_uninit () __attribute__ ((destructor));

void
jack_properties_uninit ()
{
        if (db) {
                db->close (db, 0);
                db = NULL;
        }
}

void
jack_free_description(jack_description_t* desc)
{
        return;
}

static void
make_key_dbt (DBT* dbt, jack_uuid_t subject, const char* key)
{
        char ustr[JACK_UUID_STRING_SIZE];
        size_t len1, len2;

        memset(dbt, 0, sizeof(DBT));
        jack_uuid_unparse (subject, ustr);
        len1 = JACK_UUID_STRING_SIZE;
        len2 = strlen (key) + 1;
        dbt->size = len1 + len2;
        dbt->data = malloc (dbt->size);
        memcpy (dbt->data, ustr, len1);   // copy subject+null
        memcpy (dbt->data + len1, key, len2); // copy key+null
}


int
jack_set_property (jack_client_t* client,
                   jack_uuid_t subject,
                   const char* key,
                   const char* value,
                   const char* type)
{
        DBT d_key;
        DBT data;
        int ret;
        size_t len1, len2;

        if (jack_property_init (NULL)) {
                return -1;
        }

        /* build a key */

        make_key_dbt (&d_key, subject, key);

        /* build data */

        memset(&data, 0, sizeof(data));

        len1 = strlen(value) + 1;
        if (type && type[0] != '\0') {
                len2 = strlen(type) + 1;
        } else {
                len2 = 0;
        }

        data.size = len1 + len2;
        data.data = malloc (data.size);
        memcpy (data.data, value, len1);

        if (len2) {
                memcpy (data.data + len1, type, len2);
        }

        if ((ret = db->put (db, NULL, &d_key, &data, 0)) != 0) {
                char ustr[JACK_UUID_STRING_SIZE];
                jack_uuid_unparse (subject, ustr);
                jack_error ("Cannot store metadata for %s/%s (%s)", ustr, key, db_strerror (ret));
                return -1;
        }

        return 0;
}

int
jack_get_property (jack_uuid_t subject,
                   const char* key,
                   char**      value,
                   char**      type)
{
        DBT d_key;
        DBT data;
        int ret;
        size_t len1, len2;

        if (key == NULL || key[0] == '\0') {
                return -1;
        }

        if (jack_property_init (NULL)) {
                return -1;
        }

        /* build a key */

        make_key_dbt (&d_key, subject, key);
        
        /* setup data DBT */

        memset(&data, 0, sizeof(data));
        data.flags = DB_DBT_MALLOC;

        if ((ret = db->get (db, NULL, &d_key, &data, 0)) != 0) {
                if (ret != DB_NOTFOUND) {
                        char ustr[JACK_UUID_STRING_SIZE];
                        jack_uuid_unparse (subject, ustr);
                        jack_error ("Cannot  metadata for %s/%s (%s)", ustr, key, db_strerror (ret));
                }
                return -1;
        }

        /* result must have at least 2 chars plus 2 nulls to be valid 
         */

        if (data.size < 4) {
                if (data.size > 0) {
                        free (data.data);
                }
                return -1;
        }
        
        len1 = strlen (data.data) + 1;
        (*value) = (char *) malloc (len1);
        memcpy (*value, data.data, len1);

        if (len1 < data.size) {
                len2 = strlen (data.data+len1) + 1;
                
                (*type) = (char *) malloc (len2);
                memcpy (*type, data.data+len1, len2);
        } else {
                /* no type specified, assume default */
                *type = NULL;
        }

        if (data.size) {
                free (data.data);
        }

        return 0;
}

int
jack_get_properties (jack_uuid_t subject,
                     jack_description_t* desc)
{
        DBT key;
        DBT data;
        DBC* cursor;
        int ret;
        size_t len1, len2;
        size_t cnt = 0;
        char ustr[JACK_UUID_STRING_SIZE];
        size_t props_size = 0;
        jack_property_t* prop;

        desc->properties = NULL;

        jack_uuid_unparse (subject, ustr);

        if (jack_property_init (NULL)) {
                return -1;
        }


        if ((ret = db->cursor (db, NULL, &cursor, 0)) != 0) {
                jack_error ("Cannot create cursor for metadata search (%s)", db_strerror (ret));
                return -1;
        }

        memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));
        data.flags = DB_DBT_MALLOC;

        while ((ret = cursor->get(cursor, &key, &data, DB_NEXT)) == 0) {

                /* require 2 extra chars (data+null) for key,
                   which is composed of UUID str plus a key name
                */

                if (key.size < JACK_UUID_STRING_SIZE + 2) {
                        if (data.size > 0) {
                                free (data.data);
                        }
                        continue;
                }

                if (memcmp (ustr, key.data, JACK_UUID_STRING_SIZE) != 0) {
                        /* not relevant */
                        if (data.size > 0) {
                                free (data.data);
                        }
                        continue;
                }

                /* result must have at least 2 chars plus 2 nulls to be valid 
                 */
                
                if (data.size < 4) {
                        if (data.size > 0) {
                                free (data.data);
                        }
                        continue;
                }
 
                /* realloc array if necessary */
       
                if (cnt == props_size) {
                        if (props_size == 0) {
                                props_size = 8;
                        } else {
                                props_size *= 2;
                        }

                        desc->properties = (jack_property_t*) realloc (desc->properties, sizeof (jack_property_t) * props_size);
                }

                prop = &desc->properties[cnt];

                /* store UUID/subject */

                jack_uuid_copy (desc->subject, subject);
                
                /* copy key (without leading UUID as subject */

                len1 = key.size - JACK_UUID_STRING_SIZE;
                prop->key = malloc (key.size);
                memcpy ((char*) prop->key, key.data + JACK_UUID_STRING_SIZE, len1);
                
                /* copy data */

                len1 = strlen (data.data) + 1;
                prop->data = (char *) malloc (len1);
                memcpy ((char*) prop->data, data.data, len1);
                
                if (len1 < data.size) {
                        len2 = strlen (data.data+len1) + 1;
                        
                        prop->type= (char *) malloc (len2);
                        memcpy ((char*) prop->type, data.data+len1, len2);
                } else {
                        /* no type specified, assume default */
                        prop->type = NULL;
                }
                
                if (data.size) {
                        free (data.data);
                }

                ++cnt;
        }
        
        cursor->close (cursor);

        return cnt;
}

int
jack_get_all_properties (jack_description_t** desc)
{
        DBT key;
        DBT data;
        DBC* cursor;
        int ret;
        size_t cnt = 0;

        if (jack_property_init (NULL)) {
                return -1;
        }


        if ((ret = db->cursor (db, NULL, &cursor, 0)) != 0) {
                jack_error ("Cannot create cursor for metadata search (%s)", db_strerror (ret));
                return -1;
        }

        memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));
        data.flags = DB_DBT_MALLOC;

        while ((ret = cursor->get(cursor, &key, &data, DB_NEXT)) == 0) {

                if (data.size) {
                        free (data.data);
                }
                
                ++cnt;
        }
        
        cursor->close (cursor);

        return cnt;
}


int
jack_get_description (jack_uuid_t         subject,
                       jack_description_t* desc)
{
        return 0;
}

int
jack_get_all_descriptions (jack_description_t** descs)
{
        return 0;
}

int 
jack_set_property_change_callback (jack_client_t *client,
                                   JackPropertyChangeCallback callback, void *arg)
{
	if (client->control->active) {
		jack_error ("You cannot set callbacks on an active client.");
		return -1;
	}
	client->property_cb = callback;
	client->property_cb_arg = arg;
	client->control->property_cbset = (callback != NULL);
	return 0;
}

int        
jack_remove_property (jack_client_t* client, jack_uuid_t subject, const char* key)
{
        DBT d_key;
        int ret;

        if (jack_property_init (NULL)) {
                return -1;
        }

        make_key_dbt (&d_key, subject, key);
        if ((ret = db->del (db, NULL, &d_key, 0)) != 0) {
                jack_error ("Cannot delete key %s (%s)", key, db_strerror (ret));
                return -1;
        }
        return 0;
}

int        
jack_remove_properties (jack_client_t* client, jack_uuid_t subject)
{
        DBT key;
        DBT data;
        DBC* cursor;
        int ret;
        char ustr[JACK_UUID_STRING_SIZE];
        int retval = 0;

        jack_uuid_unparse (subject, ustr);

        if (jack_property_init (NULL)) {
                return -1;
        }


        if ((ret = db->cursor (db, NULL, &cursor, 0)) != 0) {
                jack_error ("Cannot create cursor for metadata search (%s)", db_strerror (ret));
                return -1;
        }

        memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));
        data.flags = DB_DBT_MALLOC;

        while ((ret = cursor->get(cursor, &key, &data, DB_NEXT)) == 0) {

                /* require 2 extra chars (data+null) for key,
                   which is composed of UUID str plus a key name
                */

                if (key.size < JACK_UUID_STRING_SIZE + 2) {
                        if (data.size > 0) {
                                free (data.data);
                        }
                        continue;
                }

                if (memcmp (ustr, key.data, JACK_UUID_STRING_SIZE) != 0) {
                        /* not relevant */
                        if (data.size > 0) {
                                free (data.data);
                        }
                        continue;
                }

                if ((ret = cursor->del (cursor, 0)) != 0) {
                        jack_error ("cannot delete property (%s)", db_strerror (ret));
                        /* don't return -1 here since this would leave things
                           even more inconsistent. wait till the cursor is finished
                        */
                        retval = -1;
                }
        }

        cursor->close (cursor);

        return retval;
}

int        
jack_remove_all_properties (jack_client_t* client)
{
        int ret;

        if (jack_property_init (NULL)) {
                return -1;
        }

        if ((ret = db->truncate (db, NULL, NULL, 0)) != 0) {
                jack_error ("Cannot clear properties (%s)", db_strerror (ret));
                return -1;
        }

        return 0;
}
