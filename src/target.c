/*
 * target.c
 * Copyright (C) 2005, 2013, 2026 Chris Burdess <dog@gnu.org>
 * 
 * This file is part of gantt.
 * 
 * gantt is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * gantt is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with gantt.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "gantt.h"

target_t *target_alloc(project_t *project)
{
    target_t *target = malloc(sizeof(target_t));
    if (!target) {
        return NULL;
    }
    
    target->project = project;
    target->name = NULL;
    target->description = NULL;
    target->task_list = NULL;
    target->depends_list = NULL;
    target->if_condition = NULL;
    target->unless_condition = NULL;
    
    return target;
}

void target_free(target_t *target)
{
    if (!target) {
        return;
    }
    
    free(target->name);
    free(target->description);
    
    /* Free tasks */
    slist_t *list = target->task_list;
    while (list) {
        task_free((task_t *)list->data);
        list = slist_next(list);
    }
    slist_free(target->task_list);
    
    /* Free dependency names (they get replaced with pointers, so only free if still strings) */
    slist_free(target->depends_list);
    
    free(target->if_condition);
    free(target->unless_condition);
    free(target);
}

bool target_init(target_t *target, xml_node_t *node)
{
    const char *val;
    char **tokens;
    char *token;
    slist_t *task_ptr = NULL;
    slist_t *token_ptr = NULL;
    task_t *task;
    
    /* Get attributes */
    val = xml_node_get_attr(node, NAME);
    if (val) {
        target->name = strdup(val);
    }
    
    val = xml_node_get_attr(node, DESCRIPTION);
    if (val) {
        target->description = strdup(val);
    }
    
    val = xml_node_get_attr(node, DEPENDS);
    if (val) {
        tokens = str_split(val, ",", -1);
        for (int i = 0; tokens && tokens[i]; i++) {
            token = str_strip(tokens[i]);
            /* Add token to list of dependencies */
            if (!token_ptr) {
                target->depends_list = slist_new(strdup(token));
                token_ptr = target->depends_list;
            } else {
                token_ptr = slist_append(token_ptr, strdup(token));
            }
        }
        str_freev(tokens);
    }
    
    val = xml_node_get_attr(node, IF);
    if (val) {
        target->if_condition = strdup(val);
    }
    
    val = xml_node_get_attr(node, UNLESS);
    if (val) {
        target->unless_condition = strdup(val);
    }
    
    /* Process children */
    xml_node_t *cur = node->children;
    while (cur) {
        /* Create task */
        task = task_alloc(target);
        if (!task) {
            return false;
        }
        if (!task_init(task, cur)) {
            task_free(task);
            return false;
        }
        
        /* Add task to list */
        if (!task_ptr) {
            target->task_list = slist_new(task);
            task_ptr = target->task_list;
        } else {
            task_ptr = slist_append(task_ptr, task);
        }
        
        cur = cur->next;
    }
    
    /* Validation */
    if (!target->name) {
        fprintf(stderr, "Target does not have a name.\n");
        return false;
    }
    
    return true;
}

/**
 * Invokes a target.
 * Returns true on success.
 */
bool target_invoke(target_t *target, project_t *project,
                   hashtable_t *context, hashtable_t *completed)
{
    slist_t *list;
    slist_t *depends;
    task_t *task;
    
    if (hashtable_lookup(completed, target->name)) {
        return true;  /* This target has been executed */
    }
    
    if (hashtable_lookup(context, target->name)) {
        /* Circular dependency */
        fprintf(stderr, "This target is recursively defined: %s\n", target->name);
        return false;
    }
    
    hashtable_insert(context, target->name, target);
    
    /* Execute dependencies first */
    depends = target->depends_list;
    while (depends) {
        if (!target_invoke(depends->data, project, context, completed)) {
            return false;
        }
        depends = slist_next(depends);
    }
    
    hashtable_remove(context, target->name);
    
    printf("\n%s:\n", target->name);
    
    /* Execute tasks */
    list = target->task_list;
    while (list) {
        task = list->data;
        if (!task_invoke(task, project)) {
            return false;
        }
        list = slist_next(list);
    }
    
    /* Complete */
    hashtable_insert(completed, target->name, target);
    return true;
}
