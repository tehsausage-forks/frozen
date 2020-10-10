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

#ifndef ELSA_UTIL_H_
#define ELSA_UTIL_H_

static int is_space(int ch) {
  return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static int is_alpha(int ch) {
  return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

static int is_digit(int ch) {
  return ch >= '0' && ch <= '9';
}

static int is_hex_digit(int ch) {
  return is_digit(ch) || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

/* Error codes */
#define JSON_STRING_INVALID -1
#define JSON_STRING_INCOMPLETE -2

static int get_escape_len(const char *s, int len) {
  switch (*s) {
    case 'u':
      return len < 6 ? JSON_STRING_INCOMPLETE
                     : is_hex_digit(s[1]) && is_hex_digit(s[2]) &&
                       is_hex_digit(s[3]) && is_hex_digit(s[4])
                         ? 5
                         : JSON_STRING_INVALID;
    case '"':
    case '\\':
    case '/':
    case 'b':
    case 'f':
    case 'n':
    case 'r':
    case 't':
      return len < 2 ? JSON_STRING_INCOMPLETE : 1;
    default:
      return JSON_STRING_INVALID;
  }
}

static int get_utf8_char_len(unsigned char ch) {
  if ((ch & 0x80) == 0) return 1;
  switch (ch & 0xf0) {
    case 0xf0:
      return 4;
    case 0xe0:
      return 3;
    default:
      return 2;
  }
}

#endif /* ELSA_UTIL_H_ */
