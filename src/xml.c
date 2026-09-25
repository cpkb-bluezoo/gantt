/*
 * xml.c
 * SAX-style XML parser using expat - replaces libxml2 dependency
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

#include "xml.h"
#include <errno.h>

/* ========================================================================
 * XML Attribute helper implementation
 * ======================================================================== */

/**
 * Get an attribute value from an expat attribute array.
 * The attribute array is in the format: [name1, value1, name2, value2, ..., NULL]
 * 
 * @param attrs  The expat attribute array
 * @param name   The attribute name to look for
 * @return       The attribute value, or NULL if not found
 */
const char *xml_get_attr(const char **attrs, const char *name)
{
    if (!attrs || !name) {
        return NULL;
    }
    
    for (int i = 0; attrs[i] != NULL; i += 2) {
        if (strcmp(attrs[i], name) == 0) {
            return attrs[i + 1];
        }
    }
    
    return NULL;
}

/**
 * Get an attribute value from an expat attribute array and duplicate it.
 * 
 * @param attrs  The expat attribute array
 * @param name   The attribute name to look for
 * @return       A newly allocated copy of the value, or NULL if not found
 *               Caller must free the result.
 */
char *xml_get_attr_dup(const char **attrs, const char *name)
{
    const char *value = xml_get_attr(attrs, name);
    return value ? strdup(value) : NULL;
}

/* ========================================================================
 * XML Node implementation
 * ======================================================================== */

/**
 * Create a new XML node with the given element name.
 * 
 * @param name  The element name (can be NULL)
 * @return      A new XML node, or NULL on allocation failure
 */
xml_node_t *xml_node_new(const char *name)
{
    xml_node_t *node = calloc(1, sizeof(xml_node_t));
    if (!node) {
        return NULL;
    }
    
    node->name = name ? strdup(name) : NULL;
    return node;
}

/**
 * Free an XML node and all its children recursively.
 * 
 * @param node  The node to free
 */
void xml_node_free(xml_node_t *node)
{
    if (!node) {
        return;
    }
    
    /* Free attributes */
    xml_attr_t *attr = node->attrs;
    while (attr) {
        xml_attr_t *next = attr->next;
        free(attr->name);
        free(attr->value);
        free(attr);
        attr = next;
    }
    
    /* Free children recursively */
    xml_node_t *child = node->children;
    while (child) {
        xml_node_t *next = child->next;
        xml_node_free(child);
        child = next;
    }
    
    free(node->name);
    free(node->text);
    free(node);
}

/**
 * Add an attribute to an XML node.
 * The name and value are copied.
 * 
 * @param node   The node to add the attribute to
 * @param name   The attribute name (must not be NULL)
 * @param value  The attribute value (can be NULL)
 */
void xml_node_add_attr(xml_node_t *node, const char *name, const char *value)
{
    if (!node || !name) {
        return;
    }
    
    xml_attr_t *attr = malloc(sizeof(xml_attr_t));
    if (!attr) {
        return;
    }
    
    attr->name = strdup(name);
    attr->value = value ? strdup(value) : NULL;
    attr->next = node->attrs;
    node->attrs = attr;
}

/**
 * Get an attribute value from an XML node.
 * 
 * @param node  The node to search
 * @param name  The attribute name to look for
 * @return      The attribute value, or NULL if not found
 */
const char *xml_node_get_attr(xml_node_t *node, const char *name)
{
    if (!node || !name) {
        return NULL;
    }
    
    for (xml_attr_t *attr = node->attrs; attr; attr = attr->next) {
        if (strcmp(attr->name, name) == 0) {
            return attr->value;
        }
    }
    
    return NULL;
}

/**
 * Get an attribute value from an XML node and duplicate it.
 * 
 * @param node  The node to search
 * @param name  The attribute name to look for
 * @return      A newly allocated copy of the value, or NULL if not found
 *              Caller must free the result.
 */
char *xml_node_get_attr_dup(xml_node_t *node, const char *name)
{
    const char *value = xml_node_get_attr(node, name);
    return value ? strdup(value) : NULL;
}

/**
 * Append text content to an XML node.
 * If the node already has text, the new text is concatenated.
 * 
 * @param node  The node to append text to
 * @param text  The text to append
 * @param len   The length of the text
 */
void xml_node_append_text(xml_node_t *node, const char *text, int len)
{
    if (!node || !text || len <= 0) {
        return;
    }
    
    if (!node->text) {
        node->text = strndup(text, len);
    } else {
        size_t old_len = strlen(node->text);
        char *new_text = realloc(node->text, old_len + len + 1);
        if (new_text) {
            memcpy(new_text + old_len, text, len);
            new_text[old_len + len] = '\0';
            node->text = new_text;
        }
    }
}

/**
 * Add a child node to a parent node.
 * The child is appended to the end of the parent's children list.
 * 
 * @param parent  The parent node
 * @param child   The child node to add
 */
void xml_node_add_child(xml_node_t *parent, xml_node_t *child)
{
    if (!parent || !child) {
        return;
    }
    
    child->parent = parent;
    child->next = NULL;
    
    if (!parent->children) {
        parent->children = child;
    } else {
        /* Find last child */
        xml_node_t *last = parent->children;
        while (last->next) {
            last = last->next;
        }
        last->next = child;
    }
}

/**
 * Get the text content of an XML node with leading/trailing whitespace stripped.
 * 
 * @param node  The node to get text from
 * @return      A newly allocated string with the stripped text, or NULL
 *              Caller must free the result.
 */
char *xml_node_get_text(xml_node_t *node)
{
    if (!node || !node->text) {
        return NULL;
    }
    
    /* Duplicate and strip whitespace */
    char *text = strdup(node->text);
    if (text) {
        /* Note: str_strip modifies in place and may return pointer into string */
        char *stripped = str_strip(text);
        if (stripped != text) {
            /* Move stripped content to beginning if needed */
            memmove(text, stripped, strlen(stripped) + 1);
        }
    }
    return text;
}

/* ========================================================================
 * Namespace normalization
 * ======================================================================== */

/* Namespace URIs gantt currently recognises. */
#define IVY_ANTLIB_URI "antlib:org.apache.ivy.ant"
/*
 * Maven POM files (fetched from repositories as ivy.xml's m2compatible
 * fallback) declare this as their default xmlns, which namespace-qualifies
 * every element in the file. Recognised here so ivy_pom_parse_file() (see
 * ivy_parse.c) can match plain local names ("project", "dependency", ...)
 * without every element triggering the "unrecognised namespace" warning
 * below - this isn't a dispatch prefix like ivy: above, just a name a POM
 * parser needs to see in its bare form.
 */
#define MAVEN_POM_URI "http://maven.apache.org/POM/4.0.0"

char *xml_ns_normalize(const char *raw_name)
{
    const char *sep;
    const char *local;
    size_t uri_len;

    if (!raw_name) {
        return NULL;
    }

    sep = strchr(raw_name, XML_NS_SEP);
    if (!sep) {
        /* No active namespace on this element - pass through unchanged. */
        return strdup(raw_name);
    }

    local = sep + 1;
    uri_len = (size_t)(sep - raw_name);

    if (uri_len == strlen(IVY_ANTLIB_URI) &&
        strncmp(raw_name, IVY_ANTLIB_URI, uri_len) == 0) {
        return str_concat("ivy:", local, NULL);
    }

    if (uri_len == strlen(MAVEN_POM_URI) &&
        strncmp(raw_name, MAVEN_POM_URI, uri_len) == 0) {
        return strdup(local);
    }

    fprintf(stderr, "Warning: element <%s> uses an unrecognised XML namespace\n", local);
    return strdup(local);
}

/* ========================================================================
 * DOM-style parsing implementation (builds tree in memory)
 * ======================================================================== */

/**
 * Context structure for DOM-style parsing.
 */
typedef struct {
    xml_doc_t *doc;      /* The document being built */
    xml_node_t *current; /* Current node in the tree */
    int depth;           /* Current nesting depth */
} dom_parse_context_t;

/**
 * Expat callback for element start tags (DOM mode).
 * Creates a new node and adds it to the tree.
 */
static void XMLCALL dom_start_element(void *user_data, 
                                       const char *name, 
                                       const char **attrs)
{
    dom_parse_context_t *ctx = user_data;
    char *normalized_name = xml_ns_normalize(name);

    xml_node_t *node = xml_node_new(normalized_name);
    free(normalized_name);
    if (!node) {
        return;
    }
    
    /* Copy attributes */
    for (int i = 0; attrs[i] != NULL; i += 2) {
        xml_node_add_attr(node, attrs[i], attrs[i + 1]);
    }
    
    if (ctx->depth == 0) {
        /* This is the root element */
        ctx->doc->root = node;
    } else {
        /* Add as child of current node */
        xml_node_add_child(ctx->current, node);
    }
    
    ctx->current = node;
    ctx->depth++;
}

/**
 * Expat callback for element end tags (DOM mode).
 * Moves the current node pointer back to the parent.
 */
static void XMLCALL dom_end_element(void *user_data, const char *name)
{
    dom_parse_context_t *ctx = user_data;
    (void)name;  /* Unused */
    
    if (ctx->current && ctx->current->parent) {
        ctx->current = ctx->current->parent;
    }
    ctx->depth--;
}

/**
 * Expat callback for character data (DOM mode).
 * Appends text to the current node.
 */
static void XMLCALL dom_char_data(void *user_data, const char *data, int len)
{
    dom_parse_context_t *ctx = user_data;
    
    if (ctx->current) {
        xml_node_append_text(ctx->current, data, len);
    }
}

/**
 * Parse an XML file and build a DOM-like tree in memory.
 * 
 * @param filename  The path to the XML file
 * @return          The parsed document, or NULL on error
 *                  Caller must free with xml_doc_free().
 */
xml_doc_t *xml_parse_file(const char *filename)
{
    if (!filename) {
        return NULL;
    }
    
    /* Read file contents */
    size_t length;
    char *contents = file_get_contents(filename, &length);
    if (!contents) {
        fprintf(stderr, "Unable to read file: %s\n", filename);
        return NULL;
    }
    
    /* Create document */
    xml_doc_t *doc = calloc(1, sizeof(xml_doc_t));
    if (!doc) {
        free(contents);
        return NULL;
    }
    doc->filename = strdup(filename);
    
    /* Set up parse context */
    dom_parse_context_t ctx = {
        .doc = doc,
        .current = NULL,
        .depth = 0
    };
    
    /* Create parser */
    XML_Parser parser = XML_ParserCreateNS(NULL, XML_NS_SEP);
    if (!parser) {
        free(contents);
        free(doc->filename);
        free(doc);
        return NULL;
    }
    
    XML_SetUserData(parser, &ctx);
    XML_SetElementHandler(parser, dom_start_element, dom_end_element);
    XML_SetCharacterDataHandler(parser, dom_char_data);
    
    /* Parse */
    if (XML_Parse(parser, contents, length, XML_TRUE) == XML_STATUS_ERROR) {
        fprintf(stderr, "XML parse error at line %lu: %s\n",
                XML_GetCurrentLineNumber(parser),
                XML_ErrorString(XML_GetErrorCode(parser)));
        XML_ParserFree(parser);
        free(contents);
        xml_doc_free(doc);
        return NULL;
    }
    
    XML_ParserFree(parser);
    free(contents);
    
    return doc;
}

/**
 * Free an XML document and all its nodes.
 * 
 * @param doc  The document to free
 */
void xml_doc_free(xml_doc_t *doc)
{
    if (!doc) {
        return;
    }
    
    xml_node_free(doc->root);
    free(doc->filename);
    free(doc);
}

/**
 * Get the root element of an XML document.
 * 
 * @param doc  The document
 * @return     The root element, or NULL if document is empty
 */
xml_node_t *xml_doc_get_root(xml_doc_t *doc)
{
    return doc ? doc->root : NULL;
}

/* ========================================================================
 * SAX-style parsing implementation
 * ======================================================================== */

/**
 * Context structure for SAX-style parsing.
 */
typedef struct {
    xml_sax_handler_t *handler;  /* User-provided callbacks */
    void *user_data;             /* User data passed to callbacks */
} sax_parse_context_t;

/**
 * Expat callback for element start tags (SAX mode).
 * Calls the user's start_element handler.
 */
static void XMLCALL sax_start_element(void *data, 
                                       const char *name, 
                                       const char **attrs)
{
    sax_parse_context_t *ctx = data;
    if (ctx->handler && ctx->handler->start_element) {
        char *normalized_name = xml_ns_normalize(name);
        ctx->handler->start_element(ctx->user_data, normalized_name, attrs);
        free(normalized_name);
    }
}

/**
 * Expat callback for element end tags (SAX mode).
 * Calls the user's end_element handler.
 */
static void XMLCALL sax_end_element(void *data, const char *name)
{
    sax_parse_context_t *ctx = data;
    if (ctx->handler && ctx->handler->end_element) {
        ctx->handler->end_element(ctx->user_data, name);
    }
}

/**
 * Expat callback for character data (SAX mode).
 * Calls the user's char_data handler.
 */
static void XMLCALL sax_char_data(void *data, const char *s, int len)
{
    sax_parse_context_t *ctx = data;
    if (ctx->handler && ctx->handler->char_data) {
        ctx->handler->char_data(ctx->user_data, s, len);
    }
}

/**
 * Parse an XML file using SAX-style callbacks.
 * This is a streaming parser that doesn't build a tree in memory.
 * 
 * @param filename   The path to the XML file
 * @param handler    Structure containing callback functions
 * @param user_data  User data to pass to the callbacks
 * @return           true on success, false on error
 */
bool xml_sax_parse_file(const char *filename,
                        xml_sax_handler_t *handler,
                        void *user_data)
{
    if (!filename) {
        return false;
    }
    
    /* Read file contents */
    size_t length;
    char *contents = file_get_contents(filename, &length);
    if (!contents) {
        fprintf(stderr, "Unable to read file: %s\n", filename);
        return false;
    }
    
    /* Set up parse context */
    sax_parse_context_t ctx = {
        .handler = handler,
        .user_data = user_data
    };
    
    /* Create parser */
    XML_Parser parser = XML_ParserCreateNS(NULL, XML_NS_SEP);
    if (!parser) {
        free(contents);
        return false;
    }
    
    XML_SetUserData(parser, &ctx);
    XML_SetElementHandler(parser, sax_start_element, sax_end_element);
    XML_SetCharacterDataHandler(parser, sax_char_data);
    
    /* Parse */
    bool success = true;
    if (XML_Parse(parser, contents, length, XML_TRUE) == XML_STATUS_ERROR) {
        fprintf(stderr, "XML parse error at line %lu: %s\n",
                XML_GetCurrentLineNumber(parser),
                XML_ErrorString(XML_GetErrorCode(parser)));
        success = false;
    }
    
    XML_ParserFree(parser);
    free(contents);
    
    return success;
}

/* ========================================================================
 * Utility functions implementation
 * ======================================================================== */

/**
 * Compare two XML strings (NULL-safe strcmp).
 * 
 * @param a  First string (can be NULL)
 * @param b  Second string (can be NULL)
 * @return   0 if equal, negative if a < b, positive if a > b
 */
int xml_strcmp(const char *a, const char *b)
{
    if (a == b) {
        return 0;
    }
    if (!a) {
        return -1;
    }
    if (!b) {
        return 1;
    }
    return strcmp(a, b);
}

/**
 * Check if two XML strings are equal (NULL-safe).
 * 
 * @param a  First string (can be NULL)
 * @param b  Second string (can be NULL)
 * @return   true if strings are equal, false otherwise
 */
bool xml_streq(const char *a, const char *b)
{
    return xml_strcmp(a, b) == 0;
}
