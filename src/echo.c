/*
 * echo.c
 * Implementation of the <echo> task
 *
 * Copyright (C) 2005, 2026 Chris Burdess <dog@gnu.org>
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

/**
 * Logs a message to the console, one log line per line of text.
 */
static void echo_to_console(task_t *task, const char *message)
{
    const char *start = message;
    const char *end;
    
    for (;;) {
        char *line;
        
        end = strchr(start, '\n');
        if (!end) {
            /* A trailing newline does not start another line */
            if (start != message && *start == '\0') {
                break;
            }
            task_log(task, LOG_MESSAGE, start);
            break;
        }
        line = strndup(start, end - start);
        if (!line) {
            break;
        }
        task_log(task, LOG_MESSAGE, line);
        free(line);
        start = end + 1;
    }
}

/**
 * Writes a message to the file named by the task's "file" attribute,
 * appending if "append" is true. The text is written exactly as given: like
 * Ant, no newline is added.
 */
static bool echo_to_file(task_t *task, project_t *project, const char *file,
                         const char *message)
{
    char *path;
    FILE *fp;
    bool append;
    bool ok;
    
    append = parse_boolean(hashtable_lookup(task->attribute_dict, "append"), false);
    path = resolve_variables(strdup(file), project);
    path = expand_location(project, path);
    
    fp = fopen(path, append ? "a" : "w");
    if (!fp) {
        char msg[1024];
        snprintf(msg, sizeof(msg), "Cannot write to file: %s", path);
        task_log(task, LOG_ERROR, msg);
        free(path);
        return false;
    }
    
    ok = fputs(message, fp) >= 0;
    if (fclose(fp) != 0) {
        ok = false;
    }
    if (!ok) {
        char msg[1024];
        snprintf(msg, sizeof(msg), "Error writing to file: %s", path);
        task_log(task, LOG_ERROR, msg);
    }
    free(path);
    return ok;
}

/**
 * Execute the echo task.
 */
bool echo_invoke(task_t *task, project_t *project)
{
    char *message;
    char *file;
    bool ok = true;
    
    message = hashtable_lookup(task->attribute_dict, "message");
    message = resolve_variables(strdup(message ? message : ""), project);
    
    file = hashtable_lookup(task->attribute_dict, "file");
    if (file) {
        ok = echo_to_file(task, project, file, message);
    } else {
        echo_to_console(task, message);
    }
    
    free(message);
    return ok;
}
