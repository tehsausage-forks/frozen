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
#include <ctype.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"

static int b64rev(int c) {
  if (c >= 'A' && c <= 'Z') {
    return c - 'A';
  } else if (c >= 'a' && c <= 'z') {
    return c + 26 - 'a';
  } else if (c >= '0' && c <= '9') {
    return c + 52 - '0';
  } else if (c == '+') {
    return 62;
  } else if (c == '/') {
    return 63;
  } else {
    return 64;
  }
}

static int b64dec(const char *src, int n, char *dst) {
  const char *end = src + n;
  int len = 0;
  while (src + 3 < end) {
    int a = b64rev(src[0]), b = b64rev(src[1]), c = b64rev(src[2]),
        d = b64rev(src[3]);
    dst[len++] = (a << 2) | (b >> 4);
    if (src[2] != '=') {
      dst[len++] = (b << 4) | (c >> 2);
      if (src[3] != '=') {
        dst[len++] = (c << 6) | d;
      }
    }
    src += 4;
  }
  return len;
}

static unsigned char hexdec(const char *s) {
#define HEXTOI(x) (x >= '0' && x <= '9' ? x - '0' : x - 'W')
  int a = tolower(*(const unsigned char *) s);
  int b = tolower(*(const unsigned char *) (s + 1));
  return (HEXTOI(a) << 4) | HEXTOI(b);
}

struct scan_array_info {
  int found;
  char path[JSON_MAX_PATH_LEN];
  struct json_token *token;
};

static void json_scanf_array_elem_cb(void *callback_data, const char *name,
                                     size_t name_len, const char *path,
                                     const struct json_token *token) {
  struct scan_array_info *info = (struct scan_array_info *) callback_data;

  (void) name;
  (void) name_len;

  if (strcmp(path, info->path) == 0) {
    *info->token = *token;
    info->found = 1;
  }
}

int json_scanf_array_elem(const char *s, int len, const char *path, int idx,
                          struct json_token *token) {
  struct scan_array_info info;
  info.token = token;
  info.found = 0;
  memset(token, 0, sizeof(*token));
  snprintf(info.path, sizeof(info.path), "%s[%d]", path, idx);
  json_walk(s, len, json_scanf_array_elem_cb, &info);
  return info.found ? token->len : -1;
}

struct json_scanf_info {
  int num_conversions;
  char *path;
  const char *fmt;
  void *target;
  void *user_data;
  int type;
};

static void json_scanf_cb(void *callback_data, const char *name,
                          size_t name_len, const char *path,
                          const struct json_token *token) {
  struct json_scanf_info *info = (struct json_scanf_info *) callback_data;
  char buf[32]; /* Must be enough to hold numbers */

  (void) name;
  (void) name_len;

  if (strcmp(path, info->path) != 0) {
    /* It's not the path we're looking for, so, just ignore this callback */
    return;
  }

  if (token->ptr == NULL) {
    /*
     * We're not interested here in the events for which we have no value;
     * namely, JSON_TYPE_OBJECT_START and JSON_TYPE_ARRAY_START
     */
    return;
  }

  switch (info->type) {
    case 'B':
      info->num_conversions++;
      switch (sizeof(bool)) {
        case sizeof(char):
          *(char *) info->target = (token->type == JSON_TYPE_TRUE ? 1 : 0);
          break;
        case sizeof(int):
          *(int *) info->target = (token->type == JSON_TYPE_TRUE ? 1 : 0);
          break;
        default:
          /* should never be here */
          abort();
      }
      break;
    case 'M': {
      union {
        void *p;
        json_scanner_t f;
      } u = {info->target};
      info->num_conversions++;
      u.f(token->ptr, token->len, info->user_data);
      break;
    }
    case 'Q': {
      char **dst = (char **) info->target;
      if (token->type == JSON_TYPE_NULL) {
        *dst = NULL;
      } else {
        int unescaped_len = json_unescape(token->ptr, token->len, NULL, 0);
        if (unescaped_len >= 0 &&
            (*dst = (char *) malloc(unescaped_len + 1)) != NULL) {
          info->num_conversions++;
          json_unescape(token->ptr, token->len, *dst, unescaped_len);
          (*dst)[unescaped_len] = '\0';
        }
      }
      break;
    }
    case 'H': {
      char **dst = (char **) info->user_data;
      int i, len = token->len / 2;
      *(int *) info->target = len;
      if ((*dst = (char *) malloc(len + 1)) != NULL) {
        for (i = 0; i < len; i++) {
          (*dst)[i] = hexdec(token->ptr + 2 * i);
        }
        (*dst)[len] = '\0';
        info->num_conversions++;
      }
      break;
    }
    case 'V': {
      char **dst = (char **) info->target;
      int len = token->len * 4 / 3 + 2;
      if ((*dst = (char *) malloc(len + 1)) != NULL) {
        int n = b64dec(token->ptr, token->len, *dst);
        (*dst)[n] = '\0';
        *(int *) info->user_data = n;
        info->num_conversions++;
      }
      break;
    }
    case 'T':
      info->num_conversions++;
      *(struct json_token *) info->target = *token;
      break;
    default:
      /* Before scanf, copy into tmp buffer in order to 0-terminate it */
      if (token->len < (int) sizeof(buf)) {
        memcpy(buf, token->ptr, token->len);
        buf[token->len] = '\0';
        info->num_conversions += sscanf(buf, info->fmt, info->target);
      }
      break;
  }
}

int json_vscanf(const char *s, int len, const char *fmt, va_list ap) {
  char path[JSON_MAX_PATH_LEN] = "", fmtbuf[20];
  int i = 0;
  char *p = NULL;
  struct json_scanf_info info = {0, path, fmtbuf, NULL, NULL, 0};

  while (fmt[i] != '\0') {
    if (fmt[i] == '{') {
      strcat(path, ".");
      i++;
    } else if (fmt[i] == '}') {
      if ((p = strrchr(path, '.')) != NULL) *p = '\0';
      i++;
    } else if (fmt[i] == '%') {
      info.target = va_arg(ap, void *);
      info.type = fmt[i + 1];
      switch (fmt[i + 1]) {
        case 'M':
        case 'V':
        case 'H':
          info.user_data = va_arg(ap, void *);
        /* FALLTHROUGH */
        case 'B':
        case 'Q':
        case 'T':
          i += 2;
          break;
        default: {
          const char *delims = ", \t\r\n]}";
          int conv_len = strcspn(fmt + i + 1, delims) + 1;
          snprintf(fmtbuf, sizeof(fmtbuf), "%.*s", conv_len, fmt + i);
          i += conv_len;
          i += strspn(fmt + i, delims);
          break;
        }
      }
      json_walk(s, len, json_scanf_cb, &info);
    } else if (is_alpha(fmt[i]) || get_utf8_char_len(fmt[i]) > 1) {
      const char *delims = ": \r\n\t";
      int key_len = strcspn(&fmt[i], delims);
      if ((p = strrchr(path, '.')) != NULL) p[1] = '\0';
      sprintf(path + strlen(path), "%.*s", key_len, &fmt[i]);
      i += key_len + strspn(fmt + i + key_len, delims);
    } else {
      i++;
    }
  }
  return info.num_conversions;
}

int json_scanf(const char *str, int len, const char *fmt, ...) {
  int result;
  va_list ap;
  va_start(ap, fmt);
  result = json_vscanf(str, len, fmt, ap);
  va_end(ap);
  return result;
}
