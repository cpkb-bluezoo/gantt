/*
 * gantt.h
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

#ifndef GANTT_H
#define GANTT_H

#include "util.h"
#include "xml.h"

/* The version is set once, in AC_INIT in configure.ac (passed as -D flags) */
#define GANTT_VERSION PACKAGE_VERSION
#define GANTT_PREFIX "gantt_"
#define GANTT_TASK_NAME "GANTT_TASK_NAME"
#define GANTT_FILESET_SIZE "GANTT_FILESET_SIZE"

/* XML element names */
#define PROJECT "project"
#define TARGET "target"
#define PROPERTY "property"
#define DESCRIPTION "description"
#define INCLUDE "include"
#define INCLUDES_FILE "includesfile"
#define EXCLUDE "exclude"
#define EXCLUDES_FILE "excludesfile"
#define PATHELEMENT "pathelement"
#define FILESET "fileset"
#define DIRSET "dirset"
#define CLASSPATH "classpath"
#define PATH "path"

/* XML attribute names */
#define ID "id"
#define REFID "refid"
#define NAME "name"
#define DEFAULT "default"
#define BASE_DIR "basedir"
#define SOURCEPATH "sourcepath"
#define DEPENDS "depends"
#define IF "if"
#define UNLESS "unless"
#define VALUE "value"
#define LOCATION "location"
#define ROOT_DIR "dir"
#define DEFAULT_EXCLUDES "defaultexcludes"
#define CASE_SENSITIVE "casesensitive"
#define FOLLOW_SYMLINKS "followsymlinks"
#define INCLUDES "includes"
#define EXCLUDES "excludes"
#define ARG "arg"
#define JVMARG "jvmarg"
#define MANIFEST "manifest"
#define MANIFEST_ATTR "_manifest_attrs"  /* Internal key for parsed manifest */

/* Task names */
#define JAVAC "javac"
#define JAVADOC "javadoc"
#define JAR "jar"
#define JAVA "java"
#define TSTAMP "tstamp"
#define EXEC "exec"
#define AVAILABLE "available"
#define GET "get"
#define LOADFILE "loadfile"
#define TEMPFILE "tempfile"
#define LENGTH "length"
#define UPTODATE "uptodate"
#define CONDITION "condition"
#define ANTCALL "antcall"
#define INPUT "input"
#define PATHCONVERT "pathconvert"
#define SEQUENTIAL "sequential"
#define PARALLEL "parallel"
#define XMLPROPERTY "xmlproperty"
#define APPLY "apply"
#define BASENAME "basename"
#define DIRNAME "dirname"
#define FAIL "fail"
#define ECHO "echo"
#define SRC_DIR "srcdir"

/* Selector types */
#define SELECTOR_UNKNOWN 0
#define SELECTOR_INCLUDE 1
#define SELECTOR_INCLUDES_FILE 2
#define SELECTOR_EXCLUDE 3
#define SELECTOR_EXCLUDES_FILE 4
#define SELECTOR_CONTAINS 5
#define SELECTOR_DATE 6
#define SELECTOR_DEPEND 7
#define SELECTOR_DEPTH 8
#define SELECTOR_FILENAME 9
#define SELECTOR_PRESENT 10
#define SELECTOR_SIZE 11
#define SELECTOR_OR 12
#define SELECTOR_AND 13
#define SELECTOR_NOT 14
#define SELECTOR_REFID 15

/* Path element types */
#define PATH_ELEMENT 0
#define PATH_FILESET 1
#define PATH_REFERENCE 2
#define PATH_INLINE 3

/* Log levels */
#define LOG_ERROR   1
#define LOG_WARNING 2
#define LOG_INFO    3
#define LOG_DEBUG   4
#define LOG_MESSAGE 5

/* Environment variables */
#define ENV_PATH "PATH"

/* Forward declarations */
typedef struct project project_t;
typedef struct target target_t;
typedef struct task task_t;
typedef struct path path_t;
typedef struct fileset fileset_t;
typedef struct selector selector_t;

/* ========================================================================
 * Projects
 * ======================================================================== */

struct project
{
    char *filename;
    char *default_target;
    char *name;
    char *description;
    char *base_dir;
    slist_t *target_list;       /* List of targets in this project */
    hashtable_t *target_dict;   /* Map of name to target */
    hashtable_t *property_dict; /* Map of name to value */
    hashtable_t *path_dict;     /* Paths by id */
    hashtable_t *fileset_dict;  /* Filesets by id */
    hashtable_t *selector_dict; /* Named selectors by id */
};

project_t *project_alloc(void);
void project_free(project_t *project);
bool project_init(project_t *project, xml_node_t *node);

/* Direct parsing - builds project without intermediate DOM */
project_t *parse_project(const char *filename, slist_t *cmd_props);

/* ========================================================================
 * Targets
 * ======================================================================== */

struct target
{
    project_t *project;         /* Parent project */
    char *name;
    char *description;
    slist_t *task_list;         /* List of tasks in this target */
    slist_t *depends_list;      /* List of dependencies for this target */
    char *if_condition;
    char *unless_condition;
};

target_t *target_alloc(project_t *project);
void target_free(target_t *target);
bool target_init(target_t *target, xml_node_t *node);
bool target_invoke(target_t *target, project_t *project,
                   hashtable_t *context, hashtable_t *completed);

/* ========================================================================
 * Tasks
 * ======================================================================== */

struct task
{
    target_t *target;           /* Parent target */
    char *name;                 /* Element name */
    slist_t *attribute_list;    /* List of names of attributes */
    hashtable_t *attribute_dict;/* Map of attribute names to values */
    slist_t *fileset_list;      /* Filesets for this task */
    hashtable_t *path_dict;     /* Paths for this task */
    slist_t *nested_tasks;      /* Nested tasks (for parallel/sequential) */
    slist_t *selector_list;     /* Nested selectors for filtering */
    xml_node_t *xml_node;       /* Original XML node (for complex tasks) */
};

task_t *task_alloc(target_t *target);
void task_free(task_t *task);
bool task_init(task_t *task, xml_node_t *node);
bool task_invoke(task_t *task, project_t *project);
void task_log(task_t *task, int level, const char *message);
bool task_spawn(task_t *task, const char *dir, char **argv, char **envp);

/* Direct task creation for SAX parsing */
task_t *task_create(target_t *target, const char *name, const char **attrs);
void task_set_text(task_t *task, const char *text);
void task_add_fileset(task_t *task, fileset_t *fileset);
void task_add_path(task_t *task, const char *name, slist_t *path_list);
void task_add_selector(task_t *task, selector_t *selector);
void task_add_nested(task_t *task, task_t *nested);
bool task_init_children(task_t *task, xml_node_t *node);

typedef bool (*task_invoke_fn)(task_t *task, project_t *project);

/* Built-in task implementations */
bool javac_invoke(task_t *task, project_t *project);
bool javadoc_invoke(task_t *task, project_t *project);
bool jar_invoke(task_t *task, project_t *project);
bool java_invoke(task_t *task, project_t *project);
bool tstamp_invoke(task_t *task, project_t *project);
bool property_invoke(task_t *task, project_t *project);
bool exec_invoke(task_t *task, project_t *project);
bool available_invoke(task_t *task, project_t *project);
bool get_invoke(task_t *task, project_t *project);
bool loadfile_invoke(task_t *task, project_t *project);
bool tempfile_invoke(task_t *task, project_t *project);
bool length_invoke(task_t *task, project_t *project);
bool uptodate_invoke(task_t *task, project_t *project);
bool condition_invoke(task_t *task, project_t *project);
bool antcall_invoke(task_t *task, project_t *project);
bool input_invoke(task_t *task, project_t *project);
bool pathconvert_invoke(task_t *task, project_t *project);
bool sequential_invoke(task_t *task, project_t *project);
bool parallel_invoke(task_t *task, project_t *project);
bool xmlproperty_invoke(task_t *task, project_t *project);
bool apply_invoke(task_t *task, project_t *project);
bool basename_invoke(task_t *task, project_t *project);
bool dirname_invoke(task_t *task, project_t *project);
bool fail_invoke(task_t *task, project_t *project);
bool echo_invoke(task_t *task, project_t *project);
bool local_invoke(task_t *task, project_t *project);
bool native2ascii_invoke(task_t *task, project_t *project);
bool ivy_resolve_invoke(task_t *task, project_t *project);
bool ivy_retrieve_invoke(task_t *task, project_t *project);
bool ivy_cachepath_invoke(task_t *task, project_t *project);
bool ivy_cachefileset_invoke(task_t *task, project_t *project);
bool ivy_report_invoke(task_t *task, project_t *project);
bool ivy_publish_invoke(task_t *task, project_t *project);

char *expand_location(project_t *project, char *location);

/* ========================================================================
 * Paths (path-like structures)
 * ======================================================================== */

struct path
{
    unsigned int type;          /* PATH_ELEMENT | PATH_FILESET */
    void *data;                 /* Pointer to the data (char* or fileset_t*) */
};

path_t *path_alloc(void);
void path_free(path_t *path);
bool path_init(path_t *path, xml_node_t *node);
slist_t *path_list_init(xml_node_t *node);

/* Direct path creation for SAX parsing */
path_t *path_create(const char *name, const char **attrs);
void path_add_child(path_t *parent, path_t *child);

slist_t *resolve_path(slist_t *path_list, project_t *project);

/* ========================================================================
 * Dirset/FileSet/PatternSet/FilterSet
 * ======================================================================== */

struct fileset
{
    char *name;                 /* Name of the element that defined this fileset */
    char *dir;                  /* Root directory */
    char *refid;                /* Reference to another fileset (if set, dir is ignored) */
    bool default_excludes;
    bool case_sensitive;
    bool follow_symlinks;
    slist_t *selector_list;     /* List of selectors for determining files */
};

fileset_t *fileset_alloc(void);
void fileset_free(fileset_t *fileset);
bool fileset_init(fileset_t *fileset, xml_node_t *node);

/* Direct fileset creation for SAX parsing */
fileset_t *fileset_create(const char *name, const char **attrs);
void fileset_add_selector(fileset_t *fileset, selector_t *selector);

bool resolve_fileset(fileset_t *fileset, slist_t *files, project_t *project);

/* ========================================================================
 * Glob and other file selection criteria
 * ======================================================================== */

struct selector
{
    unsigned int type;          /* include, exclude, contains, date, depend, depth, */
                                /* filename, present, size, or, and, not, refid */
    hashtable_t *attribute_dict;
    slist_t *children;          /* Nested selectors (for or/and/not) */
    char *refid;                /* Reference to a named selector */
    char *unless_prop;          /* Only match if this property is NOT set */
    char *if_prop;              /* Only match if this property IS set */
};

selector_t *selector_alloc(void);
void selector_free(selector_t *selector);
bool selector_init(selector_t *selector, xml_node_t *node);

/* Direct selector creation for SAX parsing */
selector_t *selector_create(const char *name, const char **attrs);
void selector_add_child(selector_t *parent, selector_t *child);

bool add_files(const char *path, slist_t *files, fileset_t *fileset, project_t *project);
bool selector_match_dir(const char *path, selector_t *selector, fileset_t *fileset, project_t *project);
bool selector_match_file(const char *path, selector_t *selector, fileset_t *fileset, project_t *project);
bool selector_matches(const char *path, selector_t *selector, project_t *project);
selector_t *selector_parse(xml_node_t *node, project_t *project);

bool match_path(const char *pattern, const char *string, bool case_sensitive);
bool match(const char *pattern, const char *string, bool case_sensitive);
dlist_t *path_as_list(const char *path);

/* ========================================================================
 * Miscellaneous functions
 * ======================================================================== */

char *list_to_string(slist_t *list, const char *delimiter);

char *find_gantt_executable(const char *name, bool *is_gantt_executable);
char *find_executable(const char *name);

char *resolve_variables(char *value, project_t *project);

void print_project_help(project_t *project);
void print_version(void);

#endif /* GANTT_H */
