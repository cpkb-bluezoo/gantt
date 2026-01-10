/*
 * util.c
 * Utility functions and data structures - replaces glib dependency
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

#include "util.h"
#include <errno.h>
#include <time.h>

/* ========================================================================
 * Singly-linked list implementation
 * ======================================================================== */

/**
 * Create a new singly-linked list node with the given data.
 * 
 * @param data  The data to store in the node (can be NULL)
 * @return      A new list node, or NULL on allocation failure
 */
slist_t *slist_new(void *data)
{
    slist_t *node = malloc(sizeof(slist_t));
    if (!node) {
        return NULL;
    }
    node->data = data;
    node->next = NULL;
    return node;
}

/**
 * Append data to the end of a singly-linked list.
 * 
 * @param list  The list to append to (can be NULL to create new list)
 * @param data  The data to append
 * @return      The newly created tail node, or NULL on failure
 */
slist_t *slist_append(slist_t *list, void *data)
{
    slist_t *node = slist_new(data);
    if (!node) {
        return NULL;
    }
    
    if (!list) {
        return node;
    }
    
    slist_t *tail = slist_last(list);
    tail->next = node;
    return node;
}

/**
 * Prepend data to the beginning of a singly-linked list.
 * 
 * @param list  The existing list (can be NULL)
 * @param data  The data to prepend
 * @return      The new head of the list
 */
slist_t *slist_prepend(slist_t *list, void *data)
{
    slist_t *node = slist_new(data);
    if (!node) {
        return list;
    }
    node->next = list;
    return node;
}

/**
 * Get the last node in a singly-linked list.
 * 
 * @param list  The list to traverse
 * @return      The last node, or NULL if list is empty
 */
slist_t *slist_last(slist_t *list)
{
    if (!list) {
        return NULL;
    }
    while (list->next) {
        list = list->next;
    }
    return list;
}

/**
 * Count the number of elements in a singly-linked list.
 * 
 * @param list  The list to count
 * @return      The number of elements
 */
size_t slist_length(slist_t *list)
{
    size_t count = 0;
    while (list) {
        count++;
        list = list->next;
    }
    return count;
}

/**
 * Free all nodes in a singly-linked list.
 * Does not free the data stored in each node.
 * 
 * @param list  The list to free
 */
void slist_free(slist_t *list)
{
    while (list) {
        slist_t *next = list->next;
        free(list);
        list = next;
    }
}

/**
 * Free all nodes in a singly-linked list and their data.
 * 
 * @param list       The list to free
 * @param free_func  Function to call on each node's data (can be NULL)
 */
void slist_free_full(slist_t *list, void (*free_func)(void*))
{
    while (list) {
        slist_t *next = list->next;
        if (free_func && list->data) {
            free_func(list->data);
        }
        free(list);
        list = next;
    }
}

/* ========================================================================
 * Doubly-linked list implementation
 * ======================================================================== */

/**
 * Create a new doubly-linked list node with the given data.
 * 
 * @param data  The data to store in the node (can be NULL)
 * @return      A new list node, or NULL on allocation failure
 */
dlist_t *dlist_new(void *data)
{
    dlist_t *node = malloc(sizeof(dlist_t));
    if (!node) {
        return NULL;
    }
    node->data = data;
    node->prev = NULL;
    node->next = NULL;
    return node;
}

/**
 * Append data to the end of a doubly-linked list.
 * 
 * @param list  The list to append to (can be NULL to create new list)
 * @param data  The data to append
 * @return      The newly created tail node, or NULL on failure
 */
dlist_t *dlist_append(dlist_t *list, void *data)
{
    dlist_t *node = dlist_new(data);
    if (!node) {
        return NULL;
    }
    
    if (!list) {
        return node;
    }
    
    dlist_t *tail = dlist_last(list);
    tail->next = node;
    node->prev = tail;
    return node;
}

/**
 * Prepend data to the beginning of a doubly-linked list.
 * 
 * @param list  The existing list (can be NULL)
 * @param data  The data to prepend
 * @return      The new head of the list
 */
dlist_t *dlist_prepend(dlist_t *list, void *data)
{
    dlist_t *node = dlist_new(data);
    if (!node) {
        return list;
    }
    
    if (list) {
        dlist_t *head = dlist_first(list);
        head->prev = node;
        node->next = head;
    }
    return node;
}

/**
 * Get the first node in a doubly-linked list.
 * Works from any node in the list.
 * 
 * @param list  Any node in the list
 * @return      The first node, or NULL if list is empty
 */
dlist_t *dlist_first(dlist_t *list)
{
    if (!list) {
        return NULL;
    }
    while (list->prev) {
        list = list->prev;
    }
    return list;
}

/**
 * Get the last node in a doubly-linked list.
 * Works from any node in the list.
 * 
 * @param list  Any node in the list
 * @return      The last node, or NULL if list is empty
 */
dlist_t *dlist_last(dlist_t *list)
{
    if (!list) {
        return NULL;
    }
    while (list->next) {
        list = list->next;
    }
    return list;
}

/**
 * Count the number of elements in a doubly-linked list.
 * Works from any node in the list.
 * 
 * @param list  Any node in the list
 * @return      The total number of elements in the list
 */
size_t dlist_length(dlist_t *list)
{
    if (!list) {
        return 0;
    }
    
    /* Go to first node */
    list = dlist_first(list);
    
    size_t count = 0;
    while (list) {
        count++;
        list = list->next;
    }
    return count;
}

/**
 * Free all nodes in a doubly-linked list.
 * Does not free the data stored in each node.
 * Works from any node in the list.
 * 
 * @param list  Any node in the list
 */
void dlist_free(dlist_t *list)
{
    if (!list) {
        return;
    }
    
    /* Go to first node */
    list = dlist_first(list);
    
    while (list) {
        dlist_t *next = list->next;
        free(list);
        list = next;
    }
}

/**
 * Free all nodes in a doubly-linked list and their data.
 * Works from any node in the list.
 * 
 * @param list       Any node in the list
 * @param free_func  Function to call on each node's data (can be NULL)
 */
void dlist_free_full(dlist_t *list, void (*free_func)(void*))
{
    if (!list) {
        return;
    }
    
    /* Go to first node */
    list = dlist_first(list);
    
    while (list) {
        dlist_t *next = list->next;
        if (free_func && list->data) {
            free_func(list->data);
        }
        free(list);
        list = next;
    }
}

/* ========================================================================
 * Hash table implementation
 * ======================================================================== */

/**
 * Compute a hash value for a string using the djb2 variant algorithm.
 * 
 * @param str  The string to hash (can be NULL)
 * @return     The computed hash value (0 for NULL strings)
 */
unsigned int str_hash(const char *str)
{
    unsigned int hash = 0;
    if (!str) {
        return 0;
    }
    
    while (*str) {
        hash = (hash * 31) + (unsigned char)*str;
        str++;
    }
    return hash;
}

/**
 * Resize a hash table to a new size.
 * All entries are rehashed into the new bucket array.
 * 
 * @param ht        The hash table to resize
 * @param new_size  The new number of buckets
 */
static void hashtable_resize(hashtable_t *ht, size_t new_size)
{
    hashtable_entry_t **old_buckets = ht->buckets;
    size_t old_size = ht->size;
    
    ht->buckets = calloc(new_size, sizeof(hashtable_entry_t*));
    if (!ht->buckets) {
        ht->buckets = old_buckets;
        return;
    }
    ht->size = new_size;
    ht->count = 0;
    
    /* Rehash all entries */
    for (size_t i = 0; i < old_size; i++) {
        hashtable_entry_t *entry = old_buckets[i];
        while (entry) {
            hashtable_entry_t *next = entry->next;
            
            /* Insert into new bucket */
            unsigned int index = str_hash(entry->key) % new_size;
            entry->next = ht->buckets[index];
            ht->buckets[index] = entry;
            ht->count++;
            
            entry = next;
        }
    }
    
    free(old_buckets);
}

/**
 * Create a new hash table with string keys.
 * 
 * @return  A new hash table, or NULL on allocation failure
 */
hashtable_t *hashtable_new(void)
{
    hashtable_t *ht = malloc(sizeof(hashtable_t));
    if (!ht) {
        return NULL;
    }
    
    ht->size = HASHTABLE_INITIAL_SIZE;
    ht->count = 0;
    ht->buckets = calloc(ht->size, sizeof(hashtable_entry_t*));
    
    if (!ht->buckets) {
        free(ht);
        return NULL;
    }
    
    return ht;
}

/**
 * Free a hash table and all its entries.
 * Does not free the values stored in the table.
 * 
 * @param ht  The hash table to free
 */
void hashtable_free(hashtable_t *ht)
{
    if (!ht) {
        return;
    }
    
    for (size_t i = 0; i < ht->size; i++) {
        hashtable_entry_t *entry = ht->buckets[i];
        while (entry) {
            hashtable_entry_t *next = entry->next;
            free(entry->key);
            free(entry);
            entry = next;
        }
    }
    
    free(ht->buckets);
    free(ht);
}

/**
 * Free a hash table, all its entries, and all stored values.
 * 
 * @param ht         The hash table to free
 * @param free_func  Function to call on each value (can be NULL)
 */
void hashtable_free_full(hashtable_t *ht, void (*free_func)(void*))
{
    if (!ht) {
        return;
    }
    
    for (size_t i = 0; i < ht->size; i++) {
        hashtable_entry_t *entry = ht->buckets[i];
        while (entry) {
            hashtable_entry_t *next = entry->next;
            free(entry->key);
            if (free_func && entry->value) {
                free_func(entry->value);
            }
            free(entry);
            entry = next;
        }
    }
    
    free(ht->buckets);
    free(ht);
}

/**
 * Insert or update a key-value pair in the hash table.
 * The key is copied; the value pointer is stored directly.
 * If the key already exists, the value is updated.
 * 
 * @param ht     The hash table
 * @param key    The key (must not be NULL)
 * @param value  The value to store
 */
void hashtable_insert(hashtable_t *ht, const char *key, void *value)
{
    if (!ht || !key) {
        return;
    }
    
    /* Check if we need to resize */
    if ((double)ht->count / ht->size >= HASHTABLE_LOAD_FACTOR) {
        hashtable_resize(ht, ht->size * 2);
    }
    
    unsigned int index = str_hash(key) % ht->size;
    
    /* Check if key already exists */
    hashtable_entry_t *entry = ht->buckets[index];
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            entry->value = value;  /* Update existing */
            return;
        }
        entry = entry->next;
    }
    
    /* Create new entry */
    entry = malloc(sizeof(hashtable_entry_t));
    if (!entry) {
        return;
    }
    
    entry->key = strdup(key);
    entry->value = value;
    entry->next = ht->buckets[index];
    ht->buckets[index] = entry;
    ht->count++;
}

/**
 * Look up a value by key in the hash table.
 * 
 * @param ht   The hash table
 * @param key  The key to look up
 * @return     The associated value, or NULL if not found
 */
void *hashtable_lookup(hashtable_t *ht, const char *key)
{
    if (!ht || !key) {
        return NULL;
    }
    
    unsigned int index = str_hash(key) % ht->size;
    hashtable_entry_t *entry = ht->buckets[index];
    
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            return entry->value;
        }
        entry = entry->next;
    }
    
    return NULL;
}

/**
 * Remove a key-value pair from the hash table.
 * The key is freed; the value is returned to the caller.
 * 
 * @param ht   The hash table
 * @param key  The key to remove
 * @return     The removed value, or NULL if key not found
 */
void *hashtable_remove(hashtable_t *ht, const char *key)
{
    if (!ht || !key) {
        return NULL;
    }
    
    unsigned int index = str_hash(key) % ht->size;
    hashtable_entry_t *entry = ht->buckets[index];
    hashtable_entry_t *prev = NULL;
    
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            void *value = entry->value;
            
            if (prev) {
                prev->next = entry->next;
            } else {
                ht->buckets[index] = entry->next;
            }
            
            free(entry->key);
            free(entry);
            ht->count--;
            return value;
        }
        prev = entry;
        entry = entry->next;
    }
    
    return NULL;
}

/**
 * Check if a key exists in the hash table.
 * 
 * @param ht   The hash table
 * @param key  The key to check
 * @return     true if the key exists, false otherwise
 */
bool hashtable_contains(hashtable_t *ht, const char *key)
{
    if (!ht || !key) {
        return false;
    }
    
    unsigned int index = str_hash(key) % ht->size;
    hashtable_entry_t *entry = ht->buckets[index];
    
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            return true;
        }
        entry = entry->next;
    }
    
    return false;
}

/**
 * Iterate over all entries in the hash table.
 * The callback function is called for each key-value pair.
 * 
 * @param ht         The hash table
 * @param fn         Callback function to call for each entry
 * @param user_data  User data to pass to the callback
 */
void hashtable_foreach(hashtable_t *ht, hashtable_foreach_fn fn, void *user_data)
{
    if (!ht || !fn) {
        return;
    }
    
    for (size_t i = 0; i < ht->size; i++) {
        hashtable_entry_t *entry = ht->buckets[i];
        while (entry) {
            fn(entry->key, entry->value, user_data);
            entry = entry->next;
        }
    }
}

/* ========================================================================
 * Dynamic string implementation
 * ======================================================================== */

#define STRING_INITIAL_SIZE 64

/**
 * Ensure a string has enough capacity for the given size.
 * Grows the buffer exponentially if needed.
 * 
 * @param str     The string to check/grow
 * @param needed  The minimum required capacity (including null terminator)
 */
static void string_ensure_capacity(string_t *str, size_t needed)
{
    if (str->alloc >= needed) {
        return;
    }
    
    size_t new_alloc = str->alloc ? str->alloc : STRING_INITIAL_SIZE;
    while (new_alloc < needed) {
        new_alloc *= 2;
    }
    
    char *new_str = realloc(str->str, new_alloc);
    if (!new_str) {
        return;
    }
    
    str->str = new_str;
    str->alloc = new_alloc;
}

/**
 * Create a new dynamic string, optionally initialized with content.
 * 
 * @param init  Initial string content (can be NULL for empty string)
 * @return      A new string object, or NULL on allocation failure
 */
string_t *string_new(const char *init)
{
    string_t *str = malloc(sizeof(string_t));
    if (!str) {
        return NULL;
    }
    
    str->str = NULL;
    str->len = 0;
    str->alloc = 0;
    
    if (init) {
        size_t len = strlen(init);
        string_ensure_capacity(str, len + 1);
        if (str->str) {
            memcpy(str->str, init, len + 1);
            str->len = len;
        }
    } else {
        string_ensure_capacity(str, 1);
        if (str->str) {
            str->str[0] = '\0';
        }
    }
    
    return str;
}

/**
 * Create a new dynamic string with initial content of a specific length.
 * Useful for creating strings from non-null-terminated data.
 * 
 * @param init  Initial content (can be NULL, in which case buffer is uninitialized)
 * @param len   Length of the initial content
 * @return      A new string object, or NULL on allocation failure
 */
string_t *string_new_len(const char *init, size_t len)
{
    string_t *str = malloc(sizeof(string_t));
    if (!str) {
        return NULL;
    }
    
    str->str = NULL;
    str->len = 0;
    str->alloc = 0;
    
    string_ensure_capacity(str, len + 1);
    if (str->str) {
        if (init) {
            memcpy(str->str, init, len);
        }
        str->str[len] = '\0';
        str->len = len;
    }
    
    return str;
}

/**
 * Free a dynamic string object.
 * 
 * @param str           The string to free
 * @param free_segment  If true, also free the char* buffer; if false, return it
 * @return              The char* buffer if free_segment is false, otherwise NULL
 */
char *string_free(string_t *str, bool free_segment)
{
    if (!str) {
        return NULL;
    }
    
    char *result = NULL;
    if (!free_segment) {
        result = str->str;
    } else {
        free(str->str);
    }
    
    free(str);
    return result;
}

/**
 * Append a null-terminated C string to a dynamic string.
 * 
 * @param str  The string to append to
 * @param val  The value to append
 * @return     The string (for chaining)
 */
string_t *string_append(string_t *str, const char *val)
{
    if (!str || !val) {
        return str;
    }
    
    size_t val_len = strlen(val);
    string_ensure_capacity(str, str->len + val_len + 1);
    
    if (str->str) {
        memcpy(str->str + str->len, val, val_len + 1);
        str->len += val_len;
    }
    
    return str;
}

/**
 * Append a specific number of bytes to a dynamic string.
 * 
 * @param str  The string to append to
 * @param val  The bytes to append
 * @param len  The number of bytes to append
 * @return     The string (for chaining)
 */
string_t *string_append_len(string_t *str, const char *val, size_t len)
{
    if (!str || !val) {
        return str;
    }
    
    string_ensure_capacity(str, str->len + len + 1);
    
    if (str->str) {
        memcpy(str->str + str->len, val, len);
        str->len += len;
        str->str[str->len] = '\0';
    }
    
    return str;
}

/**
 * Append a single character to a dynamic string.
 * 
 * @param str  The string to append to
 * @param c    The character to append
 * @return     The string (for chaining)
 */
string_t *string_append_c(string_t *str, char c)
{
    if (!str) {
        return str;
    }
    
    string_ensure_capacity(str, str->len + 2);
    
    if (str->str) {
        str->str[str->len++] = c;
        str->str[str->len] = '\0';
    }
    
    return str;
}

/**
 * Append formatted text to a dynamic string (printf-style).
 * 
 * @param str     The string to append to
 * @param format  Printf format string
 * @param ...     Format arguments
 * @return        The string (for chaining)
 */
string_t *string_append_printf(string_t *str, const char *format, ...)
{
    if (!str || !format) {
        return str;
    }
    
    va_list args, args_copy;
    va_start(args, format);
    va_copy(args_copy, args);
    
    /* Calculate needed size */
    int needed = vsnprintf(NULL, 0, format, args);
    va_end(args);
    
    if (needed < 0) {
        va_end(args_copy);
        return str;
    }
    
    string_ensure_capacity(str, str->len + needed + 1);
    
    if (str->str) {
        vsnprintf(str->str + str->len, needed + 1, format, args_copy);
        str->len += needed;
    }
    
    va_end(args_copy);
    return str;
}

/**
 * Truncate a dynamic string to zero length.
 * The allocated buffer is retained for reuse.
 * 
 * @param str  The string to truncate
 * @return     The string (for chaining)
 */
string_t *string_truncate(string_t *str)
{
    if (!str) {
        return str;
    }
    
    str->len = 0;
    if (str->str) {
        str->str[0] = '\0';
    }
    
    return str;
}

/* ========================================================================
 * String utilities implementation
 * ======================================================================== */

/**
 * Split a string by a delimiter into an array of strings.
 * 
 * @param str         The string to split
 * @param delim       The delimiter string
 * @param max_tokens  Maximum number of tokens (<=0 for unlimited)
 * @return            NULL-terminated array of strings, or NULL on failure
 *                    Caller must free with str_freev()
 */
char **str_split(const char *str, const char *delim, int max_tokens)
{
    if (!str || !delim) {
        return NULL;
    }
    
    size_t delim_len = strlen(delim);
    if (delim_len == 0) {
        return NULL;
    }
    
    /* Count tokens */
    int count = 1;
    const char *p = str;
    while ((p = strstr(p, delim)) != NULL) {
        count++;
        p += delim_len;
        if (max_tokens > 0 && count >= max_tokens) {
            break;
        }
    }
    
    /* Allocate array */
    char **result = malloc(sizeof(char*) * (count + 1));
    if (!result) {
        return NULL;
    }
    
    /* Split string */
    p = str;
    for (int i = 0; i < count; i++) {
        const char *next = strstr(p, delim);
        
        if (!next || (max_tokens > 0 && i == count - 1)) {
            result[i] = strdup(p);
        } else {
            result[i] = strndup(p, next - p);
            p = next + delim_len;
        }
        
        if (!result[i]) {
            /* Allocation failed, clean up */
            for (int j = 0; j < i; j++) free(result[j]);
            free(result);
            return NULL;
        }
    }
    result[count] = NULL;
    
    return result;
}

/**
 * Free a NULL-terminated string array created by str_split().
 * 
 * @param str_array  The array to free
 */
void str_freev(char **str_array)
{
    if (!str_array) {
        return;
    }
    
    for (char **p = str_array; *p; p++) {
        free(*p);
    }
    free(str_array);
}

/**
 * Concatenate multiple strings into one.
 * 
 * @param first  The first string
 * @param ...    Additional strings, terminated by NULL
 * @return       A newly allocated concatenated string, or NULL on failure
 *               Caller must free the result
 */
char *str_concat(const char *first, ...)
{
    if (!first) {
        return NULL;
    }
    
    va_list args;
    
    /* First pass: calculate total length */
    size_t len = strlen(first);
    va_start(args, first);
    const char *s;
    while ((s = va_arg(args, const char*)) != NULL) {
        len += strlen(s);
    }
    va_end(args);
    
    /* Allocate result */
    char *result = malloc(len + 1);
    if (!result) {
        return NULL;
    }
    
    /* Second pass: copy strings */
    char *p = result;
    size_t first_len = strlen(first);
    memcpy(p, first, first_len);
    p += first_len;
    
    va_start(args, first);
    while ((s = va_arg(args, const char*)) != NULL) {
        size_t s_len = strlen(s);
        memcpy(p, s, s_len);
        p += s_len;
    }
    va_end(args);
    
    *p = '\0';
    return result;
}

/**
 * Strip leading and trailing whitespace from a string.
 * Modifies the string in place.
 * 
 * @param str  The string to strip
 * @return     Pointer to the first non-whitespace character
 */
char *str_strip(char *str)
{
    if (!str) {
        return NULL;
    }
    
    /* Skip leading whitespace */
    while (isspace((unsigned char)*str)) {
        str++;
    }
    
    if (*str == '\0') {
        return str;
    }
    
    /* Remove trailing whitespace */
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        end--;
    }
    *(end + 1) = '\0';
    
    return str;
}

/**
 * Replace all occurrences of certain characters with a new character.
 * Modifies the string in place.
 * 
 * @param str         The string to modify
 * @param delimiters  Characters to replace
 * @param new_char    The replacement character
 * @return            The modified string
 */
char *str_delimit(char *str, const char *delimiters, char new_char)
{
    if (!str || !delimiters) {
        return str;
    }
    
    for (char *p = str; *p; p++) {
        if (strchr(delimiters, *p)) {
            *p = new_char;
        }
    }
    
    return str;
}

/**
 * Check if a string ends with a given suffix.
 * 
 * @param str     The string to check
 * @param suffix  The suffix to look for
 * @return        true if str ends with suffix, false otherwise
 */
bool str_has_suffix(const char *str, const char *suffix)
{
    if (!str || !suffix) {
        return false;
    }
    
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    
    if (suffix_len > str_len) {
        return false;
    }
    
    return strcmp(str + str_len - suffix_len, suffix) == 0;
}

/**
 * Check if a string starts with a given prefix.
 * 
 * @param str     The string to check
 * @param prefix  The prefix to look for
 * @return        true if str starts with prefix, false otherwise
 */
bool str_has_prefix(const char *str, const char *prefix)
{
    if (!str || !prefix) {
        return false;
    }
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

/**
 * Duplicate a string (wrapper for strdup with NULL safety).
 * 
 * @param str  The string to duplicate
 * @return     A newly allocated copy, or NULL if str is NULL
 */
char *str_dup(const char *str)
{
    if (!str) {
        return NULL;
    }
    return strdup(str);
}

/**
 * Duplicate up to n characters of a string.
 * 
 * @param str  The string to duplicate
 * @param n    Maximum number of characters to copy
 * @return     A newly allocated copy, or NULL if str is NULL
 */
char *str_ndup(const char *str, size_t n)
{
    if (!str) {
        return NULL;
    }
    return strndup(str, n);
}

/**
 * Convert a string to lowercase.
 * 
 * @param str  The string to convert
 * @return     A newly allocated lowercase copy, or NULL on failure
 */
char *str_down(const char *str)
{
    if (!str) {
        return NULL;
    }
    
    char *result = strdup(str);
    if (!result) {
        return NULL;
    }
    
    for (char *p = result; *p; p++) {
        *p = tolower((unsigned char)*p);
    }
    
    return result;
}

/**
 * Convert a string to uppercase.
 * 
 * @param str  The string to convert
 * @return     A newly allocated uppercase copy, or NULL on failure
 */
char *str_up(const char *str)
{
    if (!str) {
        return NULL;
    }
    
    char *result = strdup(str);
    if (!result) {
        return NULL;
    }
    
    for (char *p = result; *p; p++) {
        *p = toupper((unsigned char)*p);
    }
    
    return result;
}

/* ========================================================================
 * File utilities implementation
 * ======================================================================== */

/**
 * Check if a file exists at the given path.
 * 
 * @param path  The path to check
 * @return      true if the file exists, false otherwise
 */
bool file_exists(const char *path)
{
    if (!path) {
        return false;
    }
    return access(path, F_OK) == 0;
}

/**
 * Check if a path is a directory.
 * 
 * @param path  The path to check
 * @return      true if path is a directory, false otherwise
 */
bool file_is_directory(const char *path)
{
    if (!path) {
        return false;
    }
    
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    return S_ISDIR(st.st_mode);
}

/**
 * Check if a file is executable.
 * 
 * @param path  The path to check
 * @return      true if the file is executable, false otherwise
 */
bool file_is_executable(const char *path)
{
    if (!path) {
        return false;
    }
    return access(path, X_OK) == 0;
}

/**
 * Check if a path is a regular file.
 * 
 * @param path  The path to check
 * @return      true if path is a regular file, false otherwise
 */
bool file_is_regular(const char *path)
{
    if (!path) {
        return false;
    }
    
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    return S_ISREG(st.st_mode);
}

/**
 * Get the current working directory.
 * 
 * @return  A newly allocated string with the current directory,
 *          or NULL on failure. Caller must free the result.
 */
char *get_current_dir(void)
{
    char *buf = NULL;
    size_t size = 256;
    
    while (1) {
        buf = realloc(buf, size);
        if (!buf) {
            return NULL;
        }
        
        if (getcwd(buf, size) != NULL) {
            return buf;
        }
        
        if (errno != ERANGE) {
            free(buf);
            return NULL;
        }
        
        size *= 2;
    }
}

/**
 * Read the entire contents of a file into memory.
 * 
 * @param filename  The file to read
 * @param length    If not NULL, receives the file length
 * @return          A newly allocated buffer with the file contents,
 *                  null-terminated. Caller must free. NULL on failure.
 */
char *file_get_contents(const char *filename, size_t *length)
{
    if (!filename) {
        return NULL;
    }
    
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        return NULL;
    }
    
    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        return NULL;
    }
    
    size_t size = st.st_size;
    char *buf = malloc(size + 1);
    if (!buf) {
        close(fd);
        return NULL;
    }
    
    size_t total = 0;
    while (total < size) {
        ssize_t n = read(fd, buf + total, size - total);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            free(buf);
            close(fd);
            return NULL;
        }
        if (n == 0) {
            break;
        }
        total += n;
    }
    
    close(fd);
    buf[total] = '\0';
    
    if (length) {
        *length = total;
    }
    return buf;
}

/* ========================================================================
 * Process spawning implementation
 * ======================================================================== */

/**
 * Merge custom environment variables with the current process environment.
 * Custom variables override existing ones with the same name.
 * 
 * @param custom_envp  NULL-terminated array of "NAME=VALUE" strings (can be NULL)
 * @return             A newly allocated merged environment array.
 *                     Caller must free with str_freev().
 */
static char **merge_environment(char **custom_envp)
{
    extern char **environ;
    
    /* Count existing environment variables */
    size_t env_count = 0;
    for (char **e = environ; e && *e; e++) {
        env_count++;
    }
    
    /* Count custom environment variables */
    size_t custom_count = 0;
    if (custom_envp) {
        for (char **e = custom_envp; *e; e++) {
            custom_count++;
        }
    }
    
    /* Build a hashtable of custom variable names for quick lookup */
    hashtable_t *custom_names = hashtable_new();
    if (custom_envp) {
        for (size_t i = 0; custom_envp[i]; i++) {
            char *eq = strchr(custom_envp[i], '=');
            if (eq) {
                char *name = strndup(custom_envp[i], eq - custom_envp[i]);
                hashtable_insert(custom_names, name, (void *)1);
                free(name);
            }
        }
    }
    
    /* Count how many existing vars are NOT overridden by custom vars */
    size_t keep_count = 0;
    for (size_t i = 0; i < env_count; i++) {
        char *eq = strchr(environ[i], '=');
        if (eq) {
            char *name = strndup(environ[i], eq - environ[i]);
            if (!hashtable_contains(custom_names, name)) {
                keep_count++;
            }
            free(name);
        }
    }
    
    hashtable_free(custom_names);
    
    /* Allocate merged array: kept existing + custom + NULL terminator */
    char **merged = malloc(sizeof(char *) * (keep_count + custom_count + 1));
    if (!merged) {
        return NULL;
    }
    
    size_t idx = 0;
    
    /* Copy existing environment variables that aren't overridden */
    custom_names = hashtable_new();
    if (custom_envp) {
        for (size_t i = 0; custom_envp[i]; i++) {
            char *eq = strchr(custom_envp[i], '=');
            if (eq) {
                char *name = strndup(custom_envp[i], eq - custom_envp[i]);
                hashtable_insert(custom_names, name, (void *)1);
                free(name);
            }
        }
    }
    
    for (size_t i = 0; i < env_count; i++) {
        char *eq = strchr(environ[i], '=');
        if (eq) {
            char *name = strndup(environ[i], eq - environ[i]);
            if (!hashtable_contains(custom_names, name)) {
                merged[idx++] = strdup(environ[i]);
            }
            free(name);
        }
    }
    
    hashtable_free(custom_names);
    
    /* Copy custom environment variables */
    if (custom_envp) {
        for (size_t i = 0; custom_envp[i]; i++) {
            merged[idx++] = strdup(custom_envp[i]);
        }
    }
    
    merged[idx] = NULL;
    return merged;
}

/**
 * Read all data from a file descriptor into a dynamically allocated string.
 * 
 * @param fd  The file descriptor to read from
 * @return    A newly allocated string with the data, or NULL on failure
 */
static char *read_all_fd(int fd)
{
    string_t *buf = string_new(NULL);
    if (!buf) {
        return NULL;
    }
    
    char tmp[4096];
    ssize_t n;
    
    while ((n = read(fd, tmp, sizeof(tmp))) > 0) {
        string_append_len(buf, tmp, n);
    }
    
    return string_free(buf, false);
}

/**
 * Spawn a child process synchronously and wait for it to complete.
 * Optionally captures stdout and stderr.
 * 
 * @param working_dir  Working directory for the child (NULL for current)
 * @param argv         NULL-terminated argument array (argv[0] is the program)
 * @param envp         NULL-terminated environment array (NULL to inherit)
 * @param result       If not NULL, receives exit status and captured output
 * @return             true on success (process was spawned), false on failure
 */
/* Profiling stats */
static double total_spawn_time = 0;
static int total_spawns = 0;
static int spawn_profile_enabled = -1;

static double spawn_get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

__attribute__((destructor))
static void print_spawn_stats(void) {
    if (spawn_profile_enabled == 1 && total_spawns > 0) {
        fprintf(stderr, "\n=== Command Execution Profiling ===\n");
        fprintf(stderr, "  Total commands spawned: %d\n", total_spawns);
        fprintf(stderr, "  Total spawn time: %.2f ms\n", total_spawn_time);
        fprintf(stderr, "  Average per spawn: %.2f ms\n", total_spawn_time / total_spawns);
    }
}

bool spawn_sync(const char *working_dir,
                char **argv,
                char **envp,
                spawn_result_t *result)
{
    double start_time = 0;
    if (spawn_profile_enabled < 0) {
        spawn_profile_enabled = (getenv("GANTT_PROFILE") != NULL) ? 1 : 0;
    }
    if (spawn_profile_enabled) {
        start_time = spawn_get_time_ms();
        total_spawns++;
    }
    
    if (!argv || !argv[0]) {
        return false;
    }
    
    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    
    if (result) {
        result->exit_status = -1;
        result->stdout_data = NULL;
        result->stderr_data = NULL;
        
        if (pipe(stdout_pipe) < 0) {
            return false;
        }
        if (pipe(stderr_pipe) < 0) {
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
            return false;
        }
    }
    
    pid_t pid = fork();
    if (pid < 0) {
        if (result) {
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
            close(stderr_pipe[0]);
            close(stderr_pipe[1]);
        }
        return false;
    }
    
    if (pid == 0) {
        /* Child process */
        if (result) {
            close(stdout_pipe[0]);
            close(stderr_pipe[0]);
            dup2(stdout_pipe[1], STDOUT_FILENO);
            dup2(stderr_pipe[1], STDERR_FILENO);
            close(stdout_pipe[1]);
            close(stderr_pipe[1]);
        }
        
        if (working_dir && chdir(working_dir) < 0) {
            _exit(127);
        }
        
        if (envp) {
            /* Merge custom env with current environment */
            char **merged_env = merge_environment(envp);
            if (merged_env) {
                execve(argv[0], argv, merged_env);
                /* If execve fails, we still exit below */
            }
        } else {
            execvp(argv[0], argv);
        }
        
        _exit(127);
    }
    
    /* Parent process */
    if (result) {
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        
        result->stdout_data = read_all_fd(stdout_pipe[0]);
        result->stderr_data = read_all_fd(stderr_pipe[0]);
        
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
    }
    
    int status;
    waitpid(pid, &status, 0);
    
    if (result) {
        if (WIFEXITED(status)) {
            result->exit_status = WEXITSTATUS(status);
        } else {
            result->exit_status = -1;
        }
    }
    
    if (spawn_profile_enabled) {
        total_spawn_time += spawn_get_time_ms() - start_time;
    }
    
    return true;
}

/**
 * Spawn a child process asynchronously (fire and forget).
 * 
 * @param working_dir  Working directory for the child (NULL for current)
 * @param argv         NULL-terminated argument array (argv[0] is the program)
 * @param envp         NULL-terminated environment array (NULL to inherit)
 * @param stdin_fd     File descriptor for child's stdin (-1 to inherit)
 * @param stdout_fd    File descriptor for child's stdout (-1 to inherit)
 * @param stderr_fd    File descriptor for child's stderr (-1 to inherit)
 * @return             The child process ID, or -1 on failure
 */
pid_t spawn_async(const char *working_dir,
                  char **argv,
                  char **envp,
                  int stdin_fd,
                  int stdout_fd,
                  int stderr_fd)
{
    if (!argv || !argv[0]) {
        return -1;
    }
    
    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }
    
    if (pid == 0) {
        /* Child process */
        if (stdin_fd >= 0) {
            dup2(stdin_fd, STDIN_FILENO);
            close(stdin_fd);
        }
        if (stdout_fd >= 0) {
            dup2(stdout_fd, STDOUT_FILENO);
            close(stdout_fd);
        }
        if (stderr_fd >= 0) {
            dup2(stderr_fd, STDERR_FILENO);
            close(stderr_fd);
        }
        
        if (working_dir && chdir(working_dir) < 0) {
            _exit(127);
        }
        
        if (envp) {
            /* Merge custom env with current environment */
            char **merged_env = merge_environment(envp);
            if (merged_env) {
                execve(argv[0], argv, merged_env);
                /* If execve fails, we still exit below */
            }
        } else {
            execvp(argv[0], argv);
        }
        
        _exit(127);
    }
    
    return pid;
}

/**
 * Free the data in a spawn result structure.
 * Does not free the structure itself.
 * 
 * @param result  The result structure to clean up
 */
void spawn_result_free(spawn_result_t *result)
{
    if (!result) {
        return;
    }
    free(result->stdout_data);
    free(result->stderr_data);
    result->stdout_data = NULL;
    result->stderr_data = NULL;
}

/**
 * Extended synchronous spawn with stdin support.
 * Sends data to child's stdin before waiting for completion.
 */
bool spawn_sync_with_input(const char *working_dir,
                           char **argv,
                           char **envp,
                           const char *stdin_data,
                           size_t stdin_len,
                           spawn_result_t *result)
{
    if (!argv || !argv[0]) {
        return false;
    }
    
    /* If no stdin data, just use regular spawn_sync */
    if (!stdin_data || stdin_len == 0) {
        return spawn_sync(working_dir, argv, envp, result);
    }
    
    int stdin_pipe[2] = {-1, -1};
    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    
    if (result) {
        result->exit_status = -1;
        result->stdout_data = NULL;
        result->stderr_data = NULL;
    }
    
    /* Create pipes */
    if (pipe(stdin_pipe) < 0) {
        return false;
    }
    
    if (result) {
        if (pipe(stdout_pipe) < 0) {
            close(stdin_pipe[0]);
            close(stdin_pipe[1]);
            return false;
        }
        if (pipe(stderr_pipe) < 0) {
            close(stdin_pipe[0]);
            close(stdin_pipe[1]);
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
            return false;
        }
    }
    
    pid_t pid = fork();
    if (pid < 0) {
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        if (result) {
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
            close(stderr_pipe[0]);
            close(stderr_pipe[1]);
        }
        return false;
    }
    
    if (pid == 0) {
        /* Child process */
        close(stdin_pipe[1]);  /* Close write end of stdin pipe */
        dup2(stdin_pipe[0], STDIN_FILENO);
        close(stdin_pipe[0]);
        
        if (result) {
            close(stdout_pipe[0]);
            close(stderr_pipe[0]);
            dup2(stdout_pipe[1], STDOUT_FILENO);
            dup2(stderr_pipe[1], STDERR_FILENO);
            close(stdout_pipe[1]);
            close(stderr_pipe[1]);
        }
        
        if (working_dir && chdir(working_dir) < 0) {
            _exit(127);
        }
        
        if (envp) {
            char **merged_env = merge_environment(envp);
            if (merged_env) {
                execve(argv[0], argv, merged_env);
            }
        } else {
            execvp(argv[0], argv);
        }
        
        _exit(127);
    }
    
    /* Parent process */
    close(stdin_pipe[0]);  /* Close read end of stdin pipe */
    
    /* Write stdin data to child */
    if (stdin_len == 0) {
        stdin_len = strlen(stdin_data);
    }
    ssize_t written = write(stdin_pipe[1], stdin_data, stdin_len);
    (void)written;  /* Ignore partial writes for simplicity */
    close(stdin_pipe[1]);  /* Signal EOF to child */
    
    if (result) {
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        
        /* Read stdout and stderr */
        string_t *stdout_buf = string_new(NULL);
        string_t *stderr_buf = string_new(NULL);
        char buf[4096];
        ssize_t n;
        
        /* Simple sequential read - for complex cases would need select() */
        while ((n = read(stdout_pipe[0], buf, sizeof(buf) - 1)) > 0) {
            buf[n] = '\0';
            string_append(stdout_buf, buf);
        }
        while ((n = read(stderr_pipe[0], buf, sizeof(buf) - 1)) > 0) {
            buf[n] = '\0';
            string_append(stderr_buf, buf);
        }
        
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        
        result->stdout_data = string_free(stdout_buf, false);
        result->stderr_data = string_free(stderr_buf, false);
    }
    
    /* Wait for child */
    int status;
    waitpid(pid, &status, 0);
    
    if (result) {
        if (WIFEXITED(status)) {
            result->exit_status = WEXITSTATUS(status);
        } else {
            result->exit_status = -1;
        }
    }
    
    return true;
}

/* ========================================================================
 * JAR/ZIP archive utilities implementation
 * ======================================================================== */

/* Track whether we've warned about missing unzip */
static int unzip_checked = 0;      /* 0 = not checked, 1 = available, -1 = missing */

/**
 * Check if unzip command is available.
 * Warns once if not found.
 * 
 * @return true if unzip is available, false otherwise
 */
static bool check_unzip_available(void)
{
    if (unzip_checked == 0) {
        /* First check - try running unzip --help */
        char *argv[] = {"unzip", "-v", NULL};
        spawn_result_t result = {0};
        
        if (spawn_sync(NULL, argv, NULL, &result) && result.exit_status == 0) {
            unzip_checked = 1;
        } else {
            unzip_checked = -1;
            fprintf(stderr, "Warning: 'unzip' command not found. "
                    "JAR/ZIP archive searching will be skipped.\n");
            fprintf(stderr, "         Install unzip to enable searching classes/resources in JAR files.\n");
        }
        spawn_result_free(&result);
    }
    return unzip_checked == 1;
}

/**
 * Check if a JAR/ZIP archive contains a specific entry.
 * Uses 'unzip -Z1' to list archive contents.
 * 
 * @param archive_path Path to the JAR/ZIP file
 * @param entry_path   Path within the archive (e.g., "com/example/Foo.class")
 * @return true if entry exists, false otherwise (or if unzip unavailable)
 */
bool archive_contains_entry(const char *archive_path, const char *entry_path)
{
    char *argv[5];
    spawn_result_t result = {0};
    bool found = false;
    
    if (!archive_path || !entry_path) {
        return false;
    }
    if (!file_exists(archive_path)) {
        return false;
    }
    if (!check_unzip_available()) {
        return false;
    }
    
    /* Use unzip -Z1 to list filenames only, one per line */
    argv[0] = "unzip";
    argv[1] = "-Z1";
    argv[2] = (char *)archive_path;
    argv[3] = NULL;
    
    if (spawn_sync(NULL, argv, NULL, &result) && result.exit_status == 0) {
        if (result.stdout_data) {
            /* Search for exact match in the file listing */
            char **lines = str_split(result.stdout_data, "\n", -1);
            for (int i = 0; lines && lines[i]; i++) {
                char *line = str_strip(lines[i]);
                if (line && strcmp(line, entry_path) == 0) {
                    found = true;
                    break;
                }
            }
            str_freev(lines);
        }
    }
    
    spawn_result_free(&result);
    return found;
}

/**
 * Extract content of a file from a JAR/ZIP archive.
 * Uses 'unzip -p' to extract to stdout without creating temp files.
 * 
 * @param archive_path Path to the JAR/ZIP file
 * @param entry_path   Path within the archive
 * @param contents     Output: pointer to extracted content (caller must free)
 * @param length       Output: length of extracted content
 * @return true on success, false if entry not found or extraction failed
 */
/**
 * Extract content of a file from a JAR/ZIP archive.
 * Uses 'unzip -p' to extract to stdout without creating temp files.
 * 
 * @param archive_path Path to the JAR/ZIP file
 * @param entry_path   Path within the archive
 * @param contents     Output: pointer to extracted content (caller must free)
 * @param length       Output: length of extracted content
 * @return true on success, false if entry not found, extraction failed, or unzip unavailable
 */
bool archive_extract_entry(const char *archive_path, const char *entry_path,
                           char **contents, size_t *length)
{
    char *argv[5];
    spawn_result_t result = {0};
    
    if (!archive_path || !entry_path || !contents) {
        return false;
    }
    if (!file_exists(archive_path)) {
        return false;
    }
    if (!check_unzip_available()) {
        return false;
    }
    
    *contents = NULL;
    if (length) {
        *length = 0;
    }
    
    /* Use unzip -p to extract to stdout */
    argv[0] = "unzip";
    argv[1] = "-p";
    argv[2] = (char *)archive_path;
    argv[3] = (char *)entry_path;
    argv[4] = NULL;
    
    if (spawn_sync(NULL, argv, NULL, &result) && result.exit_status == 0) {
        if (result.stdout_data) {
            *contents = result.stdout_data;
            result.stdout_data = NULL;  /* Transfer ownership to caller */
            if (length) {
                *length = strlen(*contents);
            }
            spawn_result_free(&result);
            return true;
        }
    }
    
    spawn_result_free(&result);
    return false;
}

/* ========================================================================
 * HTTP/URL utilities implementation
 * ======================================================================== */

/* Track which HTTP client is available: 0=unchecked, 1=curl, 2=wget, -1=none */
static int http_client_checked = 0;

/**
 * Check which HTTP client (curl or wget) is available.
 * Warns once if neither is found.
 * 
 * @return 1 for curl, 2 for wget, -1 if none available
 */
int check_http_client_available(void)
{
    if (http_client_checked == 0) {
        spawn_result_t result = {0};
        
        /* Try curl first */
        char *curl_argv[] = {"curl", "--version", NULL};
        if (spawn_sync(NULL, curl_argv, NULL, &result) && result.exit_status == 0) {
            http_client_checked = 1;
            spawn_result_free(&result);
            return http_client_checked;
        }
        spawn_result_free(&result);
        
        /* Try wget */
        char *wget_argv[] = {"wget", "--version", NULL};
        if (spawn_sync(NULL, wget_argv, NULL, &result) && result.exit_status == 0) {
            http_client_checked = 2;
            spawn_result_free(&result);
            return http_client_checked;
        }
        spawn_result_free(&result);
        
        /* Neither found */
        http_client_checked = -1;
        fprintf(stderr, "Warning: Neither 'curl' nor 'wget' found. "
                "URL fetching will be unavailable.\n");
        fprintf(stderr, "         Install curl or wget to enable loading properties from URLs.\n");
    }
    return http_client_checked;
}

/**
 * Fetch content from a URL.
 * Tries 'curl' first, then 'wget'. Warns once if neither is available.
 * 
 * @param url      The URL to fetch
 * @param contents Output: pointer to fetched content (caller must free)
 * @param length   Output: length of fetched content (can be NULL)
 * @return true on success, false on failure or if no HTTP client available
 */
bool url_fetch(const char *url, char **contents, size_t *length)
{
    spawn_result_t result = {0};
    int client;
    
    if (!url || !contents) {
        return false;
    }
    
    *contents = NULL;
    if (length) {
        *length = 0;
    }
    
    client = check_http_client_available();
    if (client < 0) {
        return false;
    }
    
    if (client == 1) {
        /* Use curl: -s for silent, -f for fail on HTTP errors, -L to follow redirects */
        char *argv[] = {"curl", "-sfL", (char *)url, NULL};
        
        if (spawn_sync(NULL, argv, NULL, &result) && result.exit_status == 0) {
            if (result.stdout_data) {
                *contents = result.stdout_data;
                result.stdout_data = NULL;
                if (length) {
                    *length = strlen(*contents);
                }
                spawn_result_free(&result);
                return true;
            }
        }
        spawn_result_free(&result);
    } else if (client == 2) {
        /* Use wget: -q for quiet, -O - to output to stdout */
        char *argv[] = {"wget", "-qO-", (char *)url, NULL};
        
        if (spawn_sync(NULL, argv, NULL, &result) && result.exit_status == 0) {
            if (result.stdout_data) {
                *contents = result.stdout_data;
                result.stdout_data = NULL;
                if (length) {
                    *length = strlen(*contents);
                }
                spawn_result_free(&result);
                return true;
            }
        }
        spawn_result_free(&result);
    }
    
    return false;
}

/* ========================================================================
 * OS detection utilities implementation
 * ======================================================================== */

/* Cached OS name from uname */
static char *cached_os_name = NULL;

/**
 * Get the operating system name using uname -s.
 * Result is cached after first call.
 */
const char *get_os_name(void)
{
    if (!cached_os_name) {
        char *argv[] = {"uname", "-s", NULL};
        spawn_result_t result = {0};
        
        if (spawn_sync(NULL, argv, NULL, &result) && 
            result.exit_status == 0 && result.stdout_data) {
            /* Remove trailing newline */
            cached_os_name = strdup(str_strip(result.stdout_data));
        } else {
            cached_os_name = strdup("unknown");
        }
        spawn_result_free(&result);
    }
    return cached_os_name;
}

/**
 * Get the OS family based on OS name.
 */
const char *get_os_family(void)
{
    const char *os = get_os_name();
    
    /* macOS / Mac OS X */
    if (strcasecmp(os, "Darwin") == 0) {
        return "mac";
    }
    
    /* Windows via Cygwin, MinGW, MSYS, or WSL */
    if (strncasecmp(os, "CYGWIN", 6) == 0 ||
        strncasecmp(os, "MINGW", 5) == 0 ||
        strncasecmp(os, "MSYS", 4) == 0) {
        return "windows";
    }
    
    /* Unix-like systems */
    if (strcasecmp(os, "Linux") == 0 ||
        strcasecmp(os, "Darwin") == 0 ||
        strcasecmp(os, "FreeBSD") == 0 ||
        strcasecmp(os, "OpenBSD") == 0 ||
        strcasecmp(os, "NetBSD") == 0 ||
        strcasecmp(os, "DragonFly") == 0 ||
        strcasecmp(os, "SunOS") == 0 ||
        strcasecmp(os, "AIX") == 0 ||
        strcasecmp(os, "HP-UX") == 0) {
        return "unix";
    }
    
    return "unknown";
}

/**
 * Check if current OS matches the given OS name(s).
 * Supports comma-separated values, case-insensitive.
 */
bool os_matches(const char *os_filter)
{
    if (!os_filter || !*os_filter) {
        return true;  /* No filter = match all */
    }
    
    const char *current_os = get_os_name();
    char *filter_copy = strdup(os_filter);
    char **parts = str_split(filter_copy, ",", -1);
    bool matched = false;
    
    for (int i = 0; parts && parts[i]; i++) {
        char *part = str_strip(parts[i]);
        if (part && strcasecmp(part, current_os) == 0) {
            matched = true;
            break;
        }
    }
    
    str_freev(parts);
    free(filter_copy);
    return matched;
}

/**
 * Check if current OS family matches the given family.
 * For "unix", also matches "mac" (macOS is Unix-like).
 */
bool os_family_matches(const char *family_filter)
{
    if (!family_filter || !*family_filter) {
        return true;
    }
    
    const char *current_family = get_os_family();
    
    /* Case-insensitive comparison */
    if (strcasecmp(family_filter, current_family) == 0) {
        return true;
    }
    
    /* Special case: "unix" also matches macOS */
    if (strcasecmp(family_filter, "unix") == 0 && 
        strcasecmp(current_family, "mac") == 0) {
        return true;
    }
    
    return false;
}

/* ========================================================================
 * Time utilities implementation
 * ======================================================================== */

/**
 * Get the current time in seconds since the Unix epoch.
 * 
 * @return  Current time in seconds
 */
long get_current_time_sec(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec;
}

/**
 * Get the current time with microsecond precision.
 * 
 * @param sec   If not NULL, receives the seconds component
 * @param usec  If not NULL, receives the microseconds component
 */
void get_current_time(long *sec, long *usec)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    if (sec) {
        *sec = tv.tv_sec;
    }
    if (usec) {
        *usec = tv.tv_usec;
    }
}

/* ========================================================================
 * Miscellaneous implementation
 * ======================================================================== */

/**
 * Parse a string as a boolean value.
 * Recognizes "true", "yes", "on", "1" as true and
 * "false", "no", "off", "0" as false (case-sensitive).
 * 
 * @param str            The string to parse
 * @param default_value  Value to return if string doesn't match any pattern
 * @return               The parsed boolean value
 */
bool parse_boolean(const char *str, bool default_value)
{
    if (!str) {
        return default_value;
    }
    
    if (strcmp(str, "true") == 0 ||
        strcmp(str, "yes") == 0 ||
        strcmp(str, "on") == 0 ||
        strcmp(str, "1") == 0) {
        return true;
    }
    
    if (strcmp(str, "false") == 0 ||
        strcmp(str, "no") == 0 ||
        strcmp(str, "off") == 0 ||
        strcmp(str, "0") == 0) {
        return false;
    }
    
    return default_value;
}
