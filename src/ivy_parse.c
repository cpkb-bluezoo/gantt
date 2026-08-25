/*
 * ivy_parse.c
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
 * Pure, offline parsing: ivysettings.xml -> ivy_settings_t, ivy.xml ->
 * ivy_module_descriptor_t, Maven POM -> ivy_module_descriptor_t (the
 * m2compatible fallback), the revision comparator, and Ivy-style
 * [token] pattern substitution. Nothing in this file touches the network
 * or the local cache - see ivy_resolve.c for that.
 */

#include "ivy.h"

/* ========================================================================
 * Module identity
 * ======================================================================== */

void ivy_module_id_init(ivy_module_id_t *id, const char *organisation,
                         const char *name, const char *revision)
{
    id->organisation = organisation ? strdup(organisation) : NULL;
    id->name = name ? strdup(name) : NULL;
    id->revision = revision ? strdup(revision) : NULL;
}

void ivy_module_id_clear(ivy_module_id_t *id)
{
    if (!id) {
        return;
    }
    free(id->organisation);
    free(id->name);
    free(id->revision);
    id->organisation = NULL;
    id->name = NULL;
    id->revision = NULL;
}

char *ivy_module_id_key(const ivy_module_id_t *id)
{
    return str_concat(id->organisation ? id->organisation : "",
                       ":",
                       id->name ? id->name : "",
                       NULL);
}

/* ========================================================================
 * Revision comparison
 * ======================================================================== */

typedef struct rev_token {
    bool numeric;
    long num;
    char *str;
} rev_token_t;

static void rev_token_free(void *p)
{
    rev_token_t *tok = p;
    if (!tok) {
        return;
    }
    free(tok->str);
    free(tok);
}

/* Splits a revision string into alternating digit-run / non-digit-run
 * tokens at each '.', '-', '_' boundary (which are themselves discarded)
 * and at every digit/non-digit boundary (so "beta2" splits to "beta","2"). */
static slist_t *tokenize_revision(const char *rev)
{
    slist_t *tokens = NULL;
    slist_t *tail = NULL;
    const char *p;

    if (!rev) {
        return NULL;
    }

    p = rev;
    while (*p) {
        const char *start;
        bool numeric;
        rev_token_t *tok;

        while (*p == '.' || *p == '-' || *p == '_') {
            p++;
        }
        if (!*p) {
            break;
        }

        start = p;
        numeric = isdigit((unsigned char)*p) ? true : false;
        if (numeric) {
            while (isdigit((unsigned char)*p)) {
                p++;
            }
        } else {
            while (*p && *p != '.' && *p != '-' && *p != '_' &&
                   !isdigit((unsigned char)*p)) {
                p++;
            }
        }

        tok = malloc(sizeof(rev_token_t));
        tok->numeric = numeric;
        tok->str = strndup(start, (size_t)(p - start));
        tok->num = numeric ? strtol(tok->str, NULL, 10) : 0;

        if (!tail) {
            tokens = slist_new(tok);
            tail = tokens;
        } else {
            tail = slist_append(tail, tok);
        }
    }

    return tokens;
}

int ivy_compare_revisions(const char *a, const char *b)
{
    slist_t *ta, *tb, *pa, *pb;
    int result = 0;

    if (!a) {
        a = "";
    }
    if (!b) {
        b = "";
    }
    if (strcmp(a, b) == 0) {
        return 0;
    }

    ta = tokenize_revision(a);
    tb = tokenize_revision(b);
    pa = ta;
    pb = tb;

    while (pa || pb) {
        rev_token_t *ka, *kb;

        if (!pa) {
            result = -1;
            break;
        }
        if (!pb) {
            result = 1;
            break;
        }

        ka = pa->data;
        kb = pb->data;

        if (ka->numeric && kb->numeric) {
            if (ka->num != kb->num) {
                result = (ka->num < kb->num) ? -1 : 1;
                break;
            }
        } else if (ka->numeric != kb->numeric) {
            /* A numeric token outranks a non-numeric one at the same
             * position (matches the common "1.0 > 1.0-beta" intuition). */
            result = ka->numeric ? 1 : -1;
            break;
        } else {
            int c = strcmp(ka->str, kb->str);
            if (c != 0) {
                result = (c < 0) ? -1 : 1;
                break;
            }
        }

        pa = slist_next(pa);
        pb = slist_next(pb);
    }

    slist_free_full(ta, rev_token_free);
    slist_free_full(tb, rev_token_free);

    return result;
}

/* ========================================================================
 * Pattern substitution
 * ======================================================================== */

static char *token_value(const char *name, const ivy_pattern_tokens_t *tokens)
{
    if (strcmp(name, "organisation") == 0 || strcmp(name, "organization") == 0) {
        return tokens->organisation ? strdup(tokens->organisation) : NULL;
    }
    if (strcmp(name, "orgPath") == 0) {
        char *v;
        char *p;
        if (!tokens->organisation) {
            return NULL;
        }
        v = strdup(tokens->organisation);
        for (p = v; *p; p++) {
            if (*p == '.') {
                *p = '/';
            }
        }
        return v;
    }
    if (strcmp(name, "module") == 0) {
        return tokens->module ? strdup(tokens->module) : NULL;
    }
    if (strcmp(name, "revision") == 0) {
        return tokens->revision ? strdup(tokens->revision) : NULL;
    }
    if (strcmp(name, "artifact") == 0) {
        return tokens->artifact ? strdup(tokens->artifact) : NULL;
    }
    if (strcmp(name, "type") == 0) {
        return tokens->type ? strdup(tokens->type) : NULL;
    }
    if (strcmp(name, "ext") == 0) {
        return tokens->ext ? strdup(tokens->ext) : NULL;
    }
    if (strcmp(name, "conf") == 0) {
        return tokens->conf ? strdup(tokens->conf) : NULL;
    }
    if (strcmp(name, "classifier") == 0) {
        return tokens->classifier ? strdup(tokens->classifier) : NULL;
    }
    return NULL;
}

/* Substitutes [token] references only (no parenthesized groups). Used both
 * for the top-level pattern and for the inside of one optional group.
 * Sets *any_missing if any referenced token had no (or an empty) value. */
static char *substitute_flat(const char *text, const ivy_pattern_tokens_t *tokens,
                              bool *any_missing)
{
    string_t *out = string_new("");
    size_t i = 0;
    size_t len = strlen(text);

    while (i < len) {
        if (text[i] == '[') {
            size_t j = i + 1;
            char *name;
            char *value;

            while (j < len && text[j] != ']') {
                j++;
            }
            if (j >= len) {
                string_append_len(out, text + i, len - i);
                break;
            }

            name = strndup(text + i + 1, j - i - 1);
            value = token_value(name, tokens);
            free(name);

            if (!value || !*value) {
                *any_missing = true;
            }
            if (value) {
                string_append(out, value);
                free(value);
            }
            i = j + 1;
        } else {
            string_append_c(out, text[i]);
            i++;
        }
    }

    return string_free(out, false);
}

char *ivy_pattern_substitute(const char *pattern, const ivy_pattern_tokens_t *tokens)
{
    string_t *out;
    size_t i, len;

    if (!pattern) {
        return NULL;
    }

    out = string_new("");
    i = 0;
    len = strlen(pattern);

    while (i < len) {
        if (pattern[i] == '(') {
            size_t j = i + 1;
            char *inner;
            char *substituted;
            bool missing = false;

            while (j < len && pattern[j] != ')') {
                j++;
            }
            if (j >= len) {
                string_append_len(out, pattern + i, len - i);
                break;
            }

            inner = strndup(pattern + i + 1, j - i - 1);
            substituted = substitute_flat(inner, tokens, &missing);
            if (!missing) {
                string_append(out, substituted);
            }
            free(substituted);
            free(inner);
            i = j + 1;
        } else if (pattern[i] == '[') {
            size_t j = i + 1;
            char *name;
            char *value;

            while (j < len && pattern[j] != ']') {
                j++;
            }
            if (j >= len) {
                string_append_len(out, pattern + i, len - i);
                break;
            }

            name = strndup(pattern + i + 1, j - i - 1);
            value = token_value(name, tokens);
            free(name);
            if (value) {
                string_append(out, value);
                free(value);
            }
            i = j + 1;
        } else {
            string_append_c(out, pattern[i]);
            i++;
        }
    }

    return string_free(out, false);
}

/* ========================================================================
 * Module descriptor: freeing
 * ======================================================================== */

static void exclude_free(void *p)
{
    ivy_exclude_t *ex = p;
    if (!ex) {
        return;
    }
    free(ex->organisation);
    free(ex->module);
    free(ex->conf);
    free(ex);
}

static void dependency_free(void *p)
{
    ivy_dependency_t *dep = p;
    if (!dep) {
        return;
    }
    ivy_module_id_clear(&dep->id);
    free(dep->conf_mapping);
    slist_free_full(dep->excludes, exclude_free);
    free(dep);
}

static void configuration_free(void *p)
{
    ivy_configuration_t *conf = p;
    if (!conf) {
        return;
    }
    free(conf->name);
    free(conf->extends);
    free(conf->visibility);
    free(conf);
}

void ivy_module_descriptor_free(ivy_module_descriptor_t *md)
{
    if (!md) {
        return;
    }
    ivy_module_id_clear(&md->id);
    free(md->status);
    slist_free_full(md->configurations, configuration_free);
    slist_free_full(md->dependencies, dependency_free);
    free(md);
}

/* Real Ivy implicitly defines a single "default" public configuration when
 * <configurations> is omitted entirely (native descriptors) or doesn't
 * exist at all (POM-derived descriptors). */
static void synthesize_default_configuration(ivy_module_descriptor_t *md)
{
    ivy_configuration_t *conf;
    if (md->configurations) {
        return;
    }
    conf = calloc(1, sizeof(ivy_configuration_t));
    conf->name = strdup("default");
    conf->visibility = strdup("public");
    md->configurations = slist_new(conf);
}

/* ========================================================================
 * ivy.xml module descriptor parsing
 * ======================================================================== */

ivy_module_descriptor_t *ivy_descriptor_parse_file(const char *filename)
{
    xml_doc_t *doc;
    xml_node_t *root;
    ivy_module_descriptor_t *md;
    slist_t *config_tail = NULL;
    slist_t *dep_tail = NULL;
    xml_node_t *cur;

    doc = xml_parse_file(filename);
    if (!doc) {
        return NULL;
    }

    root = xml_doc_get_root(doc);
    if (!root || !xml_streq(root->name, "ivy-module")) {
        fprintf(stderr, "ivy: %s: not an ivy-module document\n", filename);
        xml_doc_free(doc);
        return NULL;
    }

    md = calloc(1, sizeof(ivy_module_descriptor_t));
    md->status = strdup("release");
    md->from_pom = false;

    for (cur = root->children; cur; cur = cur->next) {
        if (xml_streq(cur->name, "info")) {
            const char *org = xml_node_get_attr(cur, "organisation");
            const char *name = xml_node_get_attr(cur, "module");
            const char *rev = xml_node_get_attr(cur, "revision");
            const char *status = xml_node_get_attr(cur, "status");
            ivy_module_id_init(&md->id, org, name, rev);
            if (status) {
                free(md->status);
                md->status = strdup(status);
            }
        } else if (xml_streq(cur->name, "configurations")) {
            xml_node_t *cnode;
            for (cnode = cur->children; cnode; cnode = cnode->next) {
                ivy_configuration_t *conf;
                const char *vis;
                if (!xml_streq(cnode->name, "conf")) {
                    continue;
                }
                conf = calloc(1, sizeof(ivy_configuration_t));
                conf->name = xml_node_get_attr_dup(cnode, "name");
                conf->extends = xml_node_get_attr_dup(cnode, "extends");
                vis = xml_node_get_attr(cnode, "visibility");
                conf->visibility = strdup(vis ? vis : "public");
                if (!config_tail) {
                    md->configurations = slist_new(conf);
                    config_tail = md->configurations;
                } else {
                    config_tail = slist_append(config_tail, conf);
                }
            }
        } else if (xml_streq(cur->name, "dependencies")) {
            xml_node_t *dnode;
            for (dnode = cur->children; dnode; dnode = dnode->next) {
                if (xml_streq(dnode->name, "dependency")) {
                    ivy_dependency_t *dep;
                    const char *org = xml_node_get_attr(dnode, "org");
                    const char *name = xml_node_get_attr(dnode, "name");
                    const char *rev = xml_node_get_attr(dnode, "rev");
                    const char *trans;
                    slist_t *ex_tail = NULL;
                    xml_node_t *enode;

                    dep = calloc(1, sizeof(ivy_dependency_t));
                    ivy_module_id_init(&dep->id, org, name, rev);
                    dep->conf_mapping = xml_node_get_attr_dup(dnode, "conf");
                    trans = xml_node_get_attr(dnode, "transitive");
                    dep->transitive = trans ? parse_boolean(trans, true) : true;

                    for (enode = dnode->children; enode; enode = enode->next) {
                        ivy_exclude_t *ex;
                        const char *eorg, *emod, *econf;
                        if (!xml_streq(enode->name, "exclude")) {
                            continue;
                        }
                        eorg = xml_node_get_attr(enode, "org");
                        emod = xml_node_get_attr(enode, "module");
                        econf = xml_node_get_attr(enode, "conf");
                        ex = calloc(1, sizeof(ivy_exclude_t));
                        ex->organisation = strdup(eorg ? eorg : "*");
                        ex->module = strdup(emod ? emod : "*");
                        ex->conf = econf ? strdup(econf) : NULL;
                        if (!ex_tail) {
                            dep->excludes = slist_new(ex);
                            ex_tail = dep->excludes;
                        } else {
                            ex_tail = slist_append(ex_tail, ex);
                        }
                    }

                    if (!dep_tail) {
                        md->dependencies = slist_new(dep);
                        dep_tail = md->dependencies;
                    } else {
                        dep_tail = slist_append(dep_tail, dep);
                    }
                } else if (xml_streq(dnode->name, "exclude")) {
                    /* Module-wide (not per-dependency) excludes are out of
                     * scope for v1 - only nested <dependency><exclude> is
                     * supported. */
                    fprintf(stderr,
                            "ivy: %s: top-level <exclude> is not supported, ignoring\n",
                            filename);
                }
            }
        }
    }

    xml_doc_free(doc);
    synthesize_default_configuration(md);

    return md;
}

/* ========================================================================
 * Maven POM parsing (m2compatible resolver fallback)
 * ======================================================================== */

/* Resolves a POM value that is exactly one "${...}" property reference -
 * the common case for <version>${revision}</version>-style POMs. Mixed
 * text/reference values and multi-level parent property inheritance are
 * out of scope for v1; unresolved references fall back to their literal
 * text. Caller must free the result. */
static char *pom_resolve_ref(const char *raw, hashtable_t *props,
                              const ivy_module_id_t *self)
{
    size_t len;
    char *key;
    char *value = NULL;

    if (!raw) {
        return NULL;
    }
    len = strlen(raw);
    if (!(len > 3 && raw[0] == '$' && raw[1] == '{' && raw[len - 1] == '}')) {
        return strdup(raw);
    }

    key = strndup(raw + 2, len - 3);
    if (strcmp(key, "project.version") == 0 || strcmp(key, "pom.version") == 0 ||
        strcmp(key, "version") == 0) {
        value = self->revision ? strdup(self->revision) : NULL;
    } else if (strcmp(key, "project.groupId") == 0 || strcmp(key, "pom.groupId") == 0 ||
               strcmp(key, "groupId") == 0) {
        value = self->organisation ? strdup(self->organisation) : NULL;
    } else if (strcmp(key, "project.artifactId") == 0 || strcmp(key, "pom.artifactId") == 0 ||
               strcmp(key, "artifactId") == 0) {
        value = self->name ? strdup(self->name) : NULL;
    } else if (props) {
        char *v = hashtable_lookup(props, key);
        value = v ? strdup(v) : NULL;
    }
    free(key);

    return value ? value : strdup(raw);
}

ivy_module_descriptor_t *ivy_pom_parse_file(const char *filename,
                                             const ivy_module_id_t *requested_id)
{
    xml_doc_t *doc;
    xml_node_t *root;
    ivy_module_descriptor_t *md;
    char *group_id = NULL;
    char *artifact_id = NULL;
    char *version = NULL;
    hashtable_t *props;
    slist_t *dep_tail = NULL;
    xml_node_t *cur;

    doc = xml_parse_file(filename);
    if (!doc) {
        return NULL;
    }

    root = xml_doc_get_root(doc);
    if (!root || !xml_streq(root->name, "project")) {
        fprintf(stderr, "ivy: %s: not a Maven POM document\n", filename);
        xml_doc_free(doc);
        return NULL;
    }

    md = calloc(1, sizeof(ivy_module_descriptor_t));
    md->status = strdup("release");
    md->from_pom = true;
    props = hashtable_new();

    for (cur = root->children; cur; cur = cur->next) {
        if (xml_streq(cur->name, "groupId")) {
            group_id = xml_node_get_text(cur);
        } else if (xml_streq(cur->name, "artifactId")) {
            artifact_id = xml_node_get_text(cur);
        } else if (xml_streq(cur->name, "version")) {
            version = xml_node_get_text(cur);
        } else if (xml_streq(cur->name, "parent")) {
            xml_node_t *p;
            for (p = cur->children; p; p = p->next) {
                if (!group_id && xml_streq(p->name, "groupId")) {
                    group_id = xml_node_get_text(p);
                } else if (!version && xml_streq(p->name, "version")) {
                    version = xml_node_get_text(p);
                }
            }
        } else if (xml_streq(cur->name, "properties")) {
            xml_node_t *p;
            for (p = cur->children; p; p = p->next) {
                char *val = xml_node_get_text(p);
                if (val) {
                    hashtable_insert(props, p->name, val);
                }
            }
        }
    }

    ivy_module_id_init(&md->id,
                        group_id ? group_id : requested_id->organisation,
                        artifact_id ? artifact_id : requested_id->name,
                        version ? version : requested_id->revision);
    free(group_id);
    free(artifact_id);
    free(version);

    for (cur = root->children; cur; cur = cur->next) {
        xml_node_t *dnode;
        if (!xml_streq(cur->name, "dependencies")) {
            continue;
        }
        for (dnode = cur->children; dnode; dnode = dnode->next) {
            char *d_group = NULL, *d_artifact = NULL, *d_version = NULL;
            char *d_scope = NULL, *d_optional = NULL;
            bool skip = false;
            xml_node_t *f;

            if (!xml_streq(dnode->name, "dependency")) {
                continue;
            }

            for (f = dnode->children; f; f = f->next) {
                if (xml_streq(f->name, "groupId")) {
                    d_group = xml_node_get_text(f);
                } else if (xml_streq(f->name, "artifactId")) {
                    d_artifact = xml_node_get_text(f);
                } else if (xml_streq(f->name, "version")) {
                    d_version = xml_node_get_text(f);
                } else if (xml_streq(f->name, "scope")) {
                    d_scope = xml_node_get_text(f);
                } else if (xml_streq(f->name, "optional")) {
                    d_optional = xml_node_get_text(f);
                }
            }

            if (d_optional && parse_boolean(d_optional, false)) {
                /* Maven excludes optional dependencies from transitive
                 * resolution by default - only relevant to direct consumers,
                 * who would declare them explicitly themselves. */
                skip = true;
            }
            if (d_scope && (strcmp(d_scope, "test") == 0 ||
                             strcmp(d_scope, "provided") == 0 ||
                             strcmp(d_scope, "system") == 0)) {
                skip = true;
            }
            if (!d_group || !d_artifact) {
                skip = true;
            }

            if (!skip && !d_version) {
                /* No <dependencyManagement> support in v1 - a dependency
                 * relying on inherited version management can't be resolved. */
                fprintf(stderr,
                        "ivy: %s: dependency %s:%s has no version (dependencyManagement "
                        "is not supported), skipping\n",
                        filename, d_group, d_artifact);
                skip = true;
            }

            if (!skip) {
                char *resolved_group = pom_resolve_ref(d_group, props, &md->id);
                char *resolved_version = pom_resolve_ref(d_version, props, &md->id);
                ivy_dependency_t *dep = calloc(1, sizeof(ivy_dependency_t));

                ivy_module_id_init(&dep->id, resolved_group, d_artifact, resolved_version);
                dep->transitive = true;
                dep->conf_mapping = NULL; /* "*->default" */

                free(resolved_group);
                free(resolved_version);

                if (!dep_tail) {
                    md->dependencies = slist_new(dep);
                    dep_tail = md->dependencies;
                } else {
                    dep_tail = slist_append(dep_tail, dep);
                }
            }

            free(d_group);
            free(d_artifact);
            free(d_version);
            free(d_scope);
            free(d_optional);
        }
    }

    hashtable_free_full(props, free);
    xml_doc_free(doc);
    synthesize_default_configuration(md);

    return md;
}

/* ========================================================================
 * ivysettings.xml parsing
 * ======================================================================== */

static char *expand_props(const char *raw, project_t *project)
{
    if (!raw) {
        return NULL;
    }
    return resolve_variables(strdup(raw), project);
}

static char *default_cache_dir(project_t *project)
{
    char *home = hashtable_lookup(project->property_dict, "user.home");
    if (home) {
        return str_concat(home, "/.ivy2/cache", NULL);
    }
    return strdup(".ivy2/cache");
}

static ivy_resolver_t *resolver_alloc(ivy_resolver_kind_t kind)
{
    ivy_resolver_t *r = calloc(1, sizeof(ivy_resolver_t));
    r->kind = kind;
    r->m2compatible = true;
    r->chain_return_first = true;
    return r;
}

static void resolver_free(void *p)
{
    ivy_resolver_t *r = p;
    if (!r) {
        return;
    }
    free(r->name);
    free(r->root);
    free(r->pattern);
    free(r->ivy_pattern);
    /* chain_resolvers holds non-owning pointers - every resolver, named or
     * anonymous, is registered (and thus owned) via settings->resolvers, so
     * only the list structure is freed here, never the pointed-to resolvers. */
    slist_free(r->chain_resolvers);
    free(r);
}

static void register_resolver(ivy_settings_t *settings, ivy_resolver_t *r,
                               const char *name, int *anon_counter)
{
    char *key;
    if (name) {
        key = strdup(name);
    } else {
        char buf[32];
        snprintf(buf, sizeof(buf), "__anon%d", (*anon_counter)++);
        key = strdup(buf);
    }
    hashtable_insert(settings->resolvers, key, r);
    free(key);
}

static ivy_resolver_t *parse_resolver_node(xml_node_t *node, ivy_settings_t *settings,
                                            project_t *project, int *anon_counter);

static ivy_resolver_t *parse_chain(xml_node_t *node, ivy_settings_t *settings,
                                    project_t *project, int *anon_counter)
{
    ivy_resolver_t *r = resolver_alloc(IVY_RESOLVER_CHAIN);
    const char *name = xml_node_get_attr(node, "name");
    const char *rf = xml_node_get_attr(node, "returnFirst");
    slist_t *tail = NULL;
    xml_node_t *cur;

    r->name = name ? strdup(name) : NULL;
    r->chain_return_first = rf ? parse_boolean(rf, true) : true;

    for (cur = node->children; cur; cur = cur->next) {
        ivy_resolver_t *member = NULL;

        if (xml_streq(cur->name, "resolver")) {
            const char *ref = xml_node_get_attr(cur, "ref");
            if (ref) {
                member = hashtable_lookup(settings->resolvers, ref);
                if (!member) {
                    fprintf(stderr, "ivy: chain references unknown resolver '%s'\n", ref);
                }
            }
        } else {
            member = parse_resolver_node(cur, settings, project, anon_counter);
        }

        if (member) {
            if (!tail) {
                r->chain_resolvers = slist_new(member);
                tail = r->chain_resolvers;
            } else {
                tail = slist_append(tail, member);
            }
        }
    }

    register_resolver(settings, r, name, anon_counter);
    return r;
}

static ivy_resolver_t *parse_resolver_node(xml_node_t *node, ivy_settings_t *settings,
                                            project_t *project, int *anon_counter)
{
    ivy_resolver_kind_t kind;
    ivy_resolver_t *r;
    const char *name;
    const char *root;
    const char *m2;
    bool default_m2;

    if (xml_streq(node->name, "chain")) {
        return parse_chain(node, settings, project, anon_counter);
    }
    if (xml_streq(node->name, "ibiblio")) {
        kind = IVY_RESOLVER_IBIBLIO;
    } else if (xml_streq(node->name, "filesystem")) {
        kind = IVY_RESOLVER_FILESYSTEM;
    } else if (xml_streq(node->name, "url")) {
        kind = IVY_RESOLVER_URL;
    } else {
        fprintf(stderr, "ivy: unrecognised resolver type <%s>, skipping\n", node->name);
        return NULL;
    }

    r = resolver_alloc(kind);
    name = xml_node_get_attr(node, "name");
    r->name = name ? strdup(name) : NULL;

    root = xml_node_get_attr(node, "root");
    if (root) {
        r->root = expand_props(root, project);
    } else if (kind == IVY_RESOLVER_IBIBLIO) {
        r->root = strdup("https://repo1.maven.org/maven2/");
    }

    /* m2compatible applies to any of the three leaf kinds, not just ibiblio
     * (a filesystem/url resolver can point at an m2-layout local mirror
     * just as well) - default true for ibiblio, false otherwise. */
    default_m2 = (kind == IVY_RESOLVER_IBIBLIO);
    m2 = xml_node_get_attr(node, "m2compatible");
    r->m2compatible = m2 ? parse_boolean(m2, default_m2) : default_m2;

    if (kind == IVY_RESOLVER_IBIBLIO) {
        /* ibiblio's layout is fixed by m2compatible, not user-customizable
         * in v1 - matches real Ivy, where an m2compatible ibiblio resolver's
         * pattern can't be overridden either. */
        if (r->m2compatible) {
            r->pattern = strdup("[orgPath]/[module]/[revision]/[module]-[revision].[ext]");
        } else {
            r->pattern = strdup("[organisation]/[module]/[type]s/[artifact]-[revision].[ext]");
            r->ivy_pattern = strdup("[organisation]/[module]/ivy-[revision].xml");
        }
    } else {
        /* filesystem/url: explicit pattern / ivy pattern, as attributes or
         * as nested <artifact pattern=".."/> / <ivy pattern=".."/> elements,
         * falling back to the same conventions ibiblio would use. */
        const char *pattern_attr = xml_node_get_attr(node, "pattern");
        xml_node_t *cur;

        if (pattern_attr) {
            r->pattern = expand_props(pattern_attr, project);
        }
        for (cur = node->children; cur; cur = cur->next) {
            if (xml_streq(cur->name, "artifact")) {
                const char *p = xml_node_get_attr(cur, "pattern");
                if (p) {
                    free(r->pattern);
                    r->pattern = expand_props(p, project);
                }
            } else if (xml_streq(cur->name, "ivy")) {
                const char *p = xml_node_get_attr(cur, "pattern");
                if (p) {
                    r->ivy_pattern = expand_props(p, project);
                }
            }
        }
        if (!r->pattern) {
            r->pattern = r->m2compatible
                ? strdup("[orgPath]/[module]/[revision]/[module]-[revision].[ext]")
                : strdup("[organisation]/[module]/[type]s/[artifact]-[revision].[ext]");
        }
        if (!r->ivy_pattern && !r->m2compatible) {
            r->ivy_pattern = strdup("[organisation]/[module]/ivy-[revision].xml");
        }
    }

    register_resolver(settings, r, name, anon_counter);
    return r;
}

ivy_settings_t *ivy_settings_load(const char *settings_file, project_t *project)
{
    xml_doc_t *doc;
    xml_node_t *root;
    ivy_settings_t *settings;
    char *cache_dir = NULL;
    char *default_resolver_name = NULL;
    int anon_counter = 0;
    int resolver_count = 0;
    ivy_resolver_t *last_resolver = NULL;
    xml_node_t *cur;

    doc = xml_parse_file(settings_file);
    if (!doc) {
        return NULL;
    }

    root = xml_doc_get_root(doc);
    if (!root || !xml_streq(root->name, "ivysettings")) {
        fprintf(stderr, "ivy: %s: not an ivysettings document\n", settings_file);
        xml_doc_free(doc);
        return NULL;
    }

    settings = calloc(1, sizeof(ivy_settings_t));
    settings->resolvers = hashtable_new();

    for (cur = root->children; cur; cur = cur->next) {
        if (xml_streq(cur->name, "caches")) {
            const char *dir = xml_node_get_attr(cur, "defaultCacheDir");
            if (dir) {
                cache_dir = expand_props(dir, project);
            }
        } else if (xml_streq(cur->name, "settings")) {
            const char *dr = xml_node_get_attr(cur, "defaultResolver");
            if (dr) {
                default_resolver_name = strdup(dr);
            }
        } else if (xml_streq(cur->name, "resolvers")) {
            xml_node_t *rnode;
            for (rnode = cur->children; rnode; rnode = rnode->next) {
                ivy_resolver_t *r = parse_resolver_node(rnode, settings, project, &anon_counter);
                if (r) {
                    resolver_count++;
                    last_resolver = r;
                }
            }
        }
    }

    xml_doc_free(doc);

    if (default_resolver_name) {
        settings->default_resolver = hashtable_lookup(settings->resolvers, default_resolver_name);
        if (!settings->default_resolver) {
            fprintf(stderr, "ivy: %s: defaultResolver '%s' not found\n",
                    settings_file, default_resolver_name);
        }
        free(default_resolver_name);
    }

    if (!settings->default_resolver && resolver_count == 1) {
        /* A common convenience real Ivy also affords: a single declared
         * resolver is used even without an explicit defaultResolver. */
        settings->default_resolver = last_resolver;
    }

    if (!settings->default_resolver) {
        fprintf(stderr, "ivy: %s: no defaultResolver could be determined\n", settings_file);
        free(cache_dir);
        ivy_settings_free(settings);
        return NULL;
    }

    settings->cache_dir = cache_dir ? cache_dir : default_cache_dir(project);

    return settings;
}

ivy_settings_t *ivy_settings_default(project_t *project)
{
    ivy_settings_t *settings = calloc(1, sizeof(ivy_settings_t));
    ivy_resolver_t *r = resolver_alloc(IVY_RESOLVER_IBIBLIO);

    r->name = strdup("central");
    r->root = strdup("https://repo1.maven.org/maven2/");
    r->m2compatible = true;

    settings->resolvers = hashtable_new();
    hashtable_insert(settings->resolvers, "central", r);
    settings->default_resolver = r;
    settings->cache_dir = default_cache_dir(project);

    return settings;
}

void ivy_settings_free(ivy_settings_t *settings)
{
    if (!settings) {
        return;
    }
    hashtable_free_full(settings->resolvers, resolver_free);
    free(settings->cache_dir);
    free(settings);
}

/* ========================================================================
 * Resolution results: freeing
 * ======================================================================== */

static void artifact_free(void *p)
{
    ivy_artifact_t *a = p;
    if (!a) {
        return;
    }
    ivy_module_id_clear(&a->id);
    free(a->type);
    free(a->ext);
    free(a->conf);
    free(a->cached_path);
    free(a);
}

static void resolved_module_free(void *p)
{
    ivy_resolved_module_t *m = p;
    if (!m) {
        return;
    }
    ivy_module_id_clear(&m->id);
    ivy_module_descriptor_free(m->descriptor);
    slist_free_full(m->artifacts, artifact_free);
    slist_free_full(m->confs, free);
    free(m);
}

static void resolved_module_free_ht(const char *key, void *value, void *user_data)
{
    (void)key;
    (void)user_data;
    resolved_module_free(value);
}

void ivy_resolution_free(ivy_resolution_t *resolution)
{
    if (!resolution) {
        return;
    }
    hashtable_foreach(resolution->modules, resolved_module_free_ht, NULL);
    hashtable_free(resolution->modules);
    slist_free_full(resolution->conf_names, free);
    free(resolution);
}
