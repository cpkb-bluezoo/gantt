/*
 * tstamp.c
 * Copyright (C) 2005, 2026 Chris Burdess <dog@gnu.org>
 * 
 * This file is part of gantt.
 * 
 * gantt is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * gantt is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "gantt.h"
#include <time.h>

static void tstamp_insert_property(task_t *task, project_t *project,
                                   struct tm *tm_time, const char *prefix,
                                   const char *format_suffix, const char *property)
{
    size_t size;
    char *value;
    char *format;
    char *prop_name;
    
    (void)task;  /* Unused */
    
    size = prefix ? strlen(prefix) + 18 : 18;
    value = malloc(sizeof(char) * size);
    if (!value) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    
    if (prefix) {
        format = str_concat(prefix, format_suffix, NULL);
    } else {
        format = (char *)format_suffix;
    }
    
    strftime(value, size, format, tm_time);
    
    if (prefix) {
        prop_name = str_concat(prefix, property, NULL);
        free(format);
    } else {
        prop_name = strdup(property);
    }
    
    hashtable_insert(project->property_dict, prop_name, value);
    free(prop_name);
}

bool tstamp_invoke(task_t *task, project_t *project)
{
    time_t now;
    struct tm *tm_now;
    char *prefix;
    
    /* Get current date */
    if (time(&now) == -1) {
        perror("time");
        exit(EXIT_FAILURE);
    }
    tm_now = localtime(&now);
    
    prefix = hashtable_lookup(task->attribute_dict, "prefix");
    
    /* DSTAMP */
    tstamp_insert_property(task, project, tm_now, prefix, "%Y%m%d", "DSTAMP");
    
    /* TSTAMP */
    tstamp_insert_property(task, project, tm_now, prefix, "%H%M", "TSTAMP");
    
    /* TODAY */
    tstamp_insert_property(task, project, tm_now, prefix, "%B %d %Y", "TODAY");
    
    return true;
}
