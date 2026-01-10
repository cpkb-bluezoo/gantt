/*
 * xml.h
 * SAX-style XML parser using expat - replaces libxml2 dependency
 *
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

#ifndef XML_H
#define XML_H

#include "util.h"
#include <expat.h>

/* ========================================================================
 * XML Attribute helper
 * ======================================================================== */

/*
 * Get an attribute value from the expat attrs array.
 * The attrs array is formatted as: [name1, value1, name2, value2, ..., NULL]
 * Returns NULL if attribute not found.
 */
const char *xml_get_attr(const char **attrs, const char *name);

/*
 * Get an attribute value and duplicate it (caller must free).
 * Returns NULL if attribute not found.
 */
char *xml_get_attr_dup(const char **attrs, const char *name);

/* ========================================================================
 * XML Node representation (for building a DOM-like structure during parsing)
 * ======================================================================== */

typedef struct xml_attr {
    char *name;
    char *value;
    struct xml_attr *next;
} xml_attr_t;

typedef struct xml_node {
    char *name;                  /* Element name */
    xml_attr_t *attrs;           /* Linked list of attributes */
    char *text;                  /* Text content (accumulated) */
    struct xml_node *parent;     /* Parent node */
    struct xml_node *children;   /* First child */
    struct xml_node *next;       /* Next sibling */
} xml_node_t;

/* Create a new XML node */
xml_node_t *xml_node_new(const char *name);

/* Free an XML node and all its children */
void xml_node_free(xml_node_t *node);

/* Add an attribute to a node */
void xml_node_add_attr(xml_node_t *node, const char *name, const char *value);

/* Get an attribute value from a node */
const char *xml_node_get_attr(xml_node_t *node, const char *name);

/* Get attribute value as a duplicate (caller must free) */
char *xml_node_get_attr_dup(xml_node_t *node, const char *name);

/* Append text to a node's text content */
void xml_node_append_text(xml_node_t *node, const char *text, int len);

/* Add a child node */
void xml_node_add_child(xml_node_t *parent, xml_node_t *child);

/* Get the trimmed text content of a node (caller must free) */
char *xml_node_get_text(xml_node_t *node);

/* ========================================================================
 * XML Document representation
 * ======================================================================== */

typedef struct xml_doc {
    xml_node_t *root;            /* Root element */
    char *filename;              /* Source filename */
} xml_doc_t;

/* Parse an XML file into a document structure */
xml_doc_t *xml_parse_file(const char *filename);

/* Free an XML document */
void xml_doc_free(xml_doc_t *doc);

/* Get the root element of a document */
xml_node_t *xml_doc_get_root(xml_doc_t *doc);

/* ========================================================================
 * SAX-style parsing (for streaming/callback-based parsing)
 * ======================================================================== */

/*
 * Callback function types for SAX-style parsing.
 * These are called as the parser encounters elements.
 */
typedef void (*xml_start_element_fn)(void *user_data, 
                                      const char *name, 
                                      const char **attrs);
typedef void (*xml_end_element_fn)(void *user_data, 
                                    const char *name);
typedef void (*xml_char_data_fn)(void *user_data, 
                                  const char *data, 
                                  int len);

typedef struct xml_sax_handler {
    xml_start_element_fn start_element;
    xml_end_element_fn end_element;
    xml_char_data_fn char_data;
} xml_sax_handler_t;

/*
 * Parse an XML file using SAX callbacks.
 * Returns true on success, false on error.
 */
bool xml_sax_parse_file(const char *filename,
                        xml_sax_handler_t *handler,
                        void *user_data);

/* ========================================================================
 * Utility functions
 * ======================================================================== */

/* Compare two XML strings (like xmlStrcmp) */
int xml_strcmp(const char *a, const char *b);

/* Check if an XML string equals a C string */
bool xml_streq(const char *a, const char *b);

#endif /* XML_H */

