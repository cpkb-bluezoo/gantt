/*
 * javac.c
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
#include <dirent.h>

/**
 * Check if any .class file in a directory is newer than the given timestamp.
 * Used for package-info.java files which don't generate their own .class file,
 * and for source files that define types with different names than the file.
 * 
 * @param dir_path      Package directory in the destination
 * @param source_mtime  Modification time of the source file
 * @param class_prefix  If non-NULL, only check class files starting with this prefix
 *                      (for cases like Foo.java generating Foo$Inner.class)
 */
static bool has_newer_class_in_dir(const char *dir_path, time_t source_mtime, 
                                   const char *class_prefix)
{
    DIR *dir;
    struct dirent *entry;
    struct stat st;
    char *filepath;
    bool found = false;
    size_t prefix_len = class_prefix ? strlen(class_prefix) : 0;
    
    dir = opendir(dir_path);
    if (!dir) {
        return false;
    }
    
    while ((entry = readdir(dir)) != NULL) {
        size_t len = strlen(entry->d_name);
        if (len > 6 && strcmp(entry->d_name + len - 6, ".class") == 0) {
            /* If class_prefix specified, check if name starts with it */
            if (class_prefix) {
                if (strncmp(entry->d_name, class_prefix, prefix_len) != 0) {
                    continue;
                }
                /* Must be followed by .class or $something.class */
                if (len != prefix_len + 6 && entry->d_name[prefix_len] != '$') {
                    continue;
                }
            }
            
            filepath = str_concat(dir_path, DIR_SEPARATOR_S, entry->d_name, NULL);
            if (stat(filepath, &st) == 0 && st.st_mtime >= source_mtime) {
                found = true;
                free(filepath);
                break;
            }
            free(filepath);
        }
    }
    
    closedir(dir);
    return found;
}

/**
 * Check if a source file needs recompilation.
 * Returns true if the source file is newer than the corresponding .class file,
 * or if the .class file doesn't exist.
 * 
 * Special handling for package-info.java files, which may not generate a
 * .class file but should be skipped if any .class file in the same package
 * directory is up-to-date.
 * 
 * @param source_file  Full path to .java source file
 * @param src_dir      Source directory (to compute relative path)
 * @param dest_dir     Destination directory for .class files
 * @return             true if file needs compilation
 */
static bool needs_compilation(const char *source_file, const char *src_dir, 
                              const char *dest_dir)
{
    struct stat src_stat, class_stat;
    char *class_file;
    char *relative_path;
    char *package_dir;
    size_t src_dir_len;
    size_t src_len;
    bool needs_compile;
    const char *basename;
    
    /* If no destdir, always compile */
    if (!dest_dir) {
        return true;
    }
    
    /* Get source file stats */
    if (stat(source_file, &src_stat) != 0) {
        return true;  /* Can't stat source - try to compile anyway */
    }
    
    /* Compute relative path from source directory */
    src_dir_len = strlen(src_dir);
    if (src_dir_len > 0 && src_dir[src_dir_len - 1] != DIR_SEPARATOR) {
        src_dir_len++;  /* Account for separator */
    }
    
    if (strncmp(source_file, src_dir, strlen(src_dir)) == 0) {
        relative_path = strdup(source_file + src_dir_len);
    } else {
        /* Source file not under src_dir - just use filename */
        basename = strrchr(source_file, DIR_SEPARATOR);
        relative_path = strdup(basename ? basename + 1 : source_file);
    }
    
    /* Check for package-info.java - special handling */
    basename = strrchr(relative_path, DIR_SEPARATOR);
    if (!basename) {
        basename = relative_path;
    } else {
        basename++;
    }
    
    /* Get package directory path */
    char *rel_dir;
    char *class_basename;
    if (basename == relative_path) {
        rel_dir = strdup("");
        class_basename = strdup(relative_path);
    } else {
        rel_dir = strdup(relative_path);
        rel_dir[basename - relative_path - 1] = '\0';
        class_basename = strdup(basename);
    }
    
    package_dir = str_concat(dest_dir, DIR_SEPARATOR_S, rel_dir, NULL);
    free(rel_dir);
    
    if (strcmp(basename, "package-info.java") == 0) {
        /* For package-info.java, check if any .class in dest package is newer.
         * If the directory exists but has no .class files (only subdirectories),
         * check if any subdirectory has .class files that are newer. */
        if (has_newer_class_in_dir(package_dir, src_stat.st_mtime, NULL)) {
            /* Found a newer class file - up to date */
            needs_compile = false;
        } else {
            /* No class files directly in package dir - check subdirectories */
            DIR *dir = opendir(package_dir);
            needs_compile = true;  /* Assume needs compilation */
            if (dir) {
                struct dirent *entry;
                while ((entry = readdir(dir)) != NULL) {
                    if (entry->d_name[0] == '.') {
                        continue;
                    }
                    char *subdir = str_concat(package_dir, DIR_SEPARATOR_S, entry->d_name, NULL);
                    struct stat st;
                    if (stat(subdir, &st) == 0 && S_ISDIR(st.st_mode)) {
                        if (has_newer_class_in_dir(subdir, src_stat.st_mtime, NULL)) {
                            needs_compile = false;
                            free(subdir);
                            break;
                        }
                    }
                    free(subdir);
                }
                closedir(dir);
            }
        }
        free(package_dir);
        free(class_basename);
        free(relative_path);
        return needs_compile;
    }
    
    /* Replace .java extension with .class */
    src_len = strlen(class_basename);
    if (src_len > 5 && strcmp(class_basename + src_len - 5, ".java") == 0) {
        class_basename[src_len - 5] = '\0';  /* Remove .java */
        class_file = str_concat(package_dir, DIR_SEPARATOR_S, class_basename, ".class", NULL);
    } else {
        /* Not a .java file - compile it */
        free(package_dir);
        free(class_basename);
        free(relative_path);
        return true;
    }
    
    /* Check if class file exists and compare timestamps */
    if (stat(class_file, &class_stat) != 0) {
        /* Class file doesn't exist - maybe source defines different type names.
         * Check if any class file in the package is newer than the source. */
        free(class_file);
        needs_compile = !has_newer_class_in_dir(package_dir, src_stat.st_mtime, NULL);
        free(package_dir);
        free(class_basename);
        free(relative_path);
        return needs_compile;
    }
    
    /* Compare modification times */
    needs_compile = (src_stat.st_mtime > class_stat.st_mtime);
    
    free(class_file);
    free(package_dir);
    free(class_basename);
    free(relative_path);
    return needs_compile;
}

/**
 * Execute the javac task.
 * 
 * Supported attributes:
 *   srcdir          - Source directory (becomes -sourcepath)
 *   destdir         - Destination directory for .class files (-d)
 *   classpath       - Classpath for compilation
 *   classpathref    - Reference to a path for classpath
 *   sourcepath      - Source path (alternative to srcdir)
 *   sourcepathref   - Reference to a path for sourcepath
 *   bootclasspath   - Bootstrap classpath
 *   bootclasspathref - Reference to bootstrap classpath
 *   extdirs         - Extension directories
 *   encoding        - Source file encoding (-encoding)
 *   source          - Source compatibility version (-source)
 *   target          - Target JVM version (-target)
 *   release         - Combined source/target for JDK 9+ (--release)
 *   debug           - Include debug info (-g or -g:none)
 *   debuglevel      - Debug info level (-g:lines,vars,source)
 *   deprecation     - Warn about deprecated APIs (-deprecation)
 *   nowarn          - Suppress warnings (-nowarn)
 *   verbose         - Verbose output (-verbose)
 *   failonerror     - Fail build on compilation error (default: true)
 *   listfiles       - List each source file being compiled
 *   includeDestClasses - Include destdir in classpath (default: true)
 *   executable      - Path to javac executable
 *   fork            - Ignored (we always fork)
 *   compiler        - Ignored (use executable instead)
 * 
 * Nested elements:
 *   <classpath>     - Classpath elements
 *   <sourcepath>    - Source path elements
 *   <bootclasspath> - Bootstrap classpath elements
 *   <compilerarg>   - Additional compiler arguments
 *   <src>           - Source directories (fileset)
 */
bool javac_invoke(task_t *task, project_t *project)
{
    unsigned int argc;
    char *compiler;
    char *executable;
    char *dir;
    char **argv;
    slist_t *arg_list;
    slist_t *arg_ptr;
    char *value;
    char *dest_dir;
    slist_t *path_list;
    slist_t *fileset_ptr;
    slist_t *file_list;
    slist_t *file_ptr;
    slist_t *compilerarg_list;
    int i, file_count;
    bool ret;
    bool failonerror;
    bool listfiles;
    bool includeDestClasses;
    fileset_t *fileset;
    char message[512];
    char *classpath_str = NULL;
    
    /* Options */
    failonerror = parse_boolean(hashtable_lookup(task->attribute_dict, "failonerror"), true);
    listfiles = parse_boolean(hashtable_lookup(task->attribute_dict, "listfiles"), false);
    includeDestClasses = parse_boolean(hashtable_lookup(task->attribute_dict, "includeDestClasses"), true);
    
    /* Get executable - check 'executable' attribute first, then build.compiler property */
    executable = hashtable_lookup(task->attribute_dict, "executable");
    if (executable) {
        executable = resolve_variables(strdup(executable), project);
        char *found = find_executable(executable);
        if (!found) {
            /* Try as absolute path */
            if (!file_is_executable(executable)) {
                fprintf(stderr, "%s: compiler not found\n", executable);
                free(executable);
                return !failonerror;
            }
            found = executable;
        } else {
            free(executable);
            executable = found;
        }
    } else {
        compiler = hashtable_lookup(project->property_dict, "build.compiler");
        if (!compiler || strcmp(compiler, "modern") == 0 || strcmp(compiler, "javac1.8") == 0 ||
            strcmp(compiler, "javac1.9") == 0 || strcmp(compiler, "javac10+") == 0) {
            /* "modern" and versioned compilers all map to the standard javac */
            compiler = JAVAC;
        }
        executable = find_executable(compiler);
        if (!executable) {
            fprintf(stderr, "%s: compiler command not found\n", compiler);
            return !failonerror;
        }
    }
    
    dir = get_current_dir();
    
    /* Build list of arguments */
    arg_list = slist_new(executable);
    arg_ptr = arg_list;
    
    /* debug */
    value = hashtable_lookup(task->attribute_dict, "debug");
    if (value) {
        if (parse_boolean(value, false)) {
            char *debuglevel = hashtable_lookup(task->attribute_dict, "debuglevel");
            if (debuglevel) {
                char *debug_arg = str_concat("-g:", debuglevel, NULL);
                arg_ptr = slist_append(arg_ptr, debug_arg);
            } else {
                arg_ptr = slist_append(arg_ptr, strdup("-g"));
            }
        } else {
            arg_ptr = slist_append(arg_ptr, strdup("-g:none"));
        }
    }
    
    /* deprecation */
    if (parse_boolean(hashtable_lookup(task->attribute_dict, "deprecation"), false)) {
        arg_ptr = slist_append(arg_ptr, strdup("-deprecation"));
    }
    
    /* nowarn */
    if (parse_boolean(hashtable_lookup(task->attribute_dict, "nowarn"), false)) {
        arg_ptr = slist_append(arg_ptr, strdup("-nowarn"));
    }
    
    /* verbose */
    if (parse_boolean(hashtable_lookup(task->attribute_dict, "verbose"), false)) {
        arg_ptr = slist_append(arg_ptr, strdup("-verbose"));
    }
    
    /* release (JDK 9+) - if set, this overrides source/target */
    value = hashtable_lookup(task->attribute_dict, "release");
    if (value) {
        value = resolve_variables(strdup(value), project);
        arg_ptr = slist_append(arg_ptr, strdup("--release"));
        arg_ptr = slist_append(arg_ptr, value);
    } else {
        /* source version (only if release not set) */
        value = hashtable_lookup(task->attribute_dict, "source");
        if (value) {
            value = resolve_variables(strdup(value), project);
            arg_ptr = slist_append(arg_ptr, strdup("-source"));
            arg_ptr = slist_append(arg_ptr, value);
        }
        
        /* target version (only if release not set) */
        value = hashtable_lookup(task->attribute_dict, "target");
        if (value) {
            value = resolve_variables(strdup(value), project);
            arg_ptr = slist_append(arg_ptr, strdup("-target"));
            arg_ptr = slist_append(arg_ptr, value);
        }
    }
    
    /* encoding */
    value = hashtable_lookup(task->attribute_dict, "encoding");
    if (value) {
        value = resolve_variables(strdup(value), project);
        arg_ptr = slist_append(arg_ptr, strdup("-encoding"));
        arg_ptr = slist_append(arg_ptr, value);
    }
    
    /* srcdir / sourcepath */
    value = hashtable_lookup(task->attribute_dict, SRC_DIR);
    if (!value) {
        value = hashtable_lookup(task->attribute_dict, "sourcepath");
    }
    if (value) {
        value = resolve_variables(strdup(value), project);
        arg_ptr = slist_append(arg_ptr, strdup("-sourcepath"));
        arg_ptr = slist_append(arg_ptr, value);
    } else {
        /* Check for nested sourcepath */
        path_list = hashtable_lookup(task->path_dict, "sourcepath");
        if (!path_list) {
            value = hashtable_lookup(task->attribute_dict, "sourcepathref");
            if (value) {
                path_list = hashtable_lookup(project->path_dict, value);
            }
        }
        if (path_list) {
            value = list_to_string(resolve_path(path_list, project), PATH_SEPARATOR_S);
            if (value && *value) {
                arg_ptr = slist_append(arg_ptr, strdup("-sourcepath"));
                arg_ptr = slist_append(arg_ptr, value);
            }
        }
    }
    
    /* destdir */
    dest_dir = hashtable_lookup(task->attribute_dict, "destdir");
    if (dest_dir) {
        dest_dir = resolve_variables(strdup(dest_dir), project);
        dest_dir = expand_location(project, dest_dir);
        arg_ptr = slist_append(arg_ptr, strdup("-d"));
        arg_ptr = slist_append(arg_ptr, strdup(dest_dir));
    }
    
    /* bootclasspath */
    value = hashtable_lookup(task->attribute_dict, "bootclasspath");
    if (value) {
        value = resolve_variables(strdup(value), project);
        arg_ptr = slist_append(arg_ptr, strdup("-bootclasspath"));
        arg_ptr = slist_append(arg_ptr, value);
    } else {
        path_list = hashtable_lookup(task->path_dict, "bootclasspath");
        if (!path_list) {
            value = hashtable_lookup(task->attribute_dict, "bootclasspathref");
            if (value) {
                path_list = hashtable_lookup(project->path_dict, value);
            }
        }
        if (path_list) {
            value = list_to_string(resolve_path(path_list, project), PATH_SEPARATOR_S);
            if (value && *value) {
                arg_ptr = slist_append(arg_ptr, strdup("-bootclasspath"));
                arg_ptr = slist_append(arg_ptr, value);
            }
        }
    }
    
    /* extdirs */
    value = hashtable_lookup(task->attribute_dict, "extdirs");
    if (value) {
        value = resolve_variables(strdup(value), project);
        arg_ptr = slist_append(arg_ptr, strdup("-extdirs"));
        arg_ptr = slist_append(arg_ptr, value);
    }
    
    /* classpath - build it up, potentially including destdir */
    path_list = hashtable_lookup(task->path_dict, "classpath");
    if (!path_list) {
        value = hashtable_lookup(task->attribute_dict, "classpathref");
        if (value) {
            path_list = hashtable_lookup(project->path_dict, value);
        }
    }
    if (!path_list) {
        value = hashtable_lookup(task->attribute_dict, "classpath");
        if (value) {
            classpath_str = resolve_variables(strdup(value), project);
        }
    }
    if (path_list) {
        classpath_str = list_to_string(resolve_path(path_list, project), PATH_SEPARATOR_S);
    }
    
    /* Include destdir in classpath if requested */
    if (includeDestClasses && dest_dir) {
        if (classpath_str && *classpath_str) {
            char *new_cp = str_concat(dest_dir, PATH_SEPARATOR_S, classpath_str, NULL);
            free(classpath_str);
            classpath_str = new_cp;
        } else {
            classpath_str = strdup(dest_dir);
        }
    }
    
    if (classpath_str && *classpath_str) {
        arg_ptr = slist_append(arg_ptr, strdup("-classpath"));
        arg_ptr = slist_append(arg_ptr, classpath_str);
    }
    
    /* Additional compiler arguments from nested <compilerarg> */
    compilerarg_list = hashtable_lookup(task->attribute_dict, "compilerarg");
    while (compilerarg_list) {
        value = compilerarg_list->data;
        if (value && *value) {
            arg_ptr = slist_append(arg_ptr, strdup(value));
        }
        compilerarg_list = slist_next(compilerarg_list);
    }
    
    /* files - with incremental compilation support */
    file_count = 0;
    int skipped_count = 0;
    fileset_ptr = task->fileset_list;
    while (fileset_ptr) {
        fileset = fileset_ptr->data;
        file_list = slist_new(NULL);  /* Dummy head */
        
        if (!resolve_fileset(fileset, file_list, project)) {
            slist_free(file_list);
            slist_free(arg_list);
            free(executable);
            free(dir);
            free(dest_dir);
            return !failonerror;
        }
        
        /* Get the resolved source directory for up-to-date checking */
        char *src_dir = resolve_variables(strdup(fileset->dir), project);
        src_dir = expand_location(project, src_dir);
        
        file_ptr = slist_next(file_list);
        while (file_ptr) {
            value = file_ptr->data;
            
            /* Apply selector filtering (e.g., to exclude files with missing deps) */
            if (task->selector_list) {
                /* Compute relative path for selector matching */
                const char *relative_path = value;
                size_t src_dir_len = strlen(src_dir);
                if (strncmp(value, src_dir, src_dir_len) == 0) {
                    relative_path = value + src_dir_len;
                    if (*relative_path == '/') {
                        relative_path++;
                    }
                }
                
                /* Check each selector - if file matches exclusion criteria, skip it */
                slist_t *sel_ptr = task->selector_list;
                bool excluded = false;
                while (sel_ptr) {
                    selector_t *sel = (selector_t *)sel_ptr->data;
                    /* The selector typically contains a <not><or>...</or></not> pattern
                     * If file matches the inner or and the condition is false,
                     * the file should be EXCLUDED (not compiled).
                     * The selector returns true for files that SHOULD be compiled. */
                    if (!selector_matches(relative_path, sel, project)) {
                        excluded = true;
                        break;
                    }
                    sel_ptr = slist_next(sel_ptr);
                }
                if (excluded) {
                    skipped_count++;
                    file_ptr = slist_next(file_ptr);
                    continue;
                }
            }
            
            /* Check if file needs recompilation (incremental build support) */
            if (dest_dir && !needs_compilation(value, src_dir, dest_dir)) {
                /* File is up-to-date, skip it */
                skipped_count++;
                file_ptr = slist_next(file_ptr);
                continue;
            }
            
            if (listfiles) {
                task_log(task, LOG_MESSAGE, value);
            }
            arg_ptr = slist_append(arg_ptr, value);
            file_count++;
            file_ptr = slist_next(file_ptr);
        }
        
        free(src_dir);
        slist_free(file_list);
        fileset_ptr = slist_next(fileset_ptr);
    }
    
    if (!file_count) {
        /* All files up-to-date */
        if (skipped_count > 0) {
            snprintf(message, sizeof(message), "All %d source file%s up-to-date", 
                     skipped_count, skipped_count == 1 ? " is" : "s are");
            task_log(task, LOG_MESSAGE, message);
        }
        slist_free(arg_list);
        free(executable);
        free(dir);
        free(dest_dir);
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
    
    if (!dest_dir) {
        snprintf(message, sizeof(message), "Compiling %d source file%s", 
                 file_count, file_count == 1 ? "" : "s");
    } else if (skipped_count > 0) {
        snprintf(message, sizeof(message), "Compiling %d source file%s to %s (%d up-to-date)", 
                 file_count, file_count == 1 ? "" : "s", dest_dir, skipped_count);
    } else {
        snprintf(message, sizeof(message), "Compiling %d source file%s to %s", 
                 file_count, file_count == 1 ? "" : "s", dest_dir);
    }
    task_log(task, LOG_MESSAGE, message);
    
    ret = task_spawn(task, dir, argv, NULL);
    
    /* Clean up */
    free(executable);
    free(dir);
    free(dest_dir);
    free(argv);
    
    if (!ret && failonerror) {
        return false;
    }
    
    return ret || !failonerror;
}
