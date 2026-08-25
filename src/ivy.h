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

typedef struct ivy_module_descriptor {
    ivy_module_id_t id;
    char *status;                  /* default "release" */
    slist_t *configurations;       /* slist of ivy_configuration_t* */
    slist_t *dependencies;         /* slist of ivy_dependency_t* */
    bool from_pom;                  /* true if synthesized via POM fallback */
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

typedef struct ivy_resolved_module {
    ivy_module_id_t id;
    ivy_module_descriptor_t *descriptor;
    slist_t *artifacts;    /* slist of ivy_artifact_t*, winners only */
    slist_t *confs;          /* slist of char*, confs that pulled this module in */
} ivy_resolved_module_t;

typedef struct ivy_resolution {
    hashtable_t *modules;     /* "organisation:name" -> ivy_resolved_module_t* */
    slist_t *conf_names;       /* requested confs for this run */
} ivy_resolution_t;

void ivy_resolution_free(ivy_resolution_t *resolution);

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
