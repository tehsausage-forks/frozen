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

static int b64idx(int c) {
  if (c < 26) {
    return c + 'A';
  } else if (c < 52) {
    return c - 26 + 'a';
  } else if (c < 62) {
    return c - 52 + '0';
  } else {
    return c == 62 ? '+' : '/';
  }
}

static int b64enc(struct json_out *out, const unsigned char *p, int n) {
  char buf[4];
  int i, len = 0;
  for (i = 0; i < n; i += 3) {
    int a = p[i], b = i + 1 < n ? p[i + 1] : 0, c = i + 2 < n ? p[i + 2] : 0;
    buf[0] = b64idx(a >> 2);
    buf[1] = b64idx((a & 3) << 4 | (b >> 4));
    buf[2] = b64idx((b & 15) << 2 | (c >> 6));
    buf[3] = b64idx(c & 63);
    if (i + 1 >= n) buf[2] = '=';
    if (i + 2 >= n) buf[3] = '=';
    len += out->printer(out, buf, sizeof(buf));
  }
  return len;
}

int json_vprintf(struct json_out *out, const char *fmt, va_list xap) {
  int len = 0;
  const char *quote = "\"", *null = "null";
  va_list ap;
  va_copy(ap, xap);

  while (*fmt != '\0') {
    if (strchr(":, \r\n\t[]{}\"", *fmt) != NULL) {
      len += out->printer(out, fmt, 1);
      fmt++;
    } else if (fmt[0] == '%') {
      char buf[21];
      size_t skip = 2;

      if (fmt[1] == 'M') {
        json_printf_callback_t f = va_arg(ap, json_printf_callback_t);
        len += f(out, &ap);
      } else if (fmt[1] == 'B') {
        int val = va_arg(ap, int);
        const char *str = val ? "true" : "false";
        len += out->printer(out, str, strlen(str));
      } else if (fmt[1] == 'H') {
        const char *hex = "0123456789abcdef";
        int i, n = va_arg(ap, int);
        const unsigned char *p = va_arg(ap, const unsigned char *);
        len += out->printer(out, quote, 1);
        for (i = 0; i < n; i++) {
          len += out->printer(out, &hex[(p[i] >> 4) & 0xf], 1);
          len += out->printer(out, &hex[p[i] & 0xf], 1);
        }
        len += out->printer(out, quote, 1);
      } else if (fmt[1] == 'V') {
        const unsigned char *p = va_arg(ap, const unsigned char *);
        int n = va_arg(ap, int);
        len += out->printer(out, quote, 1);
        len += b64enc(out, p, n);
        len += out->printer(out, quote, 1);
      } else if (fmt[1] == 'Q' ||
                 (fmt[1] == '.' && fmt[2] == '*' && fmt[3] == 'Q')) {
        size_t l = 0;
        const char *p;

        if (fmt[1] == '.') {
          l = (size_t) va_arg(ap, int);
          skip += 2;
        }
        p = va_arg(ap, char *);

        if (p == NULL) {
          len += out->printer(out, null, 4);
        } else {
          if (fmt[1] == 'Q') {
            l = strlen(p);
          }
          len += out->printer(out, quote, 1);
          len += json_escape(out, p, l);
          len += out->printer(out, quote, 1);
        }
      } else {
        /*
         * we delegate printing to the system printf.
         * The goal here is to delegate all modifiers parsing to the system
         * printf, as you can see below we still have to parse the format
         * types.
         *
         * Currently, %s with strings longer than 20 chars will require
         * double-buffering (an auxiliary buffer will be allocated from heap).
         * TODO(dfrank): reimplement %s and %.*s in order to avoid that.
         */

        const char *end_of_format_specifier = "sdfFgGlhuIcxz.*-0123456789";
        size_t n = strspn(fmt + 1, end_of_format_specifier);
        char *pbuf = buf;
        size_t need_len;
        char fmt2[20];
        va_list sub_ap;
        strncpy(fmt2, fmt, n + 1 > sizeof(fmt2) ? sizeof(fmt2) : n + 1);
        fmt2[n + 1] = '\0';

        va_copy(sub_ap, ap);
        need_len =
            vsnprintf(buf, sizeof(buf), fmt2, sub_ap) + 1 /* null-term */;
        /*
         * TODO(lsm): Fix windows & eCos code path here. Their vsnprintf
         * implementation returns -1 on overflow rather needed size.
         */
        if (need_len > sizeof(buf)) {
          /*
           * resulting string doesn't fit into a stack-allocated buffer `buf`,
           * so we need to allocate a new buffer from heap and use it
           */
          pbuf = (char *) malloc(need_len);
          va_copy(sub_ap, ap);
          vsnprintf(pbuf, need_len, fmt2, sub_ap);
        }

        /*
         * however we need to parse the type ourselves in order to advance
         * the va_list by the correct amount; there is no portable way to
         * inherit the advancement made by vprintf.
         * 32-bit (linux or windows) passes va_list by value.
         */
        if ((n + 1 == strlen("%" PRId64) && strcmp(fmt2, "%" PRId64) == 0) ||
            (n + 1 == strlen("%" PRIu64) && strcmp(fmt2, "%" PRIu64) == 0)) {
          (void) va_arg(ap, int64_t);
          skip += strlen(PRIu64) - 1;
        } else if (n + 1 == 3 && (strcmp(fmt2, "%zu") == 0)) {
            (void) va_arg(ap, size_t);
        } else if (strcmp(fmt2, "%.*s") == 0) {
          (void) va_arg(ap, int);
          (void) va_arg(ap, char *);
        } else {
          switch (fmt2[n]) {
            case 'u':
            case 'd':
              (void) va_arg(ap, int);
              break;
            case 'g':
            case 'f':
              (void) va_arg(ap, double);
              break;
            case 'p':
              (void) va_arg(ap, void *);
              break;
            default:
              /* many types are promoted to int */
              (void) va_arg(ap, int);
          }
        }

        len += out->printer(out, pbuf, strlen(pbuf));
        skip = n + 1;

        /* If buffer was allocated from heap, free it */
        if (pbuf != buf) {
          free(pbuf);
          pbuf = NULL;
        }
      }
      fmt += skip;
    } else if (*fmt == '_' || isalpha(*fmt)) {
      len += out->printer(out, quote, 1);
      while (*fmt == '_' || isalpha(*fmt) || is_digit(*fmt)) {
        len += out->printer(out, fmt, 1);
        fmt++;
      }
      len += out->printer(out, quote, 1);
    } else {
      len += out->printer(out, fmt, 1);
      fmt++;
    }
  }
  va_end(ap);

  return len;
}

int json_printf(struct json_out *out, const char *fmt, ...) {
  int n;
  va_list ap;
  va_start(ap, fmt);
  n = json_vprintf(out, fmt, ap);
  va_end(ap);
  return n;
}

int json_printf_array(struct json_out *out, va_list *ap) {
  int len = 0;
  char *arr = va_arg(*ap, char *);
  size_t i, arr_size = va_arg(*ap, size_t);
  size_t elem_size = va_arg(*ap, size_t);
  const char *fmt = va_arg(*ap, char *);
  len += json_printf(out, "[", 1);
  for (i = 0; arr != NULL && i < arr_size / elem_size; i++) {
    union {
      int64_t i;
      double d;
    } val;
    memcpy(&val, arr + i * elem_size,
           elem_size > sizeof(val) ? sizeof(val) : elem_size);
    if (i > 0) len += json_printf(out, ", ");
    if (strchr(fmt, 'f') != NULL) {
      len += json_printf(out, fmt, val.d);
    } else {
      len += json_printf(out, fmt, val.i);
    }
  }
  len += json_printf(out, "]", 1);
  return len;
}

int json_vfprintf(const char *file_name, const char *fmt, va_list ap) {
  int res = -1;
  FILE *fp = fopen(file_name, "wb");
  if (fp != NULL) {
    struct json_out out = JSON_OUT_FILE(fp);
    res = json_vprintf(&out, fmt, ap);
    fputc('\n', fp);
    fclose(fp);
  }
  return res;
}

int json_fprintf(const char *file_name, const char *fmt, ...) {
  int result;
  va_list ap;
  va_start(ap, fmt);
  result = json_vfprintf(file_name, fmt, ap);
  va_end(ap);
  return result;
}
