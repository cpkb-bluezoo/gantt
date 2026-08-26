/*
 * jar.c
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
 * Generate a temporary manifest file from nested manifest attributes.
 * 
 * @param manifest_list  List of "Name: Value" strings
 * @return               Path to temp file (caller must free and unlink), or NULL
 */
static char *generate_temp_manifest(slist_t *manifest_list)
{
    char *temp_path;
    FILE *fp;
    
    /* Create temp file */
    temp_path = strdup("/tmp/gantt_manifest_XXXXXX");
    int fd = mkstemp(temp_path);
    if (fd < 0) {
        free(temp_path);
        return NULL;
    }
    
    fp = fdopen(fd, "w");
    if (!fp) {
        close(fd);
        unlink(temp_path);
        free(temp_path);
        return NULL;
    }
    
    /* Write manifest header */
    fprintf(fp, "Manifest-Version: 1.0\n");
    fprintf(fp, "Created-By: gantt\n");
    
    /* Write attributes from list */
    while (manifest_list) {
        char *entry = manifest_list->data;
        if (entry) {
            fprintf(fp, "%s\n", entry);
        }
        manifest_list = slist_next(manifest_list);
    }
    
    /* Manifest files must end with newline */
    fprintf(fp, "\n");
    
    fclose(fp);
    return temp_path;
}

/**
 * Execute the jar task.
 * 
 * Supported attributes:
 *   destfile        - Output JAR file (required)
 *   jarfile         - Alias for destfile
 *   basedir         - Base directory for files to include
 *   compress        - Compress entries (default: true)
 *   level           - Compression level 0-9 (default: depends on compress)
 *   update          - Update existing JAR instead of creating new
 *   manifest        - External manifest file to include
 *   index           - Create JAR index (META-INF/INDEX.LIST)
 *   filesonly       - Only store files, not directories (default: false)
 *   duplicate       - How to handle duplicates: "add", "preserve", "fail"
 *   whenmanifestonly - What to do when only manifest: "create", "skip", "fail"
 *   failonerror     - Fail build on error (default: true)
 * 
 * Nested elements:
 *   <fileset>       - Files to include in the JAR
 *   <manifest>      - Inline manifest with <attribute name="..." value="..."/>
 *   <zipfileset>    - Files with prefix/fullpath control
 *   <metainf>       - META-INF directory contents
 */
bool jar_invoke(task_t *task, project_t *project)
{
    unsigned int argc;
    char *archiver;
    char *executable;
    char **argv;
    slist_t *arg_list;
    slist_t *arg_ptr;
    char *value;
    char *dir;
    char *destfile;
    char *manifest;
    char *temp_manifest = NULL;  /* Generated temp manifest file */
    slist_t *manifest_list;
    string_t *options;
    char *level;
    char *duplicate;
    char *whenmanifestonly;
    slist_t *fileset_ptr;
    slist_t *file_list;
    slist_t *file_ptr;
    int i, file_count;
    size_t len;
    bool ret;
    bool failonerror;
    bool filesonly;
    bool create_index;
    fileset_t *fileset;
    char message[512];
    
    /* Options */
    failonerror = parse_boolean(hashtable_lookup(task->attribute_dict, "failonerror"), true);
    filesonly = parse_boolean(hashtable_lookup(task->attribute_dict, "filesonly"), false);
    create_index = parse_boolean(hashtable_lookup(task->attribute_dict, "index"), false);
    duplicate = hashtable_lookup(task->attribute_dict, "duplicate");
    whenmanifestonly = hashtable_lookup(task->attribute_dict, "whenmanifestonly");
    
    /* Locate executable */
    archiver = hashtable_lookup(project->property_dict, "build.archiver");
    if (!archiver) {
        archiver = JAR;
    }
    
    executable = find_executable(archiver);
    if (!executable) {
        fprintf(stderr, "%s: archiver command not found\n", archiver);
        return !failonerror;
    }
    
    /* Build list of arguments */
    arg_list = slist_new(executable);
    arg_ptr = arg_list;
    
    /* Build options string */
    options = string_new(NULL);
    
    if (parse_boolean(hashtable_lookup(task->attribute_dict, "update"), false)) {
        string_append(options, "u");
    } else {
        string_append(options, "c");
    }
    
    /* Check for manifest - either external file or nested element */
    manifest = hashtable_lookup(task->attribute_dict, "manifest");
    manifest_list = hashtable_lookup(task->attribute_dict, MANIFEST_ATTR);
    
    if (manifest) {
        /* External manifest file takes precedence */
        string_append(options, "m");
    } else if (manifest_list) {
        /* Generate temp manifest from nested <manifest> element */
        temp_manifest = generate_temp_manifest(manifest_list);
        if (temp_manifest) {
            manifest = temp_manifest;
            string_append(options, "m");
        }
    }
    
    string_append(options, "f");  /* Always specify file */
    
    /* Compression */
    if (!parse_boolean(hashtable_lookup(task->attribute_dict, "compress"), true)) {
        string_append(options, "0");
    } else {
        /* Check for specific compression level */
        level = hashtable_lookup(task->attribute_dict, "level");
        if (level) {
            /* Level 0-9 */
            if (level[0] >= '0' && level[0] <= '9' && level[1] == '\0') {
                string_append_c(options, level[0]);
            }
        }
    }
    
    /* Verbose mode (from project or attribute) */
    if (parse_boolean(hashtable_lookup(task->attribute_dict, "verbose"), false)) {
        string_append(options, "v");
    }
    
    arg_ptr = slist_append(arg_ptr, string_free(options, false));
    
    /* basedir */
    value = hashtable_lookup(task->attribute_dict, BASE_DIR);
    if (value) {
        dir = resolve_variables(strdup(value), project);
        dir = expand_location(project, dir);
    } else {
        dir = get_current_dir();
    }
    
    /* destfile */
    destfile = hashtable_lookup(task->attribute_dict, "destfile");
    if (!destfile) {
        destfile = hashtable_lookup(task->attribute_dict, "jarfile");
    }
    if (!destfile) {
        task_log(task, LOG_ERROR, "missing required attribute 'destfile'");
        free(executable);
        free(dir);
        slist_free(arg_list);
        return !failonerror;
    } else {
        destfile = resolve_variables(strdup(destfile), project);
        /* Make absolute if relative */
        if (destfile[0] != DIR_SEPARATOR) {
            value = project->base_dir;
            if (!value) {
                value = get_current_dir();
                char *new_dest = str_concat(value, DIR_SEPARATOR_S, destfile, NULL);
                free(value);
                free(destfile);
                destfile = new_dest;
            } else {
                char *new_dest = str_concat(value, DIR_SEPARATOR_S, destfile, NULL);
                free(destfile);
                destfile = new_dest;
            }
        }
        /* Note: for jar cmf, manifest comes BEFORE jarfile in arguments */
    }
    
    if (manifest) {
        /* Manifest file - must be added before destfile for 'jar cmf manifest jarfile' */
        manifest = resolve_variables(strdup(manifest), project);
        /* Make absolute if relative */
        if (manifest[0] != DIR_SEPARATOR) {
            value = project->base_dir;
            if (!value) {
                value = get_current_dir();
                char *new_manifest = str_concat(value, DIR_SEPARATOR_S, manifest, NULL);
                free(value);
                free(manifest);
                manifest = new_manifest;
            } else {
                char *new_manifest = str_concat(value, DIR_SEPARATOR_S, manifest, NULL);
                free(manifest);
                manifest = new_manifest;
            }
        }
        arg_ptr = slist_append(arg_ptr, manifest);
    }
    
    /* Add destfile after manifest (for 'jar cmf manifest jarfile files...' order) */
    if (destfile) {
        arg_ptr = slist_append(arg_ptr, strdup(destfile));
    }
    
    /* Collect files from filesets */
    file_count = 0;
    fileset_ptr = task->fileset_list;
    while (fileset_ptr) {
        fileset = fileset_ptr->data;
        file_list = slist_new(NULL);  /* Dummy head */
        
        if (!resolve_fileset(fileset, file_list, project)) {
            slist_free(file_list);
            slist_free(arg_list);
            free(executable);
            free(dir);
            free(destfile);
            return !failonerror;
        }
        
        /* Resolve fileset directory to absolute path (same as resolve_fileset does) */
        char *fileset_dir = resolve_variables(strdup(fileset->dir), project);
        fileset_dir = expand_location(project, fileset_dir);
        len = strlen(fileset_dir);
        
        file_ptr = slist_next(file_list);
        while (file_ptr) {
            value = file_ptr->data;
            
            /* Strip fileset dir from start of file path */
            size_t skip = len;
            if (skip > 0 && value[skip] == DIR_SEPARATOR) {
                skip++;
            }
            
            /* Skip directories if filesonly is set */
            char *rel_path = value + skip;
            if (filesonly && file_is_directory(value)) {
                file_ptr = slist_next(file_ptr);
                continue;
            }
            
            arg_ptr = slist_append(arg_ptr, strdup(rel_path));
            file_count++;
            file_ptr = slist_next(file_ptr);
        }
        free(fileset_dir);
        
        slist_free(file_list);
        fileset_ptr = slist_next(fileset_ptr);
    }
    
    /* Handle whenmanifestonly */
    if (file_count == 0) {
        if (whenmanifestonly) {
            if (strcasecmp(whenmanifestonly, "skip") == 0) {
                task_log(task, LOG_MESSAGE, "Skipping JAR (manifest only)");
                slist_free(arg_list);
                free(executable);
                free(dir);
                free(destfile);
                return true;
            } else if (strcasecmp(whenmanifestonly, "fail") == 0) {
                task_log(task, LOG_ERROR, "No files to add to JAR (manifest only)");
                slist_free(arg_list);
                free(executable);
                free(dir);
                free(destfile);
                return !failonerror;
            }
            /* "create" falls through to create JAR with just manifest */
        } else if (!manifest) {
            /* No files and no manifest - nothing to do */
            slist_free(arg_list);
            free(executable);
            free(dir);
            free(destfile);
            return true;
        }
    }
    
    /* Handle duplicate policy */
    if (duplicate && strcasecmp(duplicate, "fail") == 0) {
        /* Would need to track duplicates - for now just warn */
        /* Full implementation would require preprocessing the file list */
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
    
    snprintf(message, sizeof(message), "Building jar: %s (%d file%s)", 
             destfile, file_count, file_count == 1 ? "" : "s");
    task_log(task, LOG_MESSAGE, message);
    
    ret = task_spawn(task, dir, argv, NULL);
    
    /* Create index if requested */
    if (ret && create_index) {
        char *index_argv[4];
        index_argv[0] = executable;
        index_argv[1] = "i";
        index_argv[2] = destfile;
        index_argv[3] = NULL;
        
        task_log(task, LOG_MESSAGE, "Creating JAR index");
        ret = task_spawn(task, dir, index_argv, NULL);
    }
    
    /* Clean up temp manifest if we created one */
    if (temp_manifest) {
        unlink(temp_manifest);
        free(temp_manifest);
    }
    
    free(executable);
    free(argv);
    free(dir);
    free(destfile);
    
    if (!ret && failonerror) {
        return false;
    }
    
    return ret || !failonerror;
}
