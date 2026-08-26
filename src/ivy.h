/*
 * ivy.h
 * Copyright (C) 2026 Chris Burdess <dog@bluezoo.org>
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

/*
 * Private data model and internal API shared between ivy_parse.c (offline
 * parsing of ivysettings.xml / ivy.xml / Maven POMs) and ivy_resolve.c (the
 * resolution engine and the ivy:resolve / ivy:retrieve task entry points).
 * Not included by gantt.h - nothing outside these two files needs to know
 * about Ivy's internal structures.
 */

#ifndef IVY_H
#define IVY_H

#include "gantt.h"

/* ========================================================================
 * Module identity
 * ======================================================================== */

typedef struct ivy_module_id {
    char *organisation;
    char *name;
    char *revision;
} ivy_module_id_t;

/* Initializes an id, duplicating each non-NULL argument. */
void ivy_module_id_init(ivy_module_id_t *id, const char *organisation,
                         const char *name, const char *revision);

/* Frees the members of an id (not the struct itself, which is usually embedded). */
void ivy_module_id_clear(ivy_module_id_t *id);

/* Returns a newly allocated "organisation:name" key, for use in hashtables. */
char *ivy_module_id_key(const ivy_module_id_t *id);

/* ========================================================================
 * Module descriptor (parsed ivy.xml, or synthesized from a Maven POM)
 * ======================================================================== */

typedef struct ivy_exclude {
    char *organisation;   /* "*" wildcard allowed, NULL treated as "*" */
    char *module;         /* "*" wildcard allowed, NULL treated as "*" */
    char *conf;            /* NULL = applies to all configurations */
} ivy_exclude_t;

typedef struct ivy_dependency {
    ivy_module_id_t id;
    char *conf_mapping;    /* raw conf="a->b;c->d" attribute, NULL = "*->default" */
    bool transitive;        /* default true */
    slist_t *excludes;      /* slist of ivy_exclude_t* (nested <exclude>) */
} ivy_dependency_t;

typedef struct ivy_configuration {
    char *name;
    char *extends;          /* raw comma list, NULL if none (not expanded in v1) */
    char *visibility;       /* "public" / "private", default "public" */
} ivy_configuration_t;

typedef struct ivy_license {
    char *name;
    char *url;
} ivy_license_t;

/* slist_free_full callback; also reusable for a deep-copied license list. */
void ivy_license_free(void *p);

typedef struct ivy_module_descriptor {
    ivy_module_id_t id;
    char *status;                  /* default "release" */
    slist_t *configurations;       /* slist of ivy_configuration_t* */
    slist_t *dependencies;         /* slist of ivy_dependency_t* */
    bool from_pom;                  /* true if synthesized via POM fallback */
    slist_t *licenses;               /* slist of ivy_license_t*, zero or more */
    char *homepage;                   /* ivy.xml <info homepage=".."> or POM <url> */
    char *pubdate;                      /* ivy.xml <info pubdate=".."> only; NULL for from_pom */
} ivy_module_descriptor_t;

/* Parses a real ivy.xml module descriptor file. Returns NULL on error. */
ivy_module_descriptor_t *ivy_descriptor_parse_file(const char *filename);

/*
 * Parses a Maven POM file as a module descriptor fallback (used by
 * m2compatible resolvers, which never have a native ivy.xml). requested_id
 * supplies organisation/name/revision to fall back to when the POM omits
 * them (e.g. inherited from a <parent> we don't fetch in v1). Returns NULL
 * on error.
 */
ivy_module_descriptor_t *ivy_pom_parse_file(const char *filename,
                                             const ivy_module_id_t *requested_id);

void ivy_module_descriptor_free(ivy_module_descriptor_t *md);

/* ========================================================================
 * Resolvers and settings (parsed ivysettings.xml)
 * ======================================================================== */

typedef enum {
    IVY_RESOLVER_IBIBLIO,
    IVY_RESOLVER_FILESYSTEM,
    IVY_RESOLVER_URL,
    IVY_RESOLVER_CHAIN
} ivy_resolver_kind_t;

typedef struct ivy_resolver {
    ivy_resolver_kind_t kind;
    char *name;
    char *root;              /* ibiblio/url: base URL; filesystem: base dir */
    bool m2compatible;        /* ibiblio only, default true */
    char *pattern;             /* artifact pattern (filesystem/url only) */
    char *ivy_pattern;         /* ivy descriptor pattern (filesystem/url only) */
    slist_t *chain_resolvers;  /* chain only: ordered slist of ivy_resolver_t* */
    bool chain_return_first;   /* chain only: v1 default true */
} ivy_resolver_t;

typedef struct ivy_settings {
    hashtable_t *resolvers;         /* name -> ivy_resolver_t* (named resolvers only) */
    ivy_resolver_t *default_resolver;
    char *cache_dir;                  /* default ${user.home}/.ivy2/cache */
} ivy_settings_t;

/*
 * Loads settings from an ivysettings.xml file. Returns NULL on error (bad
 * XML, missing defaultResolver, dangling <resolver ref=".."/>, etc).
 */
ivy_settings_t *ivy_settings_load(const char *settings_file, project_t *project);

/*
 * Zero-config default: a single m2compatible ibiblio resolver pointed at
 * Maven Central, cache under ${user.home}/.ivy2/cache. Mirrors real Ivy's
 * own out-of-the-box behaviour when no ivysettings.xml is present.
 */
ivy_settings_t *ivy_settings_default(project_t *project);

void ivy_settings_free(ivy_settings_t *settings);

/* ========================================================================
 * Resolved artifacts (populated by the resolution engine in ivy_resolve.c)
 * ======================================================================== */

typedef struct ivy_artifact {
    ivy_module_id_t id;
    char *type;            /* "jar" | "pom" | "ivy" */
    char *ext;
    char *conf;              /* configuration this artifact is attached to */
    char *cached_path;        /* absolute path under settings->cache_dir */
} ivy_artifact_t;

/*
 * One edge in the dependency graph: which module (and its own resolved
 * revision) declared a dependency that reached a given ivy_revision_report_t.
 */
typedef struct ivy_caller {
    ivy_module_id_t id;   /* organisation/name = the calling module; revision =
                            * the caller's own resolved revision (real Ivy's
                            * "callerrev"). For edges declared directly by the
                            * root ivy.xml, this is ivy_resolution_t.root_id. */
    char *conf;             /* comma-joined subset of the requested confs this
                              * edge actually maps into (see
                              * ivy_resolve_run()'s conf_mapping_matching_confs) */
} ivy_caller_t;

/*
 * One distinct revision of a module encountered anywhere in the graph -
 * winners and evicted losers alike, plus a single error entry for a
 * dependency that could not be resolved by any configured resolver.
 */
typedef struct ivy_revision_report {
    char *revision;
    char *status;               /* NULL for an error entry */
    char *resolver_name;          /* NULL if this revision was never found */
    char *pubdate;                  /* copied from the descriptor, may be NULL */
    char *homepage;                   /* copied from the descriptor, may be NULL */
    bool evicted;
    char *evicted_by_rev;               /* non-NULL iff evicted is true */
    bool is_default;                      /* the conflict-resolution winner */
    char *error;                            /* non-NULL only if this exact
                                              * organisation:name:revision could
                                              * never be found */
    bool downloaded;                          /* true only if the winning
                                                * artifact was freshly fetched
                                                * (not a cache hit) this run */
    slist_t *callers;                           /* slist of ivy_caller_t* */
    slist_t *artifacts;                           /* slist of ivy_artifact_t*;
                                                    * populated only when
                                                    * is_default && !error */
    slist_t *licenses;                              /* slist of ivy_license_t*,
                                                       * deep-copied out of the
                                                       * descriptor (which is
                                                       * freed once resolution
                                                       * finishes) */
    slist_t *confs;                                   /* slist of char*, deduped
                                                         * union of this revision's
                                                         * callers' matching confs */
} ivy_revision_report_t;

/* All distinct revisions ever encountered for one organisation:name. */
typedef struct ivy_module_report {
    ivy_module_id_t id;   /* organisation/name stable once created; revision is
                            * the current best successfully-resolved revision,
                            * updated in place as the walk proceeds via
                            * ivy_compare_revisions(), and stays NULL if every
                            * attempt at this organisation:name errored (no
                            * revision in `revisions` is ever is_default) */
    slist_t *revisions;    /* slist of ivy_revision_report_t*, discovery order */
} ivy_module_report_t;

typedef struct ivy_resolution {
    hashtable_t *modules;     /* "organisation:name" -> ivy_module_report_t* */
    slist_t *conf_names;       /* requested confs for this run */
    char *cache_dir;             /* copied from ivy_settings_t.cache_dir before
                                   * ivy_resolve_run() frees the settings -
                                   * needed by ivy:cachefileset for a scoped
                                   * fileset directory */
    ivy_module_id_t root_id;      /* the resolved ivy.xml's own organisation/
                                    * module/revision, needed by ivy:report's
                                    * <info> element */
} ivy_resolution_t;

void ivy_resolution_free(ivy_resolution_t *resolution);

/* Returns the conflict-resolution winner among mr->revisions, or NULL if
 * every attempt at this module errored (no winner exists). Shared by
 * ivy:retrieve and the ivy:cachepath/ivy:cachefileset/ivy:report tasks. */
ivy_revision_report_t *ivy_module_report_find_default(ivy_module_report_t *mr);

/* ========================================================================
 * Resolution engine (ivy_resolve.c)
 * ======================================================================== */

/*
 * Resolves ivy_file's dependencies (transitively) against settings_file
 * (or, if NULL, the zero-config default - see ivy_settings_default()),
 * restricted to conf_filter (a comma-separated list of configuration
 * names, or NULL/"*" for all configurations declared in ivy_file).
 *
 * On success, *out_resolution is a newly allocated ivy_resolution_t (caller
 * frees with ivy_resolution_free()) and this returns true. On failure (bad
 * settings, bad ivy.xml, or the root descriptor simply couldn't be parsed),
 * returns false and *out_resolution is untouched.
 *
 * Individual unresolvable dependencies are logged as warnings via `task`
 * (which may be NULL to suppress logging) and skipped rather than failing
 * the whole run - task_t here is used purely for task_log(), not as a
 * dispatch target.
 */
bool ivy_resolve_run(project_t *project, task_t *task,
                      const char *ivy_file, const char *settings_file,
                      const char *conf_filter, ivy_resolution_t **out_resolution);

/*
 * Shared settingsfile discovery for every ivy:* task: an explicit
 * `settingsfile` attribute always wins; otherwise ${basedir}/ivysettings.xml
 * is used if it exists, else NULL (zero-config default, see
 * ivy_settings_default()). Caller frees the result.
 */
char *discover_settings_file(task_t *task, project_t *project);

/* ========================================================================
 * Pattern substitution
 * ======================================================================== */

/* Tokens available for [organisation], [orgPath], [module], [revision],
 * [artifact], [type], [ext], [conf], and the optional (-[classifier]) group. */
typedef struct ivy_pattern_tokens {
    const char *organisation;
    const char *module;
    const char *revision;
    const char *artifact;
    const char *type;
    const char *ext;
    const char *conf;
    const char *classifier;  /* may be NULL - never populated in v1 */
} ivy_pattern_tokens_t;

/*
 * Substitutes [token] references in an Ivy-style pattern. A parenthesized
 * segment, e.g. "(-[classifier])", is included only if every token it
 * references resolved to a non-empty value; otherwise the whole segment is
 * dropped. Caller must free the result.
 */
char *ivy_pattern_substitute(const char *pattern, const ivy_pattern_tokens_t *tokens);

/* ========================================================================
 * Revision comparison
 * ======================================================================== */

/*
 * Loose dotted/hyphenated revision comparator for real Maven/Ivy revisions
 * (e.g. "4.13.2", "2.0.0.RELEASE") - returns <0, 0, or >0 like strcmp.
 * Concrete literal revisions only; no support for ranges or
 * latest.integration in v1.
 */
int ivy_compare_revisions(const char *a, const char *b);

#endif /* IVY_H */
