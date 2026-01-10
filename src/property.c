/*
 * property.c
 * Copyright (C) 2005, 2013, 2026 Chris Burdess <dog@bluezoo.org>
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

extern char **environ;

/* Forward declaration */
static void property_parse_string(project_t *project, const char *contents, const char *prefix);

/**
 * If the value of this attribute is an absolute path, it is left unchanged
 * (with / and \ characters converted to the current platform's conventions).
 * Otherwise it is taken as a path relative to the project's basedir and
 * expanded.
 */
char *expand_location(project_t *project, char *location)
{
    int len;
    char *base_dir;
    char *result;
    
    location = str_delimit(location, "/\\", DIR_SEPARATOR);
    
    /* Convert to absolute filename */
    base_dir = project->base_dir;
    if (!base_dir) {
        base_dir = get_current_dir();
    }
    
    len = strlen(base_dir);
    if (len > 0 && base_dir[len - 1] != DIR_SEPARATOR) {
        char *new_base = str_concat(base_dir, DIR_SEPARATOR_S, NULL);
        if (project->base_dir != base_dir) {
            free(base_dir);
        }
        base_dir = new_base;
    }
    
    len = strlen(location);
    
    if (DIR_SEPARATOR == '\\') {
        /* Windows */
        if ((len > 0 && location[0] == '\\') ||
            (len > 2 && isalpha((unsigned char)location[0]) &&
             location[1] == ':' && location[2] == '\\') ||
            (len > 1 && location[0] == '\\' && location[1] == '\\')) {
            /* Already absolute */
            result = location;
        } else {
            result = str_concat(base_dir, location, NULL);
        }
    } else {
        /* Unix */
        if (len > 0 && location[0] != '/') {
            result = str_concat(base_dir, location, NULL);
        } else {
            result = location;
        }
    }
    
    if (project->base_dir != base_dir) {
        free(base_dir);
    }
    
    return result;
}

/**
 * Append a dot to a prefix if necessary.
 */
static char *maybe_append_dot(char *text)
{
    int pos;
    
    pos = strlen(text) - 1;
    if (pos < 0 || text[pos] != '.') {
        char *result = str_concat(text, ".", NULL);
        return result;
    }
    return strdup(text);
}

/**
 * Reads properties from a file in Java .properties format.
 * 
 * Format: name=value, one per line. Lines starting with # are comments.
 * Respects property immutability - existing properties are not overwritten.
 * 
 * @param project The project to add properties to
 * @param filename Path to the properties file
 * @param prefix Optional prefix to prepend to all property names (with dot)
 * @return true on success, false if file cannot be read
 */
bool property_read_file(project_t *project, const char *filename, const char *prefix)
{
    size_t length;
    char *contents;
    char **lines;
    char **entry;
    char *name;
    char *value;
    
    contents = file_get_contents(filename, &length);
    if (!contents) {
        return false;
    }
    
    /* Normalize line endings */
    str_delimit(contents, "\r", '\n');
    
    lines = str_split(contents, "\n", 4096);
    free(contents);
    
    for (int i = 0; lines && lines[i]; i++) {
        /* Skip empty lines and comments */
        char *line = str_strip(lines[i]);
        if (!line || !*line || line[0] == '#' || line[0] == '!') {
            continue;
        }
        
        /* Support both = and : as separators (Java properties format) */
        entry = str_split(line, "=", 2);
        if (!entry || !entry[0] || !entry[1]) {
            str_freev(entry);
            entry = str_split(line, ":", 2);
        }
        
        if (entry && entry[0]) {
            char *key = str_strip(entry[0]);
            if (prefix) {
                name = str_concat(prefix, key, NULL);
            } else {
                name = strdup(key);
            }
            value = entry[1] ? strdup(str_strip(entry[1])) : strdup("");
            
            /* Respect immutability - only set if not already defined */
            if (hashtable_lookup(project->property_dict, name) == NULL) {
                hashtable_insert(project->property_dict, name, value);
            } else {
                free(name);
                free(value);
            }
        }
        str_freev(entry);
    }
    
    str_freev(lines);
    return true;
}

/**
 * Reads properties from a URL.
 * 
 * Uses curl or wget to fetch the content, then parses as a properties file.
 * 
 * @param project The project to add properties to
 * @param url The URL to fetch properties from
 * @param prefix Optional prefix to prepend to all property names
 * @return true on success, false if URL couldn't be fetched
 */
bool property_read_url(project_t *project, const char *url, const char *prefix)
{
    char *contents = NULL;
    size_t length = 0;
    
    if (!url_fetch(url, &contents, &length)) {
        fprintf(stderr, "property: Failed to fetch URL: %s\n", url);
        return false;
    }
    
    /* Parse the fetched content as properties */
    property_parse_string(project, contents, prefix);
    free(contents);
    
    return true;
}

/**
 * Parse properties from a string in Java .properties format.
 * 
 * @param project The project to add properties to
 * @param contents The properties content string
 * @param prefix Optional prefix to prepend to all property names
 */
static void property_parse_string(project_t *project, const char *contents, const char *prefix)
{
    char **lines;
    char **entry;
    char *name;
    char *value;
    char *contents_copy;
    
    if (!contents) {
        return;
    }
    
    /* Make a mutable copy for str_delimit */
    contents_copy = strdup(contents);
    
    /* Normalize line endings */
    str_delimit(contents_copy, "\r", '\n');
    
    lines = str_split(contents_copy, "\n", 4096);
    free(contents_copy);
    
    for (int i = 0; lines && lines[i]; i++) {
        /* Skip empty lines and comments */
        char *line = str_strip(lines[i]);
        if (!line || !*line || line[0] == '#' || line[0] == '!') {
            continue;
        }
        
        /* Support both = and : as separators */
        entry = str_split(line, "=", 2);
        if (!entry || !entry[0] || !entry[1]) {
            str_freev(entry);
            entry = str_split(line, ":", 2);
        }
        
        if (entry && entry[0]) {
            char *key = str_strip(entry[0]);
            if (prefix) {
                name = str_concat(prefix, key, NULL);
            } else {
                name = strdup(key);
            }
            value = entry[1] ? strdup(str_strip(entry[1])) : strdup("");
            
            /* Respect immutability - only set if not already defined */
            if (hashtable_lookup(project->property_dict, name) == NULL) {
                hashtable_insert(project->property_dict, name, value);
            } else {
                free(name);
                free(value);
            }
        }
        str_freev(entry);
    }
    
    str_freev(lines);
}

/**
 * Reads properties from a resource file found in the classpath.
 * 
 * Searches through each directory and JAR file in the classpath.
 * Uses 'unzip' command to extract from JAR/ZIP files.
 * 
 * @param project The project context
 * @param resource The resource path (e.g., "config/defaults.properties")
 * @param classpath_str Colon-separated classpath to search
 * @param prefix Optional prefix to prepend to all property names
 * @return true if resource was found and loaded, false otherwise
 */
static bool property_read_resource(project_t *project, const char *resource, 
                                   const char *classpath_str, const char *prefix)
{
    char **paths;
    bool found = false;
    
    if (!classpath_str || !*classpath_str) {
        fprintf(stderr, "property: 'resource' requires 'classpath' or 'classpathref'\n");
        return false;
    }
    
    paths = str_split(classpath_str, ":", -1);
    
    for (int i = 0; paths && paths[i] && !found; i++) {
        char *path = str_strip(paths[i]);
        if (!path || !*path) {
            continue;
        }
        
        if (file_is_directory(path)) {
            /* Search in directory */
            char *full_path = str_concat(path, "/", resource, NULL);
                if (file_exists(full_path) && file_is_regular(full_path)) {
                found = property_read_file(project, full_path, prefix);
            }
            free(full_path);
        } else if (str_has_suffix(path, ".jar") || str_has_suffix(path, ".zip")) {
            /* Extract from JAR/ZIP archive */
            char *contents = NULL;
            size_t length = 0;
            if (archive_extract_entry(path, resource, &contents, &length)) {
                property_parse_string(project, contents, prefix);
                free(contents);
                found = true;
            }
        }
    }
    
    str_freev(paths);
    return found;
}

/**
 * Helper to set a property, respecting Ant's immutability rules.
 * Properties can only be set once; subsequent attempts are silently ignored.
 * 
 * @param project The project containing the property dictionary
 * @param name The property name
 * @param value The property value (will be freed if not inserted)
 * @return true if property was set, false if it already existed
 */
static bool set_property_if_unset(project_t *project, const char *name, char *value)
{
    if (hashtable_lookup(project->property_dict, name) != NULL) {
        /* Property already set - Ant properties are immutable */
        free(value);
        return false;
    }
    hashtable_insert(project->property_dict, strdup(name), value);
    return true;
}

/**
 * Implements the <property> task.
 * 
 * Supported attributes:
 *   - name/value: Set property to a literal value
 *   - name/location: Set property to an absolute path
 *   - name/refid: Set property to string representation of a path reference
 *   - file: Load properties from a .properties file
 *   - url: Load properties from a URL (uses curl or wget)
 *   - resource: Load properties from a classpath resource file
 *   - classpath: Classpath to search for resource (colon-separated)
 *   - classpathref: Reference to a path definition for resource search
 *   - environment: Load environment variables with a prefix
 *   - prefix: Prefix to add to all loaded property names
 * 
 * Note: Ant properties are immutable - once set, they cannot be changed.
 * 
 * @param task The task structure containing attributes
 * @param project The project context
 * @return true on success, false on error
 */
bool property_invoke(task_t *task, project_t *project)
{
    char *name;
    char *value;
    char *src;
    char *prefix;
    
    prefix = hashtable_lookup(task->attribute_dict, "prefix");
    if (prefix) {
        prefix = maybe_append_dot(prefix);
    }
    
    name = hashtable_lookup(task->attribute_dict, "name");
    if (name) {
        /* Check if property already exists (immutability) */
        if (hashtable_lookup(project->property_dict, name) != NULL) {
            /* Property already set - silently ignore per Ant semantics */
            free(prefix);
            return true;
        }
        
        value = hashtable_lookup(task->attribute_dict, "value");
        if (value) {
            value = resolve_variables(strdup(value), project);
            hashtable_insert(project->property_dict, strdup(name), value);
            free(prefix);
            return true;
        }
        
        value = hashtable_lookup(task->attribute_dict, "location");
        if (value) {
            value = resolve_variables(strdup(value), project);
            value = expand_location(project, value);
            hashtable_insert(project->property_dict, strdup(name), value);
            free(prefix);
            return true;
        }
        
        /* refid - convert a path reference to its string representation */
        value = hashtable_lookup(task->attribute_dict, "refid");
        if (value) {
            slist_t *path_list = hashtable_lookup(project->path_dict, value);
            if (path_list) {
                slist_t *resolved = resolve_path(path_list, project);
                char *path_str = list_to_string(resolved, PATH_SEPARATOR_S);
                if (path_str) {
                    hashtable_insert(project->property_dict, strdup(name), path_str);
                    free(prefix);
                    return true;
                }
            }
            fprintf(stderr, "property: refid '%s' not found\n", value);
            free(prefix);
            return false;
        }
        
        free(prefix);
        return false;
    } else {
        src = hashtable_lookup(task->attribute_dict, "file");
        if (src) {
            src = resolve_variables(strdup(src), project);
            bool result = property_read_file(project, src, prefix);
            free(src);
            free(prefix);
            return result;
        }
        
        src = hashtable_lookup(task->attribute_dict, "url");
        if (src) {
            bool result = property_read_url(project, src, prefix);
            free(prefix);
            return result;
        }
        
        /* resource - load properties from a classpath resource */
        src = hashtable_lookup(task->attribute_dict, "resource");
        if (src) {
            char *classpath_str = NULL;
            char *classpath = hashtable_lookup(task->attribute_dict, "classpath");
            char *classpathref = hashtable_lookup(task->attribute_dict, "classpathref");
            
            if (classpathref) {
                /* Look up path reference and resolve it */
                slist_t *path_list = hashtable_lookup(project->path_dict, classpathref);
                if (path_list) {
                    slist_t *resolved = resolve_path(path_list, project);
                    classpath_str = list_to_string(resolved, ":");
                }
            } else if (classpath) {
                classpath_str = resolve_variables(strdup(classpath), project);
            }
            
            src = resolve_variables(strdup(src), project);
            bool result = property_read_resource(project, src, classpath_str, prefix);
            free(src);
            free(classpath_str);
            free(prefix);
            return result;
        }
        
        src = hashtable_lookup(task->attribute_dict, "environment");
        if (src) {
            char *env_prefix = maybe_append_dot(src);
            char **env = environ;
            
            while (env && *env) {
                char **entry = str_split(*env, "=", 2);
                if (entry && entry[0]) {
                    name = str_concat(env_prefix, entry[0], NULL);
                    value = (entry[1]) ? strdup(entry[1]) : strdup("");
                    /* Use immutable set for environment properties too */
                    set_property_if_unset(project, name, value);
                    free(name);
                }
                str_freev(entry);
                env++;
            }
            
            free(env_prefix);
            free(prefix);
            return true;
        }
        
        free(prefix);
        return false;
    }
}

/**
 * Implements the <available> task.
 * 
 * Sets a property if a resource (file, directory, class, or resource) exists.
 * Searches both directories and JAR/ZIP files in the classpath.
 * 
 * Supported attributes:
 *   - property: (required) Name of property to set if resource exists
 *   - value: Value to set property to (default: "true")
 *   - file: File or directory to check for
 *   - type: "file" or "dir" to check for specific type
 *   - filepath: Path to search for the file in (colon-separated directories)
 *   - classname: Java class to check for (searches directories and JARs)
 *   - classpath/classpathref: Classpath to search for class/resource
 *   - resource: Resource file to check for in classpath (directories and JARs)
 * 
 * Note: JAR searching uses the 'unzip' command which must be available.
 * 
 * @param task The task structure containing attributes
 * @param project The project context
 * @return true on success (even if resource not found), false on error
 */
bool available_invoke(task_t *task, project_t *project)
{
    char *property;
    char *value;
    char *file;
    char *type;
    char *filepath;
    char *classname;
    char *classpath;
    char *classpathref;
    char *resource;
    bool is_available = false;
    
    /* Get required property attribute */
    property = hashtable_lookup(task->attribute_dict, "property");
    if (!property) {
        fprintf(stderr, "available: 'property' attribute is required\n");
        return false;
    }
    
    /* Get optional value (default "true") */
    value = hashtable_lookup(task->attribute_dict, "value");
    if (!value) {
        value = "true";
    }
    
    /* Get type constraint */
    type = hashtable_lookup(task->attribute_dict, "type");
    
    /* Check for file/directory */
    file = hashtable_lookup(task->attribute_dict, "file");
    if (file) {
        char *resolved_file = resolve_variables(strdup(file), project);
        filepath = hashtable_lookup(task->attribute_dict, "filepath");
        
        if (filepath) {
            /* Search in filepath directories */
            char *resolved_path = resolve_variables(strdup(filepath), project);
            char **dirs = str_split(resolved_path, ":", -1);
            
            for (int i = 0; dirs && dirs[i]; i++) {
                char *full_path = str_concat(dirs[i], "/", resolved_file, NULL);
                
                if (file_exists(full_path)) {
                    /* Check type constraint */
                    if (!type) {
                        is_available = true;
                    } else if (strcmp(type, "dir") == 0) {
                        is_available = file_is_directory(full_path);
                    } else if (strcmp(type, "file") == 0) {
                        is_available = file_is_regular(full_path);
                    }
                }
                
                free(full_path);
                if (is_available) {
                    break;
                }
            }
            
            str_freev(dirs);
            free(resolved_path);
        } else {
            /* Check file directly */
            if (file_exists(resolved_file)) {
                if (!type) {
                    is_available = true;
                } else if (strcmp(type, "dir") == 0) {
                    is_available = file_is_directory(resolved_file);
                } else if (strcmp(type, "file") == 0) {
                    is_available = file_is_regular(resolved_file);
                }
            }
        }
        
        free(resolved_file);
    }
    
    /* Check for class existence */
    classname = hashtable_lookup(task->attribute_dict, "classname");
    if (classname && !is_available) {
        char *resolved_class = resolve_variables(strdup(classname), project);
        char *class_path_str = NULL;
        
        /* Convert class name to path (com.foo.Bar -> com/foo/Bar.class) */
        char *class_file = strdup(resolved_class);
        str_delimit(class_file, ".", '/');
        char *class_file_path = str_concat(class_file, ".class", NULL);
        free(class_file);
        
        /* Get classpath */
        classpath = hashtable_lookup(task->attribute_dict, "classpath");
        classpathref = hashtable_lookup(task->attribute_dict, "classpathref");
        
        if (classpathref) {
            /* Look up path reference and resolve it */
            slist_t *path_list = hashtable_lookup(project->path_dict, classpathref);
            if (path_list) {
                slist_t *resolved = resolve_path(path_list, project);
                class_path_str = list_to_string(resolved, ":");
            }
        } else if (classpath) {
            class_path_str = resolve_variables(strdup(classpath), project);
        }
        
        if (class_path_str) {
            char **paths = str_split(class_path_str, ":", -1);
            
            for (int i = 0; paths && paths[i]; i++) {
                char *path = str_strip(paths[i]);
                if (!path || !*path) {
                    continue;
                }
                
                /* Check if it's a directory or JAR file */
                if (file_is_directory(path)) {
                    /* Search in directory */
                    char *full_path = str_concat(path, "/", class_file_path, NULL);
                    if (file_exists(full_path)) {
                        is_available = true;
                    }
                    free(full_path);
                } else if (str_has_suffix(path, ".jar") || str_has_suffix(path, ".zip")) {
                    /* Search in JAR/ZIP archive */
                    if (archive_contains_entry(path, class_file_path)) {
                        is_available = true;
                    }
                }
                
                if (is_available) {
                    break;
                }
            }
            
            str_freev(paths);
            free(class_path_str);
        }
        
        free(class_file_path);
        free(resolved_class);
    }
    
    /* Check for resource existence */
    resource = hashtable_lookup(task->attribute_dict, "resource");
    if (resource && !is_available) {
        char *resolved_resource = resolve_variables(strdup(resource), project);
        char *res_path_str = NULL;
        
        /* Get classpath */
        classpath = hashtable_lookup(task->attribute_dict, "classpath");
        classpathref = hashtable_lookup(task->attribute_dict, "classpathref");
        
        if (classpathref) {
            slist_t *path_list = hashtable_lookup(project->path_dict, classpathref);
            if (path_list) {
                slist_t *resolved = resolve_path(path_list, project);
                res_path_str = list_to_string(resolved, ":");
            }
        } else if (classpath) {
            res_path_str = resolve_variables(strdup(classpath), project);
        }
        
        if (res_path_str) {
            char **paths = str_split(res_path_str, ":", -1);
            
            for (int i = 0; paths && paths[i]; i++) {
                char *path = str_strip(paths[i]);
                if (!path || !*path) {
                    continue;
                }
                
                if (file_is_directory(path)) {
                    char *full_path = str_concat(path, "/", resolved_resource, NULL);
                    if (file_exists(full_path)) {
                        is_available = true;
                    }
                    free(full_path);
                } else if (str_has_suffix(path, ".jar") || str_has_suffix(path, ".zip")) {
                    /* Search in JAR/ZIP archive */
                    if (archive_contains_entry(path, resolved_resource)) {
                        is_available = true;
                    }
                }
                
                if (is_available) {
                    break;
                }
            }
            
            str_freev(paths);
            free(res_path_str);
        }
        
        free(resolved_resource);
    }
    
    /* Set property if available */
    if (is_available) {
        char *resolved_value = resolve_variables(strdup(value), project);
        hashtable_insert(project->property_dict, strdup(property), resolved_value);
    }
    
    return true;
}

bool basename_invoke(task_t *task, project_t *project)
{
    char *property;
    char *file;
    char *suffix;
    char *result;
    char *p;
    
    property = hashtable_lookup(task->attribute_dict, "property");
    file = hashtable_lookup(task->attribute_dict, "file");
    
    if (!property || !file) {
        return false;
    }
    
    file = resolve_variables(strdup(file), project);
    
    suffix = hashtable_lookup(task->attribute_dict, "suffix");
    if (suffix && str_has_suffix(file, suffix)) {
        int fl = strlen(file);
        int sl = strlen(suffix);
        file[fl - sl] = '\0';
    }
    
    /* Find last path separator */
    result = file;
    for (p = file; *p; p++) {
        if (*p == DIR_SEPARATOR) {
            result = p + 1;
        }
    }
    
    result = strdup(result);
    free(file);
    
    hashtable_insert(project->property_dict, property, result);
    return true;
}

bool dirname_invoke(task_t *task, project_t *project)
{
    char *property;
    char *file;
    char *p;
    
    property = hashtable_lookup(task->attribute_dict, "property");
    file = hashtable_lookup(task->attribute_dict, "file");
    
    if (!property || !file) {
        return false;
    }
    
    file = resolve_variables(strdup(file), project);
    
    /* Find last path separator and truncate */
    p = file + strlen(file);
    while (--p > file) {
        if (*p == DIR_SEPARATOR) {
            *p = '\0';
            break;
        }
    }
    
    hashtable_insert(project->property_dict, property, file);
    return true;
}

/**
 * Execute the get task - download a URL to a file.
 * 
 * Supported attributes:
 *   src           - URL to download (required)
 *   dest          - Destination file (required)
 *   verbose       - Show progress (default: false)
 *   ignoreerrors  - Don't fail on error (default: false)
 *   username      - HTTP basic auth username
 *   password      - HTTP basic auth password
 *   usetimestamp  - Only download if newer (default: false)
 *   skipexisting  - Skip if dest exists (default: false)
 *   retries       - Number of retry attempts (default: 0)
 *   maxtime       - Maximum time in seconds (default: unlimited)
 * 
 * Uses curl (preferred) or wget, with a one-time warning if neither is found.
 */
bool get_invoke(task_t *task, project_t *project)
{
    char *src;
    char *dest;
    char *username;
    char *password;
    char *retries_str;
    char *maxtime_str;
    bool verbose;
    bool ignoreerrors;
    bool usetimestamp;
    bool skipexisting;
    int retries;
    int maxtime;
    int client;
    spawn_result_t result = {0};
    char message[512];
    
    /* Required attributes */
    src = hashtable_lookup(task->attribute_dict, "src");
    dest = hashtable_lookup(task->attribute_dict, "dest");
    
    if (!src) {
        task_log(task, LOG_ERROR, "missing required attribute 'src'");
        return false;
    }
    if (!dest) {
        task_log(task, LOG_ERROR, "missing required attribute 'dest'");
        return false;
    }
    
    src = resolve_variables(strdup(src), project);
    dest = resolve_variables(strdup(dest), project);
    dest = expand_location(project, dest);
    
    /* Optional attributes */
    verbose = parse_boolean(hashtable_lookup(task->attribute_dict, "verbose"), false);
    ignoreerrors = parse_boolean(hashtable_lookup(task->attribute_dict, "ignoreerrors"), false);
    usetimestamp = parse_boolean(hashtable_lookup(task->attribute_dict, "usetimestamp"), false);
    skipexisting = parse_boolean(hashtable_lookup(task->attribute_dict, "skipexisting"), false);
    username = hashtable_lookup(task->attribute_dict, "username");
    password = hashtable_lookup(task->attribute_dict, "password");
    
    retries_str = hashtable_lookup(task->attribute_dict, "retries");
    retries = retries_str ? atoi(retries_str) : 0;
    
    maxtime_str = hashtable_lookup(task->attribute_dict, "maxtime");
    maxtime = maxtime_str ? atoi(maxtime_str) : 0;
    
    /* Skip if file exists and skipexisting is set */
    if (skipexisting && file_exists(dest)) {
        if (verbose) {
            snprintf(message, sizeof(message), "Skipping (exists): %s", dest);
            task_log(task, LOG_MESSAGE, message);
        }
        free(src);
        free(dest);
        return true;
    }
    
    /* Create parent directory if needed */
    char *parent = strdup(dest);
    char *last_sep = strrchr(parent, DIR_SEPARATOR);
    if (last_sep) {
        *last_sep = '\0';
        if (*parent && !file_is_directory(parent)) {
            /* Create directory */
            char *mkdir_argv[] = {"mkdir", "-p", parent, NULL};
            spawn_sync(NULL, mkdir_argv, NULL, NULL);
        }
    }
    free(parent);
    
    /* Check for HTTP client */
    client = check_http_client_available();
    if (client < 0) {
        free(src);
        free(dest);
        return ignoreerrors;
    }
    
    snprintf(message, sizeof(message), "Getting: %s", src);
    task_log(task, LOG_MESSAGE, message);
    
    /* Build argument list */
    slist_t *arg_list = NULL;
    slist_t *arg_ptr = NULL;
    
    if (client == 1) {
        /* Use curl */
        arg_list = slist_new("curl");
        arg_ptr = arg_list;
        
        if (!verbose) {
            arg_ptr = slist_append(arg_ptr, "-s");  /* Silent */
        }
        arg_ptr = slist_append(arg_ptr, "-f");      /* Fail on HTTP errors */
        arg_ptr = slist_append(arg_ptr, "-L");      /* Follow redirects */
        
        if (usetimestamp) {
            arg_ptr = slist_append(arg_ptr, "-z");
            arg_ptr = slist_append(arg_ptr, dest);  /* Only if newer */
        }
        
        if (username && password) {
            arg_ptr = slist_append(arg_ptr, "-u");
            char *auth = str_concat(username, ":", password, NULL);
            arg_ptr = slist_append(arg_ptr, auth);
        }
        
        if (retries > 0) {
            arg_ptr = slist_append(arg_ptr, "--retry");
            char retries_buf[16];
            snprintf(retries_buf, sizeof(retries_buf), "%d", retries);
            arg_ptr = slist_append(arg_ptr, strdup(retries_buf));
        }
        
        if (maxtime > 0) {
            arg_ptr = slist_append(arg_ptr, "--max-time");
            char maxtime_buf[16];
            snprintf(maxtime_buf, sizeof(maxtime_buf), "%d", maxtime);
            arg_ptr = slist_append(arg_ptr, strdup(maxtime_buf));
        }
        
        arg_ptr = slist_append(arg_ptr, "-o");
        arg_ptr = slist_append(arg_ptr, dest);
        arg_ptr = slist_append(arg_ptr, src);
        
    } else if (client == 2) {
        /* Use wget */
        arg_list = slist_new("wget");
        arg_ptr = arg_list;
        
        if (!verbose) {
            arg_ptr = slist_append(arg_ptr, "-q");  /* Quiet */
        }
        
        if (usetimestamp) {
            arg_ptr = slist_append(arg_ptr, "-N");  /* Timestamping */
        }
        
        if (username) {
            char *opt = str_concat("--http-user=", username, NULL);
            arg_ptr = slist_append(arg_ptr, opt);
        }
        if (password) {
            char *opt = str_concat("--http-passwd=", password, NULL);
            arg_ptr = slist_append(arg_ptr, opt);
        }
        
        if (retries > 0) {
            char *opt = malloc(32);
            snprintf(opt, 32, "--tries=%d", retries + 1);
            arg_ptr = slist_append(arg_ptr, opt);
        }
        
        if (maxtime > 0) {
            char *opt = malloc(32);
            snprintf(opt, 32, "--timeout=%d", maxtime);
            arg_ptr = slist_append(arg_ptr, opt);
        }
        
        arg_ptr = slist_append(arg_ptr, "-O");
        arg_ptr = slist_append(arg_ptr, dest);
        arg_ptr = slist_append(arg_ptr, src);
    }
    
    /* Build argv */
    int argc = slist_length(arg_list);
    char **argv = malloc(sizeof(char *) * (argc + 1));
    arg_ptr = arg_list;
    int i = 0;
    while (arg_ptr) {
        argv[i++] = arg_ptr->data;
        arg_ptr = slist_next(arg_ptr);
    }
    argv[i] = NULL;
    
    /* Execute */
    bool success = spawn_sync(NULL, argv, NULL, &result);
    success = success && (result.exit_status == 0);
    
    spawn_result_free(&result);
    slist_free(arg_list);
    free(argv);
    free(src);
    free(dest);
    
    if (!success && !ignoreerrors) {
        task_log(task, LOG_ERROR, "Download failed");
        return false;
    }
    
    return true;
}

/**
 * Execute the loadfile task - load a file's contents into a property.
 * 
 * Supported attributes:
 *   property      - Property name to store contents (required)
 *   srcFile       - Source file to load (required)
 *   encoding      - File encoding (not implemented)
 *   failonerror   - Fail if file not found (default: true)
 *   quiet         - Don't log warnings (default: false)
 */
bool loadfile_invoke(task_t *task, project_t *project)
{
    char *property;
    char *srcfile;
    char *contents;
    size_t length;
    bool failonerror;
    
    property = hashtable_lookup(task->attribute_dict, "property");
    srcfile = hashtable_lookup(task->attribute_dict, "srcFile");
    if (!srcfile) {
        srcfile = hashtable_lookup(task->attribute_dict, "srcfile");
    }
    
    if (!property) {
        task_log(task, LOG_ERROR, "missing required attribute 'property'");
        return false;
    }
    if (!srcfile) {
        task_log(task, LOG_ERROR, "missing required attribute 'srcFile'");
        return false;
    }
    
    failonerror = parse_boolean(hashtable_lookup(task->attribute_dict, "failonerror"), true);
    
    srcfile = resolve_variables(strdup(srcfile), project);
    srcfile = expand_location(project, srcfile);
    
    contents = file_get_contents(srcfile, &length);
    if (!contents) {
        if (failonerror) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Cannot read file: %s", srcfile);
            task_log(task, LOG_ERROR, msg);
            free(srcfile);
            return false;
        }
        free(srcfile);
        return true;
    }
    
    /* Remove trailing newline if present */
    if (length > 0 && contents[length - 1] == '\n') {
        contents[length - 1] = '\0';
    }
    
    hashtable_insert(project->property_dict, strdup(property), contents);
    free(srcfile);
    return true;
}

/**
 * Execute the tempfile task - create a temporary file and store its path.
 * 
 * Supported attributes:
 *   property      - Property name to store temp file path (required)
 *   destdir       - Directory for temp file (default: system temp)
 *   prefix        - Filename prefix (default: "ant")
 *   suffix        - Filename suffix (default: ".tmp")
 *   deleteonexit  - Delete when gantt exits (not implemented)
 *   createfile    - Actually create the file (default: false)
 */
bool tempfile_invoke(task_t *task, project_t *project)
{
    char *property;
    char *destdir;
    char *prefix;
    char *suffix;
    bool createfile;
    char template[512];
    char *temp_path;
    int fd;
    
    property = hashtable_lookup(task->attribute_dict, "property");
    if (!property) {
        task_log(task, LOG_ERROR, "missing required attribute 'property'");
        return false;
    }
    
    destdir = hashtable_lookup(task->attribute_dict, "destdir");
    if (!destdir) {
        destdir = getenv("TMPDIR");
        if (!destdir) {
            destdir = "/tmp";
        }
    } else {
        destdir = resolve_variables(strdup(destdir), project);
    }
    
    prefix = hashtable_lookup(task->attribute_dict, "prefix");
    if (!prefix) {
        prefix = "ant";
    }
    
    suffix = hashtable_lookup(task->attribute_dict, "suffix");
    if (!suffix) {
        suffix = ".tmp";
    }
    
    createfile = parse_boolean(hashtable_lookup(task->attribute_dict, "createfile"), false);
    
    /* Build template */
    snprintf(template, sizeof(template), "%s/%sXXXXXX%s", destdir, prefix, suffix);
    
    temp_path = strdup(template);
    
    if (createfile) {
        /* Create the file */
        fd = mkstemps(temp_path, strlen(suffix));
        if (fd < 0) {
            task_log(task, LOG_ERROR, "Failed to create temp file");
            free(temp_path);
            return false;
        }
        close(fd);
    } else {
        /* Just generate a unique name without creating */
        fd = mkstemps(temp_path, strlen(suffix));
        if (fd >= 0) {
            close(fd);
            unlink(temp_path);  /* Remove since we just wanted the name */
        }
    }
    
    hashtable_insert(project->property_dict, strdup(property), temp_path);
    return true;
}

/**
 * Execute the length task - get the length of a file or string.
 * 
 * Supported attributes:
 *   property      - Property name to store length (required)
 *   file          - File to measure
 *   string        - String to measure
 *   mode          - "each" or "all" for filesets (default: all)
 *   trim          - Trim whitespace from string (default: false)
 */
bool length_invoke(task_t *task, project_t *project)
{
    char *property;
    char *file;
    char *str;
    bool trim;
    char length_str[32];
    size_t length = 0;
    
    property = hashtable_lookup(task->attribute_dict, "property");
    if (!property) {
        task_log(task, LOG_ERROR, "missing required attribute 'property'");
        return false;
    }
    
    file = hashtable_lookup(task->attribute_dict, "file");
    str = hashtable_lookup(task->attribute_dict, "string");
    trim = parse_boolean(hashtable_lookup(task->attribute_dict, "trim"), false);
    
    if (file) {
        file = resolve_variables(strdup(file), project);
        file = expand_location(project, file);
        
        struct stat st;
        if (stat(file, &st) == 0) {
            length = st.st_size;
        }
        free(file);
    } else if (str) {
        str = resolve_variables(strdup(str), project);
        if (trim) {
            str = str_strip(str);
        }
        length = strlen(str);
        free(str);
    } else {
        task_log(task, LOG_ERROR, "must specify 'file' or 'string'");
        return false;
    }
    
    snprintf(length_str, sizeof(length_str), "%zu", length);
    hashtable_insert(project->property_dict, strdup(property), strdup(length_str));
    return true;
}

/**
 * Execute the uptodate task - check if target is up-to-date.
 * 
 * Supported attributes:
 *   property      - Property to set if up-to-date (required)
 *   value         - Value to set (default: "true")
 *   srcfile       - Source file to check
 *   targetfile    - Target file to compare against
 */
bool uptodate_invoke(task_t *task, project_t *project)
{
    char *property;
    char *value;
    char *srcfile;
    char *targetfile;
    struct stat src_stat, target_stat;
    bool uptodate = false;
    
    property = hashtable_lookup(task->attribute_dict, "property");
    if (!property) {
        task_log(task, LOG_ERROR, "missing required attribute 'property'");
        return false;
    }
    
    value = hashtable_lookup(task->attribute_dict, "value");
    if (!value) {
        value = "true";
    }
    
    srcfile = hashtable_lookup(task->attribute_dict, "srcfile");
    targetfile = hashtable_lookup(task->attribute_dict, "targetfile");
    
    if (srcfile && targetfile) {
        srcfile = resolve_variables(strdup(srcfile), project);
        srcfile = expand_location(project, srcfile);
        targetfile = resolve_variables(strdup(targetfile), project);
        targetfile = expand_location(project, targetfile);
        
        /* Target is up-to-date if it exists and is newer than source */
        if (stat(srcfile, &src_stat) == 0 && stat(targetfile, &target_stat) == 0) {
            if (target_stat.st_mtime >= src_stat.st_mtime) {
                uptodate = true;
            }
        } else if (stat(targetfile, &target_stat) == 0 && stat(srcfile, &src_stat) != 0) {
            /* Target exists but source doesn't - consider up-to-date */
            uptodate = true;
        }
        
        free(srcfile);
        free(targetfile);
    }
    
    /* TODO: Support filesets for multiple source files */
    
    if (uptodate) {
        hashtable_insert(project->property_dict, strdup(property), 
                        resolve_variables(strdup(value), project));
    }
    
    return true;
}

/* Forward declaration for nested condition evaluation */
static bool evaluate_xml_condition(xml_node_t *node, project_t *project);

/**
 * Parse a version string into components for comparison.
 * Returns number of components parsed.
 */
static int parse_version_string(const char *version, int *major, int *minor, int *patch)
{
    *major = *minor = *patch = 0;
    if (!version) {
        return 0;
    }
    return sscanf(version, "%d.%d.%d", major, minor, patch);
}

/**
 * Compare two version strings.
 * Returns: -1 if v1 < v2, 0 if v1 == v2, 1 if v1 > v2
 */
static int compare_versions(const char *v1, const char *v2)
{
    int maj1, min1, pat1, maj2, min2, pat2;
    parse_version_string(v1, &maj1, &min1, &pat1);
    parse_version_string(v2, &maj2, &min2, &pat2);
    
    if (maj1 != maj2) {
        return (maj1 < maj2) ? -1 : 1;
    }
    if (min1 != min2) {
        return (min1 < min2) ? -1 : 1;
    }
    if (pat1 != pat2) {
        return (pat1 < pat2) ? -1 : 1;
    }
    return 0;
}

/**
 * Get the current Java version from java -version
 */
static char *get_java_version(void)
{
    static char version[64] = {0};
    if (version[0]) {
        return version;
    }
    
    FILE *fp = popen("java -version 2>&1 | head -1", "r");
    if (fp) {
        char line[256];
        if (fgets(line, sizeof(line), fp)) {
            char *start = strchr(line, '"');
            if (start) {
                start++;
                char *end = strchr(start, '"');
                if (end) {
                    size_t len = end - start;
                    if (len < sizeof(version)) {
                        strncpy(version, start, len);
                        version[len] = '\0';
                    }
                }
            }
        }
        pclose(fp);
    }
    if (!version[0]) {
        strcpy(version, "11");
    }
    return version;
}

/**
 * Evaluate <javaversion> condition.
 */
static bool evaluate_javaversion(xml_node_t *node, project_t *project)
{
    const char *atleast = xml_node_get_attr(node, "atleast");
    const char *exactly = xml_node_get_attr(node, "exactly");
    char *java_version = get_java_version();
    
    if (atleast) {
        char *resolved = resolve_variables(strdup(atleast), project);
        int cmp = compare_versions(java_version, resolved);
        free(resolved);
        return cmp >= 0;
    }
    if (exactly) {
        char *resolved = resolve_variables(strdup(exactly), project);
        int cmp = compare_versions(java_version, resolved);
        free(resolved);
        return cmp == 0;
    }
    return false;
}

/**
 * Evaluate <equals> condition.
 */
static bool evaluate_equals_cond(xml_node_t *node, project_t *project)
{
    const char *arg1 = xml_node_get_attr(node, "arg1");
    const char *arg2 = xml_node_get_attr(node, "arg2");
    const char *casesensitive = xml_node_get_attr(node, "casesensitive");
    
    if (!arg1 || !arg2) {
        return false;
    }
    
    char *val1 = resolve_variables(strdup(arg1), project);
    char *val2 = resolve_variables(strdup(arg2), project);
    
    bool result;
    if (casesensitive && strcasecmp(casesensitive, "false") == 0) {
        result = (strcasecmp(val1, val2) == 0);
    } else {
        result = (strcmp(val1, val2) == 0);
    }
    
    free(val1);
    free(val2);
    return result;
}

/**
 * Evaluate <isset> condition.
 */
static bool evaluate_isset_cond(xml_node_t *node, project_t *project)
{
    const char *property = xml_node_get_attr(node, "property");
    if (!property) {
        return false;
    }
    return (hashtable_lookup(project->property_dict, property) != NULL);
}

/**
 * Evaluate <istrue> condition.
 */
static bool evaluate_istrue_cond(xml_node_t *node, project_t *project)
{
    const char *value = xml_node_get_attr(node, "value");
    if (!value) {
        return false;
    }
    char *resolved = resolve_variables(strdup(value), project);
    bool result = parse_boolean(resolved, false);
    free(resolved);
    return result;
}

/**
 * Evaluate <isfalse> condition.
 */
static bool evaluate_isfalse_cond(xml_node_t *node, project_t *project)
{
    const char *value = xml_node_get_attr(node, "value");
    if (!value) {
        return false;
    }
    char *resolved = resolve_variables(strdup(value), project);
    bool result = !parse_boolean(resolved, true);
    free(resolved);
    return result;
}

/**
 * Evaluate <os> condition.
 */
static bool evaluate_os_cond(xml_node_t *node, project_t *project)
{
    const char *family = xml_node_get_attr(node, "family");
    const char *name = xml_node_get_attr(node, "name");
    (void)project;
    
    if (family && !os_family_matches(family)) {
        return false;
    }
    if (name && !os_matches(name)) {
        return false;
    }
    return true;
}

/**
 * Evaluate <and> condition - all children must be true.
 */
static bool evaluate_and_cond(xml_node_t *node, project_t *project)
{
    for (xml_node_t *child = node->children; child; child = child->next) {
        if (!evaluate_xml_condition(child, project)) {
            return false;
        }
    }
    return true;
}

/**
 * Evaluate <or> condition - any child must be true.
 */
static bool evaluate_or_cond(xml_node_t *node, project_t *project)
{
    for (xml_node_t *child = node->children; child; child = child->next) {
        if (evaluate_xml_condition(child, project)) {
            return true;
        }
    }
    return false;
}

/**
 * Evaluate <not> condition - invert the child.
 */
static bool evaluate_not_cond(xml_node_t *node, project_t *project)
{
    xml_node_t *child = node->children;
    if (!child) {
        return true;
    }
    return !evaluate_xml_condition(child, project);
}

/**
 * Evaluate a nested XML condition node.
 */
static bool evaluate_xml_condition(xml_node_t *node, project_t *project)
{
    if (!node || !node->name) {
        return false;
    }
    
    if (xml_streq(node->name, "javaversion")) {
        return evaluate_javaversion(node, project);
    }
    if (xml_streq(node->name, "equals")) {
        return evaluate_equals_cond(node, project);
    }
    if (xml_streq(node->name, "isset")) {
        return evaluate_isset_cond(node, project);
    }
    if (xml_streq(node->name, "istrue")) {
        return evaluate_istrue_cond(node, project);
    }
    if (xml_streq(node->name, "isfalse")) {
        return evaluate_isfalse_cond(node, project);
    }
    if (xml_streq(node->name, "os")) {
        return evaluate_os_cond(node, project);
    }
    if (xml_streq(node->name, "and")) {
        return evaluate_and_cond(node, project);
    }
    if (xml_streq(node->name, "or")) {
        return evaluate_or_cond(node, project);
    }
    if (xml_streq(node->name, "not")) {
        return evaluate_not_cond(node, project);
    }
    
    /* Unknown condition type */
    fprintf(stderr, "Warning: Unknown condition type: %s\n", node->name);
    return false;
}

/**
 * Execute the condition task - set property based on conditions.
 * 
 * Supported attributes:
 *   property      - Property to set if condition is true (required)
 *   value         - Value to set (default: "true")
 *   else          - Value to set if condition is false
 * 
 * Nested conditions (checked via attributes):
 *   isset         - Check if property is set
 *   equals        - Check if two values are equal (arg1, arg2)
 *   os_family     - Check OS family
 *   os_name       - Check OS name
 *   available_file - Check if file exists
 *   available_class - Check if class exists
 *   istrue        - Check if value is "true"/"yes"/"on"
 *   isfalse       - Check if value is "false"/"no"/"off"
 * 
 * Nested XML conditions:
 *   <javaversion atleast="..."/>
 *   <equals arg1="..." arg2="..."/>
 *   <isset property="..."/>
 *   <istrue value="..."/>
 *   <isfalse value="..."/>
 *   <os family="..."/>
 *   <and>...</and>
 *   <or>...</or>
 *   <not>...</not>
 */
bool condition_invoke(task_t *task, project_t *project)
{
    char *property;
    char *value;
    char *else_value;
    bool condition_met = false;
    char *check;
    
    property = hashtable_lookup(task->attribute_dict, "property");
    if (!property) {
        task_log(task, LOG_ERROR, "missing required attribute 'property'");
        return false;
    }
    
    value = hashtable_lookup(task->attribute_dict, "value");
    if (!value) {
        value = "true";
    }
    
    else_value = hashtable_lookup(task->attribute_dict, "else");
    
    /* Check various conditions */
    
    /* isset - check if a property is set */
    check = hashtable_lookup(task->attribute_dict, "isset");
    if (check) {
        condition_met = (hashtable_lookup(project->property_dict, check) != NULL);
    }
    
    /* equals - check if two values are equal */
    char *arg1 = hashtable_lookup(task->attribute_dict, "arg1");
    char *arg2 = hashtable_lookup(task->attribute_dict, "arg2");
    if (arg1 && arg2) {
        arg1 = resolve_variables(strdup(arg1), project);
        arg2 = resolve_variables(strdup(arg2), project);
        condition_met = (strcmp(arg1, arg2) == 0);
        free(arg1);
        free(arg2);
    }
    
    /* os_family - check OS family */
    check = hashtable_lookup(task->attribute_dict, "os_family");
    if (check) {
        condition_met = os_family_matches(check);
    }
    
    /* os_name - check OS name */
    check = hashtable_lookup(task->attribute_dict, "os_name");
    if (check) {
        condition_met = os_matches(check);
    }
    
    /* available_file - check if file exists */
    check = hashtable_lookup(task->attribute_dict, "available_file");
    if (check) {
        check = resolve_variables(strdup(check), project);
        check = expand_location(project, check);
        condition_met = file_exists(check);
        free(check);
    }
    
    /* istrue - check if value is true */
    check = hashtable_lookup(task->attribute_dict, "istrue");
    if (check) {
        check = resolve_variables(strdup(check), project);
        condition_met = parse_boolean(check, false);
        free(check);
    }
    
    /* isfalse - check if value is false */
    check = hashtable_lookup(task->attribute_dict, "isfalse");
    if (check) {
        check = resolve_variables(strdup(check), project);
        condition_met = !parse_boolean(check, true);
        free(check);
    }
    
    /* Check nested XML conditions (from task->xml_node) */
    if (!condition_met && task->xml_node && task->xml_node->children) {
        /* Evaluate the first nested condition element */
        condition_met = evaluate_xml_condition(task->xml_node->children, project);
    }
    
    /* Set property based on result */
    if (condition_met) {
        hashtable_insert(project->property_dict, strdup(property),
                        resolve_variables(strdup(value), project));
    } else if (else_value) {
        hashtable_insert(project->property_dict, strdup(property),
                        resolve_variables(strdup(else_value), project));
    }
    
    return true;
}

/**
 * Execute the antcall task - call another target in the same build file.
 * 
 * Supported attributes:
 *   target        - Target to call (required)
 *   inheritAll    - Inherit all properties (default: true)
 *   inheritRefs   - Inherit all references (default: false)
 */
bool antcall_invoke(task_t *task, project_t *project);

/**
 * Execute the local task - declare a property as local to the target.
 * 
 * Supported attributes:
 *   name          - Name of the property to make local (required)
 * 
 * Note: Full property scoping is not yet implemented in gantt.
 * This task is currently a no-op - properties remain global.
 */
bool local_invoke(task_t *task, project_t *project)
{
    const char *name = hashtable_lookup(task->attribute_dict, "name");
    (void)project;
    
    if (!name) {
        task_log(task, LOG_ERROR, "local: name attribute is required");
        return false;
    }
    
    /* 
     * In Ant, this marks a property as local to the current target.
     * For now, this is a no-op since gantt doesn't support property scoping.
     * The property will be set globally when assigned later.
     */
    
    return true;
}

bool antcall_invoke(task_t *task, project_t *project)
{
    char *target_name;
    target_t *target;
    hashtable_t *context;
    hashtable_t *completed;
    bool result;
    
    target_name = hashtable_lookup(task->attribute_dict, "target");
    if (!target_name) {
        task_log(task, LOG_ERROR, "missing required attribute 'target'");
        return false;
    }
    
    target_name = resolve_variables(strdup(target_name), project);
    
    /* Find the target */
    target = hashtable_lookup(project->target_dict, target_name);
    if (!target) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Target not found: %s", target_name);
        task_log(task, LOG_ERROR, msg);
        free(target_name);
        return false;
    }
    
    free(target_name);
    
    /* Create context and completed hashtables for this invocation */
    context = hashtable_new();
    completed = hashtable_new();
    
    /* Invoke the target */
    result = target_invoke(target, project, context, completed);
    
    hashtable_free(context);
    hashtable_free(completed);
    
    return result;
}

/**
 * Execute the input task - read user input into a property.
 * 
 * Supported attributes:
 *   message       - Prompt message to display
 *   addproperty   - Property to store input (required)
 *   defaultvalue  - Default value if user just presses Enter
 *   validargs     - Comma-separated list of valid values
 */
bool input_invoke(task_t *task, project_t *project)
{
    char *message;
    char *addproperty;
    char *defaultvalue;
    char *validargs;
    char input[1024];
    char *result;
    
    addproperty = hashtable_lookup(task->attribute_dict, "addproperty");
    if (!addproperty) {
        task_log(task, LOG_ERROR, "missing required attribute 'addproperty'");
        return false;
    }
    
    /* Check if property already set (properties are immutable) */
    if (hashtable_lookup(project->property_dict, addproperty)) {
        return true;  /* Property already set, skip */
    }
    
    message = hashtable_lookup(task->attribute_dict, "message");
    defaultvalue = hashtable_lookup(task->attribute_dict, "defaultvalue");
    validargs = hashtable_lookup(task->attribute_dict, "validargs");
    
    /* Display prompt */
    if (message) {
        message = resolve_variables(strdup(message), project);
        printf("%s", message);
        if (defaultvalue) {
            printf(" [%s]", defaultvalue);
        }
        printf(" ");
        fflush(stdout);
        free(message);
    }
    
    /* Read input */
    if (fgets(input, sizeof(input), stdin) == NULL) {
        input[0] = '\0';
    }
    
    /* Remove trailing newline */
    size_t len = strlen(input);
    if (len > 0 && input[len - 1] == '\n') {
        input[len - 1] = '\0';
    }
    
    /* Use default if empty */
    if (input[0] == '\0' && defaultvalue) {
        result = resolve_variables(strdup(defaultvalue), project);
    } else {
        result = strdup(input);
    }
    
    /* Validate against valid args if specified */
    if (validargs && result[0] != '\0') {
        char *valid_copy = strdup(validargs);
        char *token = strtok(valid_copy, ",");
        bool is_valid = false;
        
        while (token) {
            /* Trim whitespace */
            while (*token == ' ') {
                token++;
            }
            char *end = token + strlen(token) - 1;
            while (end > token && *end == ' ') {
                *end-- = '\0';
            }
            
            if (strcmp(result, token) == 0) {
                is_valid = true;
                break;
            }
            token = strtok(NULL, ",");
        }
        free(valid_copy);
        
        if (!is_valid) {
            fprintf(stderr, "Invalid input. Valid values: %s\n", validargs);
            free(result);
            return false;
        }
    }
    
    hashtable_insert(project->property_dict, strdup(addproperty), result);
    return true;
}

/**
 * Execute the pathconvert task - convert paths between formats.
 * 
 * Supported attributes:
 *   property      - Property to store result (required)
 *   targetos      - Target OS: unix, windows (default: current)
 *   pathsep       - Path separator (default: based on targetos)
 *   dirsep        - Directory separator (default: based on targetos)
 *   refid         - Reference to a path to convert
 *   setonempty    - Set property even if empty (default: true)
 */
bool pathconvert_invoke(task_t *task, project_t *project)
{
    char *property;
    char *targetos;
    char *pathsep;
    char *dirsep;
    char *refid;
    bool setonempty;
    char pathsep_char;
    char dirsep_char;
    slist_t *path_list = NULL;
    char *result = NULL;
    
    property = hashtable_lookup(task->attribute_dict, "property");
    if (!property) {
        task_log(task, LOG_ERROR, "missing required attribute 'property'");
        return false;
    }
    
    targetos = hashtable_lookup(task->attribute_dict, "targetos");
    pathsep = hashtable_lookup(task->attribute_dict, "pathsep");
    dirsep = hashtable_lookup(task->attribute_dict, "dirsep");
    refid = hashtable_lookup(task->attribute_dict, "refid");
    setonempty = parse_boolean(hashtable_lookup(task->attribute_dict, "setonempty"), true);
    
    /* Determine separators based on target OS */
    if (targetos && strcasecmp(targetos, "windows") == 0) {
        pathsep_char = pathsep ? pathsep[0] : ';';
        dirsep_char = dirsep ? dirsep[0] : '\\';
    } else {
        /* Unix default */
        pathsep_char = pathsep ? pathsep[0] : ':';
        dirsep_char = dirsep ? dirsep[0] : '/';
    }
    
    /* Get path from refid */
    if (refid) {
        path_list = hashtable_lookup(project->path_dict, refid);
        if (!path_list) {
            /* Also check task's own path dict */
            path_list = hashtable_lookup(task->path_dict, refid);
        }
    }
    
    /* Check for classpath in task's path dict */
    if (!path_list && task->path_dict) {
        path_list = hashtable_lookup(task->path_dict, "classpath");
        if (!path_list) {
            path_list = hashtable_lookup(task->path_dict, "path");
        }
    }
    
    if (path_list) {
        /* Resolve and convert paths */
        slist_t *resolved = resolve_path(path_list, project);
        
        if (resolved) {
            /* Calculate total length */
            size_t total_len = 0;
            slist_t *ptr = resolved;
            while (ptr) {
                total_len += strlen(ptr->data) + 1;  /* +1 for separator */
                ptr = slist_next(ptr);
            }
            
            result = malloc(total_len + 1);
            result[0] = '\0';
            
            ptr = resolved;
            while (ptr) {
                char *path = ptr->data;
                
                /* Convert directory separators if needed */
                if (dirsep_char != '/') {
                    char *p = path;
                    while (*p) {
                        if (*p == '/') {
                            *p = dirsep_char;
                        }
                        p++;
                    }
                }
                
                if (result[0] != '\0') {
                    size_t len = strlen(result);
                    result[len] = pathsep_char;
                    result[len + 1] = '\0';
                }
                strcat(result, path);
                
                ptr = slist_next(ptr);
            }
            
            slist_free_full(resolved, free);
        }
    }
    
    /* Set property */
    if (result && result[0] != '\0') {
        hashtable_insert(project->property_dict, strdup(property), result);
    } else if (setonempty) {
        hashtable_insert(project->property_dict, strdup(property), strdup(""));
        free(result);
    } else {
        free(result);
    }
    
    return true;
}

/**
 * Execute the sequential task - run nested tasks in sequence.
 * 
 * This task runs all nested tasks one after another in order.
 * It's primarily useful inside <parallel> to group tasks that
 * must run sequentially within a parallel block.
 */
bool sequential_invoke(task_t *task, project_t *project)
{
    slist_t *nested = task->nested_tasks;
    
    while (nested) {
        task_t *nested_task = nested->data;
        if (nested_task) {
            if (!task_invoke(nested_task, project)) {
                return false;
            }
        }
        nested = slist_next(nested);
    }
    
    return true;
}

/**
 * Execute the parallel task - run nested tasks concurrently using fork().
 * 
 * Supported attributes:
 *   failonany     - Fail if any task fails (default: true)
 *   threadCount   - Max concurrent tasks (ignored, uses unlimited)
 *   timeout       - Timeout in milliseconds (not implemented)
 * 
 * Note: Property changes in child processes won't propagate back
 * to the parent due to process isolation.
 */
bool parallel_invoke(task_t *task, project_t *project)
{
    slist_t *nested = task->nested_tasks;
    bool failonany;
    int num_tasks = 0;
    pid_t *pids = NULL;
    int i;
    bool success = true;
    
    failonany = parse_boolean(hashtable_lookup(task->attribute_dict, "failonany"), true);
    
    /* Count nested tasks */
    slist_t *ptr = nested;
    while (ptr) {
        num_tasks++;
        ptr = slist_next(ptr);
    }
    
    if (num_tasks == 0) {
        return true;  /* Nothing to do */
    }
    
    /* Allocate array for child PIDs */
    pids = malloc(sizeof(pid_t) * num_tasks);
    if (!pids) {
        task_log(task, LOG_ERROR, "Failed to allocate memory for parallel tasks");
        return false;
    }
    
    /* Fork a child process for each nested task */
    i = 0;
    ptr = nested;
    while (ptr) {
        task_t *nested_task = ptr->data;
        
        pid_t pid = fork();
        if (pid < 0) {
            /* Fork failed */
            task_log(task, LOG_ERROR, "Fork failed");
            free(pids);
            return false;
        } else if (pid == 0) {
            /* Child process - execute the task and exit */
            bool result = task_invoke(nested_task, project);
            _exit(result ? 0 : 1);
        } else {
            /* Parent - record child PID */
            pids[i++] = pid;
        }
        
        ptr = slist_next(ptr);
    }
    
    /* Wait for all children to complete */
    for (i = 0; i < num_tasks; i++) {
        int status;
        waitpid(pids[i], &status, 0);
        
        if (WIFEXITED(status)) {
            if (WEXITSTATUS(status) != 0) {
                if (failonany) {
                    success = false;
                }
            }
        } else {
            /* Child was killed by signal */
            if (failonany) {
                success = false;
            }
        }
    }
    
    free(pids);
    return success;
}

/* Helper struct for xmlproperty parsing */
typedef struct {
    project_t *project;
    char *prefix;
    char *path;           /* Current element path */
    bool collapse;        /* Collapse attributes */
    bool keeproot;        /* Keep root element in path */
    bool semantic;        /* Semantic attribute processing */
    int depth;
} xmlprop_ctx_t;

/* XML element start handler for xmlproperty */
static void xmlprop_start(void *data, const char *name, const char **attrs)
{
    xmlprop_ctx_t *ctx = data;
    char new_path[1024];
    
    ctx->depth++;
    
    /* Build path */
    if (ctx->path[0] == '\0') {
        /* Root element */
        if (ctx->keeproot) {
            snprintf(new_path, sizeof(new_path), "%s%s", 
                     ctx->prefix ? ctx->prefix : "", name);
        } else {
            new_path[0] = '\0';
        }
    } else {
        snprintf(new_path, sizeof(new_path), "%s.%s", ctx->path, name);
    }
    
    free(ctx->path);
    ctx->path = strdup(new_path);
    
    /* Process attributes */
    if (attrs) {
        for (int i = 0; attrs[i]; i += 2) {
            const char *attr_name = attrs[i];
            const char *attr_value = attrs[i + 1];
            
            if (attr_value) {
                char prop_name[1024];
                if (ctx->collapse) {
                    /* Collapse: element.attribute */
                    if (ctx->path[0]) {
                        snprintf(prop_name, sizeof(prop_name), "%s.%s", 
                                 ctx->path, attr_name);
                    } else {
                        snprintf(prop_name, sizeof(prop_name), "%s%s",
                                 ctx->prefix ? ctx->prefix : "", attr_name);
                    }
                } else {
                    /* No collapse: element(attribute) */
                    if (ctx->path[0]) {
                        snprintf(prop_name, sizeof(prop_name), "%s(%s)", 
                                 ctx->path, attr_name);
                    } else {
                        snprintf(prop_name, sizeof(prop_name), "%s(%s)",
                                 name, attr_name);
                    }
                }
                
                /* Only set if not already set */
                if (!hashtable_lookup(ctx->project->property_dict, prop_name)) {
                    hashtable_insert(ctx->project->property_dict, 
                                    strdup(prop_name), strdup(attr_value));
                }
            }
        }
    }
}

/* XML element end handler for xmlproperty */
static void xmlprop_end(void *data, const char *name)
{
    xmlprop_ctx_t *ctx = data;
    (void)name;
    
    ctx->depth--;
    
    /* Pop path segment */
    char *last_dot = strrchr(ctx->path, '.');
    if (last_dot) {
        *last_dot = '\0';
    } else {
        ctx->path[0] = '\0';
    }
}

/* XML character data handler for xmlproperty */
static void xmlprop_cdata(void *data, const char *s, int len)
{
    xmlprop_ctx_t *ctx = data;
    
    /* Skip whitespace-only content */
    bool all_whitespace = true;
    for (int i = 0; i < len; i++) {
        if (!isspace((unsigned char)s[i])) {
            all_whitespace = false;
            break;
        }
    }
    if (all_whitespace) {
        return;
    }
    
    /* Set property with element text */
    if (ctx->path[0]) {
        char *value = malloc(len + 1);
        memcpy(value, s, len);
        value[len] = '\0';
        
        /* Trim */
        value = str_strip(value);
        
        if (!hashtable_lookup(ctx->project->property_dict, ctx->path)) {
            hashtable_insert(ctx->project->property_dict,
                            strdup(ctx->path), value);
        } else {
            free(value);
        }
    }
}

/**
 * Execute the xmlproperty task - load XML file as properties.
 * 
 * Supported attributes:
 *   file          - XML file to load (required)
 *   prefix        - Prefix for property names
 *   keeproot      - Include root element in property names (default: true)
 *   collapseAttributes - Use dot notation for attrs (default: true)
 *   semanticAttributes - Special handling for name/value/location (default: false)
 *   validate      - Validate XML (ignored)
 *   rootDirectory - Root directory for relative paths
 */
bool xmlproperty_invoke(task_t *task, project_t *project)
{
    char *file;
    char *prefix;
    bool keeproot, collapse;
    char *contents;
    size_t length;
    XML_Parser parser;
    xmlprop_ctx_t ctx;
    
    file = hashtable_lookup(task->attribute_dict, "file");
    if (!file) {
        task_log(task, LOG_ERROR, "missing required attribute 'file'");
        return false;
    }
    
    file = resolve_variables(strdup(file), project);
    file = expand_location(project, file);
    
    prefix = hashtable_lookup(task->attribute_dict, "prefix");
    keeproot = parse_boolean(hashtable_lookup(task->attribute_dict, "keeproot"), true);
    collapse = parse_boolean(hashtable_lookup(task->attribute_dict, "collapseAttributes"), true);
    
    /* Read file */
    contents = file_get_contents(file, &length);
    if (!contents) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Cannot read XML file: %s", file);
        task_log(task, LOG_ERROR, msg);
        free(file);
        return false;
    }
    
    /* Set up context */
    ctx.project = project;
    ctx.prefix = prefix;
    ctx.path = strdup("");
    ctx.collapse = collapse;
    ctx.keeproot = keeproot;
    ctx.semantic = parse_boolean(hashtable_lookup(task->attribute_dict, "semanticAttributes"), false);
    ctx.depth = 0;
    
    /* Create parser */
    parser = XML_ParserCreate(NULL);
    XML_SetUserData(parser, &ctx);
    XML_SetElementHandler(parser, xmlprop_start, xmlprop_end);
    XML_SetCharacterDataHandler(parser, xmlprop_cdata);
    
    /* Parse */
    if (XML_Parse(parser, contents, length, 1) == XML_STATUS_ERROR) {
        char msg[256];
        snprintf(msg, sizeof(msg), "XML parse error: %s at line %lu",
                 XML_ErrorString(XML_GetErrorCode(parser)),
                 XML_GetCurrentLineNumber(parser));
        task_log(task, LOG_ERROR, msg);
        XML_ParserFree(parser);
        free(ctx.path);
        free(contents);
        free(file);
        return false;
    }
    
    XML_ParserFree(parser);
    free(ctx.path);
    free(contents);
    free(file);
    return true;
}

/**
 * Execute the apply task - run command for each file in fileset.
 * 
 * Supported attributes:
 *   executable    - Command to run (required)
 *   dir           - Working directory
 *   dest          - Destination directory (for mapper)
 *   parallel      - Run in parallel (batch mode like xargs)
 *   type          - Type filter: file, dir, both (default: file)
 *   failonerror   - Fail if command fails (default: true)
 *   skipemptyfilesets - Skip if no files match (default: false)
 *   verbose       - Print each command (default: false)
 *   relative      - Use relative paths (default: false)
 *   forwardslash  - Force forward slashes (default: false)
 *   maxparallel   - Max files per invocation when parallel (default: unlimited)
 *   addsourcefile - Add source file to command (default: true)
 *
 * Note: "execon" is an alias for "apply"
 */
bool apply_invoke(task_t *task, project_t *project)
{
    char *executable;
    char *dir;
    bool parallel_mode;
    bool failonerror;
    bool verbose;
    bool relative;
    bool addsourcefile;
    int maxparallel;
    slist_t *files = NULL;
    slist_t *arg_list;
    fileset_t *fileset;
    
    executable = hashtable_lookup(task->attribute_dict, "executable");
    if (!executable) {
        task_log(task, LOG_ERROR, "missing required attribute 'executable'");
        return false;
    }
    
    executable = resolve_variables(strdup(executable), project);
    dir = hashtable_lookup(task->attribute_dict, "dir");
    if (dir) {
        dir = resolve_variables(strdup(dir), project);
        dir = expand_location(project, dir);
    }
    
    parallel_mode = parse_boolean(hashtable_lookup(task->attribute_dict, "parallel"), false);
    failonerror = parse_boolean(hashtable_lookup(task->attribute_dict, "failonerror"), true);
    verbose = parse_boolean(hashtable_lookup(task->attribute_dict, "verbose"), false);
    relative = parse_boolean(hashtable_lookup(task->attribute_dict, "relative"), false);
    addsourcefile = parse_boolean(hashtable_lookup(task->attribute_dict, "addsourcefile"), true);
    
    char *maxp_str = hashtable_lookup(task->attribute_dict, "maxparallel");
    maxparallel = maxp_str ? atoi(maxp_str) : 0;
    
    /* Get <arg> elements */
    arg_list = hashtable_lookup(task->attribute_dict, "arg");
    
    /* Resolve fileset */
    if (task->fileset_list) {
        /* Create dummy head for accumulator */
        files = slist_new(NULL);
        
        slist_t *fs_ptr = task->fileset_list;
        while (fs_ptr) {
            fileset = fs_ptr->data;
            if (!resolve_fileset(fileset, files, project)) {
                slist_free(files);
                files = NULL;
                break;
            }
            fs_ptr = slist_next(fs_ptr);
        }
        
        /* Skip dummy head if we have files */
        if (files) {
            slist_t *real_files = slist_next(files);
            if (!real_files) {
                /* No files matched */
                slist_free(files);
                files = NULL;
            } else {
                /* Free dummy head, keep real list */
                files->next = NULL;
                slist_free(files);
                files = real_files;
            }
        }
    }
    
    if (!files) {
        bool skipempty = parse_boolean(hashtable_lookup(task->attribute_dict, "skipemptyfilesets"), false);
        if (skipempty) {
            free(executable);
            free(dir);
            return true;
        }
        task_log(task, LOG_WARNING, "No files matched");
        free(executable);
        free(dir);
        return true;
    }
    
    bool success = true;
    
    if (parallel_mode) {
        /* Batch mode: run command once with all files as arguments */
        int file_count = slist_length(files);
        int batch_size = (maxparallel > 0) ? maxparallel : file_count;
        slist_t *batch_start = files;
        
        while (batch_start) {
            /* Build command with batch of files */
            int argc = 1;  /* executable */
            
            /* Count args */
            slist_t *arg_ptr = arg_list;
            while (arg_ptr) {
                argc++;
                arg_ptr = slist_next(arg_ptr);
            }
            
            /* Count files in this batch */
            int batch_count = 0;
            slist_t *f = batch_start;
            while (f && batch_count < batch_size) {
                batch_count++;
                f = slist_next(f);
            }
            
            if (addsourcefile) {
                argc += batch_count;
            }
            
            char **argv = malloc(sizeof(char *) * (argc + 1));
            int i = 0;
            argv[i++] = executable;
            
            /* Add args */
            arg_ptr = arg_list;
            while (arg_ptr) {
                argv[i++] = resolve_variables(strdup(arg_ptr->data), project);
                arg_ptr = slist_next(arg_ptr);
            }
            
            /* Add files */
            if (addsourcefile) {
                f = batch_start;
                int count = 0;
                while (f && count < batch_size) {
                    char *filepath = f->data;
                    if (relative) {
                        argv[i++] = strdup(filepath);
                    } else {
                        argv[i++] = expand_location(project, strdup(filepath));
                    }
                    count++;
                    f = slist_next(f);
                }
            }
            argv[i] = NULL;
            
            if (verbose) {
                printf("Executing: %s", executable);
                for (int j = 1; argv[j]; j++) {
                    printf(" %s", argv[j]);
                }
                printf("\n");
            }
            
            spawn_result_t result;
            bool ok = spawn_sync(dir, argv, NULL, &result);
            ok = ok && (result.exit_status == 0);
            spawn_result_free(&result);
            
            /* Free argv (except executable) */
            for (int j = 1; argv[j]; j++) {
                free(argv[j]);
            }
            free(argv);
            
            if (!ok && failonerror) {
                success = false;
                break;
            }
            
            /* Move to next batch */
            int skip = 0;
            while (batch_start && skip < batch_size) {
                batch_start = slist_next(batch_start);
                skip++;
            }
        }
    } else {
        /* Sequential mode: run command once per file */
        slist_t *f = files;
        while (f) {
            char *filepath = f->data;
            
            /* Build command */
            int argc = 1;  /* executable */
            slist_t *arg_ptr = arg_list;
            while (arg_ptr) {
                argc++;
                arg_ptr = slist_next(arg_ptr);
            }
            if (addsourcefile) {
                argc++;
            }
            
            char **argv = malloc(sizeof(char *) * (argc + 1));
            int i = 0;
            argv[i++] = executable;
            
            /* Add args */
            arg_ptr = arg_list;
            while (arg_ptr) {
                argv[i++] = resolve_variables(strdup(arg_ptr->data), project);
                arg_ptr = slist_next(arg_ptr);
            }
            
            /* Add file */
            if (addsourcefile) {
                if (relative) {
                    argv[i++] = strdup(filepath);
                } else {
                    argv[i++] = expand_location(project, strdup(filepath));
                }
            }
            argv[i] = NULL;
            
            if (verbose) {
                printf("Executing: %s", executable);
                for (int j = 1; argv[j]; j++) {
                    printf(" %s", argv[j]);
                }
                printf("\n");
            }
            
            spawn_result_t result;
            bool ok = spawn_sync(dir, argv, NULL, &result);
            ok = ok && (result.exit_status == 0);
            spawn_result_free(&result);
            
            /* Free argv (except executable) */
            for (int j = 1; argv[j]; j++) {
                free(argv[j]);
            }
            free(argv);
            
            if (!ok && failonerror) {
                success = false;
                break;
            }
            
            f = slist_next(f);
        }
    }
    
    slist_free_full(files, free);
    free(executable);
    free(dir);
    return success;
}
