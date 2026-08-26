/*
 * fail.c
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

/**
 * Execute the fail task.
 */
bool fail_invoke(task_t *task, project_t *project)
{
    char *value;
    char *prop;
    
    value = hashtable_lookup(task->attribute_dict, "if");
    if (value) {
        prop = hashtable_lookup(project->property_dict, value);
        if (!parse_boolean(prop, false)) {
            return true;
        }
    }
    
    value = hashtable_lookup(task->attribute_dict, "unless");
    if (value) {
        prop = hashtable_lookup(project->property_dict, value);
        if (parse_boolean(prop, false)) {
            return true;
        }
    }
    
    value = hashtable_lookup(task->attribute_dict, "message");
    if (value) {
        task_log(task, LOG_MESSAGE, value);
    }
    
    value = hashtable_lookup(task->attribute_dict, "status");
    if (value) {
        exit(atoi(value));
    }
    
    return false;
}
