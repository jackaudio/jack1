/*
 *   JACK IEC61883 (FireWire audio) driver
 *
 *   Copyright (C) Robert Ham 2003 <rah@bash.sh>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdio.h>
#include <string.h>

#include <jack/jack.h>
#include <jack/types.h>
#include <jack/jslist.h>
#include <jack/port.h>
#include <jack/internal.h>
#include <jack/engine.h>

#include "iec61883_common.h"



static int
iec619883_channel_spec_compare (void * a, void * b)
{
	iec61883_channel_info_t * ai = a;
	iec61883_channel_info_t * bi = b;
	return ai->iso_ch - bi->iso_ch;
}

static JSList *
iec619883_channel_spec_uniq (JSList * spec)
{
	JSList * list;
	JSList * next;

	if (!spec || !spec->next)
		return spec;
  
	for (list = spec; list && list->next; list = next) {
		next = list->next;

		if (((iec61883_channel_info_t *)list->data)->iso_ch ==
		    ((iec61883_channel_info_t *)list->next->data)->iso_ch) {
			free (list->data);
			spec = jack_slist_remove (spec, list);
		}
	}
  
	return spec;
}

static iec61883_channel_info_t *
iec61883_channel_info_new (char iso_channel, int audio_channels)
{
	iec61883_channel_info_t * info;
  
	info = malloc (sizeof (iec61883_channel_info_t));
  
	info->iso_ch    = iso_channel;
	info->naud_chs  = audio_channels;
  
	return info;
}

JSList *
iec61883_get_channel_spec (const char * channel_spec)
{
	JSList * list = NULL;
	char *spec;
	char *range;
	char *rptr;
	char *aptr;
	int i, j, k, naud_chs;
  
	spec = strdup (channel_spec);
  
	range = strtok (spec, ",");
	do {
		i = atoi (range);
      
		if (i >= 64)
		{
			jack_error ("IEC61883CM: malformed channel range "
				    "specification '%s'", range);
			continue;
		}
        
      
		rptr = strchr (range, '-');
		aptr = strchr (range, ':');

		if (aptr) {
			naud_chs = atoi (aptr + 1);
		} else {
			naud_chs = 2;
		}
      
		if (rptr) {
			j = atoi (rptr + 1);
          
          
			if (j <= i || j >= 64) {
				jack_error ("IEC61883CM: malformed channel range "
					    "specification '%s'", range);
			} else {
				for (k = i; k <= j; k++) {
					list = jack_slist_append (list,
								  iec61883_channel_info_new (k,
											     naud_chs));
				}
			}
		} else {
			list = jack_slist_append (list, iec61883_channel_info_new (i, naud_chs));
		}
      
	} while ( (range = strtok (NULL, ",")) );
  
	free (spec);
  
	return iec619883_channel_spec_uniq (jack_slist_sort (list, iec619883_channel_spec_compare));
}

void
iec61883_client_print_iso_ch_info (JSList * infos, FILE * file) {
	JSList * node;
	iec61883_channel_info_t * cinfo;

	for (node = infos; node; node = jack_slist_next (node)) {
		cinfo = (iec61883_channel_info_t *) node->data;

		fprintf (file, "%d:%d",  cinfo->iso_ch, cinfo->naud_chs);
		if (jack_slist_next (node)) {
			putchar (',');
		}

	}
}
