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

int json_printer_buf(struct json_out *out, const char *buf, size_t len) {
  size_t avail = out->u.buf.size - out->u.buf.len;
  size_t n = len < avail ? len : avail;
  memcpy(out->u.buf.buf + out->u.buf.len, buf, n);
  out->u.buf.len += n;
  if (out->u.buf.size > 0) {
    size_t idx = out->u.buf.len;
    if (idx >= out->u.buf.size) idx = out->u.buf.size - 1;
    out->u.buf.buf[idx] = '\0';
  }
  return len;
}

int json_printer_file(struct json_out *out, const char *buf, size_t len) {
  return fwrite(buf, 1, len, out->u.fp);
}
