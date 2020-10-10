/*
 * Copyright (c) 2004-2013 Sergey Lyubka <valenok@gmail.com>
 * Copyright (c) 2013 Cesanta Software Limited
 * Copyright (c) 2020 Julian Smythe <sausage@tehsausage.com>
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

#include "elsa.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct prettify_data {
  struct json_out *out;
  int level;
  int last_token;
};

static void indent(struct json_out *out, int level) {
  while (level-- > 0) out->printer(out, "  ", 2);
}

static void print_key(struct prettify_data *pd, const char *path,
                      const char *name, int name_len) {
  if (pd->last_token != JSON_TYPE_INVALID &&
      pd->last_token != JSON_TYPE_ARRAY_START &&
      pd->last_token != JSON_TYPE_OBJECT_START) {
    pd->out->printer(pd->out, ",", 1);
  }
  if (path[0] != '\0') pd->out->printer(pd->out, "\n", 1);
  indent(pd->out, pd->level);
  if (path[0] != '\0' && path[strlen(path) - 1] != ']') {
    pd->out->printer(pd->out, "\"", 1);
    pd->out->printer(pd->out, name, (int) name_len);
    pd->out->printer(pd->out, "\"", 1);
    pd->out->printer(pd->out, ": ", 2);
  }
}

static void prettify_cb(void *userdata, const char *name, size_t name_len,
                        const char *path, const struct json_token *t) {
  struct prettify_data *pd = (struct prettify_data *) userdata;
  switch (t->type) {
    case JSON_TYPE_OBJECT_START:
    case JSON_TYPE_ARRAY_START:
      print_key(pd, path, name, name_len);
      pd->out->printer(pd->out, t->type == JSON_TYPE_ARRAY_START ? "[" : "{",
                       1);
      pd->level++;
      break;
    case JSON_TYPE_OBJECT_END:
    case JSON_TYPE_ARRAY_END:
      pd->level--;
      if (pd->last_token != JSON_TYPE_INVALID &&
          pd->last_token != JSON_TYPE_ARRAY_START &&
          pd->last_token != JSON_TYPE_OBJECT_START) {
        pd->out->printer(pd->out, "\n", 1);
        indent(pd->out, pd->level);
      }
      pd->out->printer(pd->out, t->type == JSON_TYPE_ARRAY_END ? "]" : "}", 1);
      break;
    case JSON_TYPE_NUMBER:
    case JSON_TYPE_NULL:
    case JSON_TYPE_TRUE:
    case JSON_TYPE_FALSE:
    case JSON_TYPE_STRING:
      print_key(pd, path, name, name_len);
      if (t->type == JSON_TYPE_STRING) pd->out->printer(pd->out, "\"", 1);
      pd->out->printer(pd->out, t->ptr, t->len);
      if (t->type == JSON_TYPE_STRING) pd->out->printer(pd->out, "\"", 1);
      break;
    default:
      break;
  }
  pd->last_token = t->type;
}

int json_prettify(const char *s, int len, struct json_out *out) {
  struct prettify_data pd = {out, 0, JSON_TYPE_INVALID};
  return json_walk(s, len, prettify_cb, &pd);
}

int json_prettify_file(const char *file_name) {
  int res = -1;
  char *s = json_fread(file_name);
  FILE *fp;
  if (s != NULL && (fp = fopen(file_name, "w")) != NULL) {
    struct json_out out = JSON_OUT_FILE(fp);
    res = json_prettify(s, strlen(s), &out);
    if (res < 0) {
      /* On error, restore the old content */
      fclose(fp);
      fp = fopen(file_name, "w");
      fseek(fp, 0, SEEK_SET);
      fwrite(s, 1, strlen(s), fp);
    } else {
      fputc('\n', fp);
    }
    fclose(fp);
  }
  free(s);
  return res;
}
