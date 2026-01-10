/*
 * javadoc.c
 * Copyright (C) 2005, 2026 Chris Burdess <dog@bluezoo.org>
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
 * Execute the javadoc task.
 * 
 * Supported attributes:
 *   destdir           - Output directory (required)
 *   sourcepath        - Source path
 *   sourcepathref     - Reference to source path
 *   classpath         - Classpath for referenced classes
 *   classpathref      - Reference to classpath
 *   bootclasspath     - Boot classpath
 *   bootclasspathref  - Reference to boot classpath
 *   packagenames      - Package patterns to document (comma-separated)
 *   excludepackagenames - Packages to exclude
 *   overview          - Overview HTML file
 *   access            - Access level: "public", "protected", "package", "private"
 *   public            - Show only public (boolean)
 *   protected         - Show protected and public (boolean)
 *   package           - Show package, protected, public (boolean)
 *   private           - Show all (boolean)
 *   verbose           - Verbose output (boolean)
 *   version           - Include @version (boolean)
 *   use               - Include Use pages (boolean)
 *   author            - Include @author (boolean)
 *   splitindex        - Split index A-Z (boolean)
 *   nodeprecated      - Exclude deprecated (boolean)
 *   nodeprecatedlist  - No deprecated list (boolean)
 *   noindex           - No index (boolean)
 *   nonavbar          - No navigation bar (boolean)
 *   notree            - No tree page (boolean)
 *   nohelp            - No help link (boolean)
 *   windowtitle       - Browser window title
 *   doctitle          - Document title HTML
 *   header            - Header HTML
 *   footer            - Footer HTML
 *   bottom            - Bottom HTML
 *   source            - Source version (e.g., "1.8", "11")
 *   encoding          - Source file encoding
 *   docencoding       - Output encoding
 *   charset           - HTML charset
 *   linksource        - Include source links (boolean)
 *   link              - External documentation URL
 *   failonerror       - Fail on error (boolean, default: true)
 *   executable        - Custom javadoc executable path
 * 
 * Nested elements:
 *   <fileset>         - Source files
 *   <sourcepath>      - Source path elements
 *   <classpath>       - Classpath elements
 *   <bootclasspath>   - Boot classpath elements
 *   <arg>             - Additional arguments
 *   <link>            - External documentation links (href attribute)
 * 
 * Not supported:
 *   doclet            - Custom doclet class
 *   docletpath        - Path to doclet
 *   <tag>             - Custom tag definitions
 *   <taglet>          - Custom taglet classes
 *   <group>           - Package grouping
 */
bool javadoc_invoke(task_t *task, project_t *project)
{
    unsigned int argc;
    char *javadoc_cmd;
    char *executable;
    char **argv;
    slist_t *arg_list;
    slist_t *arg_ptr;
    char *value;
    char *sourcepath;
    char *destdir;
    char *classpath;
    char *access;
    slist_t *fileset_ptr;
    slist_t *file_list;
    slist_t *file_ptr;
    slist_t *extra_args;
    slist_t *packages = NULL;
    slist_t *pkg_ptr = NULL;
    slist_t *path_list;
    int i, file_count;
    size_t len;
    bool ret;
    bool failonerror;
    fileset_t *fileset;
    char message[512];
    
    /* Options */
    failonerror = parse_boolean(hashtable_lookup(task->attribute_dict, "failonerror"), true);
    
    /* Locate executable */
    value = hashtable_lookup(task->attribute_dict, "executable");
    if (value) {
        javadoc_cmd = resolve_variables(strdup(value), project);
    } else {
        javadoc_cmd = hashtable_lookup(project->property_dict, "build.javadoc");
        if (!javadoc_cmd) {
            javadoc_cmd = JAVADOC;
        }
        javadoc_cmd = strdup(javadoc_cmd);
    }
    
    executable = find_executable(javadoc_cmd);
    if (!executable) {
        fprintf(stderr, "%s: javadoc command not found\n", javadoc_cmd);
        free(javadoc_cmd);
        return !failonerror;
    }
    free(javadoc_cmd);
    
    /* Build list of arguments */
    arg_list = slist_new(executable);
    arg_ptr = arg_list;
    
    /* Access level - check specific booleans first, then 'access' attribute */
    if (parse_boolean(hashtable_lookup(task->attribute_dict, "private"), false)) {
        arg_ptr = slist_append(arg_ptr, strdup("-private"));
    } else if (parse_boolean(hashtable_lookup(task->attribute_dict, "package"), false)) {
        arg_ptr = slist_append(arg_ptr, strdup("-package"));
    } else if (parse_boolean(hashtable_lookup(task->attribute_dict, "protected"), false)) {
        arg_ptr = slist_append(arg_ptr, strdup("-protected"));
    } else if (parse_boolean(hashtable_lookup(task->attribute_dict, "public"), false)) {
        arg_ptr = slist_append(arg_ptr, strdup("-public"));
    } else {
        access = hashtable_lookup(task->attribute_dict, "access");
        if (access) {
            if (strcasecmp(access, "private") == 0) {
                arg_ptr = slist_append(arg_ptr, strdup("-private"));
            } else if (strcasecmp(access, "package") == 0) {
                arg_ptr = slist_append(arg_ptr, strdup("-package"));
            } else if (strcasecmp(access, "protected") == 0) {
                arg_ptr = slist_append(arg_ptr, strdup("-protected"));
            } else if (strcasecmp(access, "public") == 0) {
                arg_ptr = slist_append(arg_ptr, strdup("-public"));
            }
        }
    }
    
    /* Boolean flags */
    if (parse_boolean(hashtable_lookup(task->attribute_dict, "verbose"), false)) {
        arg_ptr = slist_append(arg_ptr, strdup("-verbose"));
    }
    if (parse_boolean(hashtable_lookup(task->attribute_dict, "version"), false)) {
        arg_ptr = slist_append(arg_ptr, strdup("-version"));
    }
    if (parse_boolean(hashtable_lookup(task->attribute_dict, "use"), false)) {
        arg_ptr = slist_append(arg_ptr, strdup("-use"));
    }
    if (parse_boolean(hashtable_lookup(task->attribute_dict, "author"), false)) {
        arg_ptr = slist_append(arg_ptr, strdup("-author"));
    }
    if (parse_boolean(hashtable_lookup(task->attribute_dict, "splitindex"), false)) {
        arg_ptr = slist_append(arg_ptr, strdup("-splitindex"));
    }
    if (parse_boolean(hashtable_lookup(task->attribute_dict, "nodeprecated"), false)) {
        arg_ptr = slist_append(arg_ptr, strdup("-nodeprecated"));
    }
    if (parse_boolean(hashtable_lookup(task->attribute_dict, "nodeprecatedlist"), false)) {
        arg_ptr = slist_append(arg_ptr, strdup("-nodeprecatedlist"));
    }
    if (parse_boolean(hashtable_lookup(task->attribute_dict, "noindex"), false)) {
        arg_ptr = slist_append(arg_ptr, strdup("-noindex"));
    }
    if (parse_boolean(hashtable_lookup(task->attribute_dict, "nonavbar"), false)) {
        arg_ptr = slist_append(arg_ptr, strdup("-nonavbar"));
    }
    if (parse_boolean(hashtable_lookup(task->attribute_dict, "notree"), false)) {
        arg_ptr = slist_append(arg_ptr, strdup("-notree"));
    }
    if (parse_boolean(hashtable_lookup(task->attribute_dict, "nohelp"), false)) {
        arg_ptr = slist_append(arg_ptr, strdup("-nohelp"));
    }
    if (parse_boolean(hashtable_lookup(task->attribute_dict, "linksource"), false)) {
        arg_ptr = slist_append(arg_ptr, strdup("-linksource"));
    }
    
    /* Source version */
    value = hashtable_lookup(task->attribute_dict, "source");
    if (value) {
        arg_ptr = slist_append(arg_ptr, strdup("-source"));
        arg_ptr = slist_append(arg_ptr, resolve_variables(strdup(value), project));
    }
    
    /* Encoding options */
    value = hashtable_lookup(task->attribute_dict, "encoding");
    if (value) {
        arg_ptr = slist_append(arg_ptr, strdup("-encoding"));
        arg_ptr = slist_append(arg_ptr, resolve_variables(strdup(value), project));
    }
    value = hashtable_lookup(task->attribute_dict, "docencoding");
    if (value) {
        arg_ptr = slist_append(arg_ptr, strdup("-docencoding"));
        arg_ptr = slist_append(arg_ptr, resolve_variables(strdup(value), project));
    }
    value = hashtable_lookup(task->attribute_dict, "charset");
    if (value) {
        arg_ptr = slist_append(arg_ptr, strdup("-charset"));
        arg_ptr = slist_append(arg_ptr, resolve_variables(strdup(value), project));
    }
    
    /* Text/HTML content options */
    value = hashtable_lookup(task->attribute_dict, "windowtitle");
    if (value) {
        arg_ptr = slist_append(arg_ptr, strdup("-windowtitle"));
        arg_ptr = slist_append(arg_ptr, resolve_variables(strdup(value), project));
    }
    value = hashtable_lookup(task->attribute_dict, "doctitle");
    if (value) {
        arg_ptr = slist_append(arg_ptr, strdup("-doctitle"));
        arg_ptr = slist_append(arg_ptr, resolve_variables(strdup(value), project));
    }
    value = hashtable_lookup(task->attribute_dict, "header");
    if (value) {
        arg_ptr = slist_append(arg_ptr, strdup("-header"));
        arg_ptr = slist_append(arg_ptr, resolve_variables(strdup(value), project));
    }
    value = hashtable_lookup(task->attribute_dict, "footer");
    if (value) {
        arg_ptr = slist_append(arg_ptr, strdup("-footer"));
        arg_ptr = slist_append(arg_ptr, resolve_variables(strdup(value), project));
    }
    value = hashtable_lookup(task->attribute_dict, "bottom");
    if (value) {
        arg_ptr = slist_append(arg_ptr, strdup("-bottom"));
        arg_ptr = slist_append(arg_ptr, resolve_variables(strdup(value), project));
    }
    
    /* Overview file */
    value = hashtable_lookup(task->attribute_dict, "overview");
    if (value) {
        arg_ptr = slist_append(arg_ptr, strdup("-overview"));
        arg_ptr = slist_append(arg_ptr, expand_location(project, resolve_variables(strdup(value), project)));
    }
    
    /* External links */
    value = hashtable_lookup(task->attribute_dict, "link");
    if (value) {
        arg_ptr = slist_append(arg_ptr, strdup("-link"));
        arg_ptr = slist_append(arg_ptr, resolve_variables(strdup(value), project));
    }
    
    /* sourcepath */
    value = hashtable_lookup(task->attribute_dict, SOURCEPATH);
    if (!value) {
        value = hashtable_lookup(task->attribute_dict, "sourcepathref");
        if (value) {
            path_list = hashtable_lookup(project->path_dict, value);
            if (path_list) {
                slist_t *resolved = resolve_path(path_list, project);
                value = list_to_string(resolved, PATH_SEPARATOR_S);
                slist_free(resolved);
            } else {
                value = NULL;
            }
        }
    }
    if (value) {
        sourcepath = resolve_variables(strdup(value), project);
        arg_ptr = slist_append(arg_ptr, strdup("-sourcepath"));
        arg_ptr = slist_append(arg_ptr, sourcepath);
    } else {
        sourcepath = get_current_dir();
    }
    
    /* classpath */
    classpath = hashtable_lookup(task->attribute_dict, CLASSPATH);
    if (!classpath) {
        value = hashtable_lookup(task->attribute_dict, "classpathref");
        if (value) {
            path_list = hashtable_lookup(project->path_dict, value);
            if (path_list) {
                slist_t *resolved = resolve_path(path_list, project);
                classpath = list_to_string(resolved, PATH_SEPARATOR_S);
                slist_free(resolved);
            }
        }
    } else {
        classpath = resolve_variables(strdup(classpath), project);
    }
    if (classpath) {
        arg_ptr = slist_append(arg_ptr, strdup("-classpath"));
        arg_ptr = slist_append(arg_ptr, classpath);
    }
    
    /* bootclasspath */
    value = hashtable_lookup(task->attribute_dict, "bootclasspath");
    if (!value) {
        value = hashtable_lookup(task->attribute_dict, "bootclasspathref");
        if (value) {
            path_list = hashtable_lookup(project->path_dict, value);
            if (path_list) {
                slist_t *resolved = resolve_path(path_list, project);
                value = list_to_string(resolved, PATH_SEPARATOR_S);
                slist_free(resolved);
                arg_ptr = slist_append(arg_ptr, strdup("-bootclasspath"));
                arg_ptr = slist_append(arg_ptr, (char *)value);
            }
        }
    } else {
        arg_ptr = slist_append(arg_ptr, strdup("-bootclasspath"));
        arg_ptr = slist_append(arg_ptr, resolve_variables(strdup(value), project));
    }
    
    /* Exclude packages */
    value = hashtable_lookup(task->attribute_dict, "excludepackagenames");
    if (value) {
        char *excluded = resolve_variables(strdup(value), project);
        /* Convert comma-separated to colon-separated for -exclude */
        char **pkgs = str_split(excluded, ",", -1);
        for (int p = 0; pkgs && pkgs[p]; p++) {
            char *pkg = str_strip(pkgs[p]);
            if (pkg && *pkg) {
                arg_ptr = slist_append(arg_ptr, strdup("-exclude"));
                arg_ptr = slist_append(arg_ptr, strdup(pkg));
            }
        }
        str_freev(pkgs);
        free(excluded);
    }
    
    /* destdir (required) */
    destdir = hashtable_lookup(task->attribute_dict, "destdir");
    if (!destdir) {
        task_log(task, LOG_ERROR, "missing required attribute 'destdir'");
        free(executable);
        slist_free(arg_list);
        return !failonerror;
    } else {
        destdir = resolve_variables(strdup(destdir), project);
        destdir = expand_location(project, destdir);
        arg_ptr = slist_append(arg_ptr, strdup("-d"));
        arg_ptr = slist_append(arg_ptr, destdir);
    }
    
    /* Additional arguments from nested <arg> elements */
    extra_args = hashtable_lookup(task->attribute_dict, ARG);
    while (extra_args) {
        value = extra_args->data;
        if (value) {
            arg_ptr = slist_append(arg_ptr, resolve_variables(strdup(value), project));
        }
        extra_args = slist_next(extra_args);
    }
    
    /* Package names to document */
    value = hashtable_lookup(task->attribute_dict, "packagenames");
    if (value) {
        char *pkg_list = resolve_variables(strdup(value), project);
        char **pkgs = str_split(pkg_list, ",", -1);
        for (int p = 0; pkgs && pkgs[p]; p++) {
            char *pkg = str_strip(pkgs[p]);
            if (pkg && *pkg) {
                if (!packages) {
                    packages = slist_new(strdup(pkg));
                    pkg_ptr = packages;
                } else {
                    pkg_ptr = slist_append(pkg_ptr, strdup(pkg));
                }
            }
        }
        str_freev(pkgs);
        free(pkg_list);
    }
    
    /* Collect source files from filesets */
    file_count = 0;
    fileset_ptr = task->fileset_list;
    while (fileset_ptr) {
        fileset = fileset_ptr->data;
        file_list = slist_new(NULL);  /* Dummy head */
        
        if (!resolve_fileset(fileset, file_list, project)) {
            slist_free(file_list);
            slist_free(arg_list);
            if (packages) {
                slist_free(packages);
            }
            free(executable);
            return !failonerror;
        }
        
        file_ptr = slist_next(file_list);
        while (file_ptr) {
            value = file_ptr->data;
            
            /* Strip fileset dir from start of file path */
            char *dir = resolve_variables(fileset->dir, project);
            len = strlen(dir);
            if (len > 0 && value[len] == DIR_SEPARATOR) {
                len++;
            }
            value = value + len;
            
            if (dir != fileset->dir) {
                free(dir);
            }
            
            arg_ptr = slist_append(arg_ptr, strdup(value));
            file_count++;
            file_ptr = slist_next(file_ptr);
        }
        
        slist_free(file_list);
        fileset_ptr = slist_next(fileset_ptr);
    }
    
    /* Add package names after files */
    pkg_ptr = packages;
    while (pkg_ptr) {
        arg_ptr = slist_append(arg_ptr, pkg_ptr->data);
        pkg_ptr = slist_next(pkg_ptr);
    }
    if (packages) {
        slist_free(packages);
    }
    
    if (file_count == 0 && !packages) {
        task_log(task, LOG_WARNING, "No source files or packages to document");
        slist_free(arg_list);
        free(executable);
        return true;
    }
    
    /* Copy arg_list into argv */
    argc = slist_length(arg_list);
    argv = malloc(sizeof(char *) * (argc + 1));
    arg_ptr = arg_list;
    i = 0;
    while (arg_ptr) {
        argv[i++] = arg_ptr->data;
        arg_ptr = slist_next(arg_ptr);
    }
    argv[i] = NULL;
    
    slist_free(arg_list);
    
    snprintf(message, sizeof(message), "Generating Javadoc to %s", destdir);
    task_log(task, LOG_MESSAGE, message);
    
    ret = task_spawn(task, sourcepath, argv, NULL);
    
    free(executable);
    free(argv);
    
    if (!ret && failonerror) {
        return false;
    }
    
    return ret || !failonerror;
}
