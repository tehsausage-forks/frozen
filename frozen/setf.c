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

struct json_setf_data {
  const char *json_path;
  const char *base; /* Pointer to the source JSON string */
  int matched;      /* Matched part of json_path */
  int pos;          /* Offset of the mutated value begin */
  int end;          /* Offset of the mutated value end */
  int prev;         /* Offset of the previous token end */
};

static int get_matched_prefix_len(const char *s1, const char *s2) {
  int i = 0;
  while (s1[i] && s2[i] && s1[i] == s2[i]) i++;
  return i;
}

static void json_vsetf_cb(void *userdata, const char *name, size_t name_len,
                          const char *path, const struct json_token *t) {
  struct json_setf_data *data = (struct json_setf_data *) userdata;
  int off, len = get_matched_prefix_len(path, data->json_path);
  if (t->ptr == NULL) return;
  off = t->ptr - data->base;
  // printf("--%d %s %d\n", t->type, path, off);
  if (len > data->matched) data->matched = len;

  /*
   * If there is no exact path match, set the mutation position to tbe end
   * of the object or array
   */
  if (len < data->matched && data->pos == 0 &&
      (t->type == JSON_TYPE_OBJECT_END || t->type == JSON_TYPE_ARRAY_END)) {
    data->pos = data->end = data->prev;
  }

  /* Exact path match. Set mutation position to the value of this token */
  if (strcmp(path, data->json_path) == 0 && t->type != JSON_TYPE_OBJECT_START &&
      t->type != JSON_TYPE_ARRAY_START) {
    data->pos = off;
    data->end = off + t->len;
  }

  /*
   * For deletion, we need to know where the previous value ends, because
   * we don't know where matched value key starts.
   * When the mutation position is not yet set, remember each value end.
   * When the mutation position is already set, but it is at the beginning
   * of the object/array, we catch the end of the object/array and see
   * whether the object/array start is closer then previously stored prev.
   */
  if (data->pos == 0) {
    data->prev = off + t->len; /* pos is not yet set */
  } else if ((t->ptr[0] == '[' || t->ptr[0] == '{') && off + 1 < data->pos &&
             off + 1 > data->prev) {
    data->prev = off + 1;
  }
  (void) name;
  (void) name_len;
}

int json_vsetf(const char *s, int len, struct json_out *out,
               const char *json_path, const char *json_fmt, va_list ap) {
  struct json_setf_data data;
  memset(&data, 0, sizeof(data));
  data.json_path = json_path;
  data.base = s;
  data.end = len;
  // printf("S:[%.*s] %s %p\n", len, s, json_path, json_fmt);
  json_walk(s, len, json_vsetf_cb, &data);
  // printf("-> %d %d %d\n", data.prev, data.pos, data.end);
  if (json_fmt == NULL) {
    /* Deletion codepath */
    json_printf(out, "%.*s", data.prev, s);
    /* Trim comma after the value that begins at object/array start */
    if (s[data.prev - 1] == '{' || s[data.prev - 1] == '[') {
      int i = data.end;
      while (i < len && is_space(s[i])) i++;
      if (s[i] == ',') data.end = i + 1; /* Point after comma */
    }
    json_printf(out, "%.*s", len - data.end, s + data.end);
  } else {
    /* Modification codepath */
    int n, off = data.matched, depth = 0;

    /* Print the unchanged beginning */
    json_printf(out, "%.*s", data.pos, s);

    /* Add missing keys */
    while ((n = strcspn(&json_path[off], ".[")) > 0) {
      if (s[data.prev - 1] != '{' && s[data.prev - 1] != '[' && depth == 0) {
        json_printf(out, ",");
      }
      if (off > 0 && json_path[off - 1] != '.') break;
      json_printf(out, "%.*Q:", 1, json_path + off);
      off += n;
      if (json_path[off] != '\0') {
        json_printf(out, "%c", json_path[off] == '.' ? '{' : '[');
        depth++;
        off++;
      }
    }
    /* Print the new value */
    json_vprintf(out, json_fmt, ap);

    /* Close brackets/braces of the added missing keys */
    for (; off > data.matched; off--) {
      int ch = json_path[off];
      const char *p = ch == '.' ? "}" : ch == '[' ? "]" : "";
      json_printf(out, "%s", p);
    }

    /* Print the rest of the unchanged string */
    json_printf(out, "%.*s", len - data.end, s + data.end);
  }
  return data.end > data.pos ? 1 : 0;
}

int json_setf(const char *s, int len, struct json_out *out,
              const char *json_path, const char *json_fmt, ...) {
  int result;
  va_list ap;
  va_start(ap, json_fmt);
  result = json_vsetf(s, len, out, json_path, json_fmt, ap);
  va_end(ap);
  return result;
}
