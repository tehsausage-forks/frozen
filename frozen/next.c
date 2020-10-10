/*
 * Copyright (c) 2004-2013 Sergey Lyubka <valenok@gmail.com>
 * Copyright (c) 2013 Cesanta Software Limited
 * All rights reserved
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "frozen.h"
#include <ctype.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"

struct next_data {
  void *handle;            // Passed handle. Changed if a next entry is found
  const char *path;        // Path to the iterated object/array
  int path_len;            // Path length - optimisation
  int found;               // Non-0 if found the next entry
  struct json_token *key;  // Object's key
  struct json_token *val;  // Object's value
  int *idx;                // Array index
};

static void next_set_key(struct next_data *d, const char *name, int name_len,
                         int is_array) {
  if (is_array) {
    /* Array. Set index and reset key  */
    if (d->key != NULL) {
      d->key->len = 0;
      d->key->ptr = NULL;
    }
    if (d->idx != NULL) *d->idx = atoi(name);
  } else {
    /* Object. Set key and make index -1 */
    if (d->key != NULL) {
      d->key->ptr = name;
      d->key->len = name_len;
    }
    if (d->idx != NULL) *d->idx = -1;
  }
}

static void next_cb(void *userdata, const char *name, size_t name_len,
                    const char *path, const struct json_token *t) {
  struct next_data *d = (struct next_data *) userdata;
  const char *p = path + d->path_len;
  if (d->found) return;
  if (d->path_len >= (int) strlen(path)) return;
  if (strncmp(d->path, path, d->path_len) != 0) return;
  if (strchr(p + 1, '.') != NULL) return; /* More nested objects - skip */
  if (strchr(p + 1, '[') != NULL) return; /* Ditto for arrays */
  // {OBJECT,ARRAY}_END types do not pass name, _START does. Save key.
  if (t->type == JSON_TYPE_OBJECT_START || t->type == JSON_TYPE_ARRAY_START) {
    // printf("SAV %s %d %p\n", path, t->type, t->ptr);
    next_set_key(d, name, name_len, p[0] == '[');
  } else if (d->handle == NULL || d->handle < (void *) t->ptr) {
    // printf("END %s %d %p\n", path, t->type, t->ptr);
    if (t->type != JSON_TYPE_OBJECT_END && t->type != JSON_TYPE_ARRAY_END) {
      next_set_key(d, name, name_len, p[0] == '[');
    }
    if (d->val != NULL) *d->val = *t;
    d->handle = (void *) t->ptr;
    d->found = 1;
  }
}

static void *json_next(const char *s, int len, void *handle, const char *path,
                       struct json_token *key, struct json_token *val, int *i) {
  struct json_token tmpval, *v = val == NULL ? &tmpval : val;
  struct json_token tmpkey, *k = key == NULL ? &tmpkey : key;
  int tmpidx, *pidx = i == NULL ? &tmpidx : i;
  struct next_data data = {handle, path, strlen(path), 0, k, v, pidx};
  json_walk(s, len, next_cb, &data);
  return data.found ? data.handle : NULL;
}

void *json_next_key(const char *s, int len, void *handle, const char *path,
                    struct json_token *key, struct json_token *val) {
  return json_next(s, len, handle, path, key, val, NULL);
}

void *json_next_elem(const char *s, int len, void *handle, const char *path,
                     int *idx, struct json_token *val) {
  return json_next(s, len, handle, path, NULL, val, idx);
}
