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
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"

struct frozen {
  const char *end;
  const char *cur;

  const char *cur_name;
  size_t cur_name_len;

  /* For callback API */
  char path[JSON_MAX_PATH_LEN];
  size_t path_len;
  void *callback_data;
  json_walk_callback_t callback;
};

struct fstate {
  const char *ptr;
  size_t path_len;
};

#define SET_STATE(fr, ptr, str, len)              \
  struct fstate fstate = {(ptr), (fr)->path_len}; \
  append_to_path((fr), (str), (len));

#define CALL_BACK(fr, tok, value, len)                                        \
  do {                                                                        \
    if ((fr)->callback &&                                                     \
        ((fr)->path_len == 0 || (fr)->path[(fr)->path_len - 1] != '.')) {     \
      struct json_token t = {(value), (len), (tok)};                          \
                                                                              \
      /* Call the callback with the given value and current name */           \
      (fr)->callback((fr)->callback_data, (fr)->cur_name, (fr)->cur_name_len, \
                     (fr)->path, &t);                                         \
                                                                              \
      /* Reset the name */                                                    \
      (fr)->cur_name = NULL;                                                  \
      (fr)->cur_name_len = 0;                                                 \
    }                                                                         \
  } while (0)

static int append_to_path(struct frozen *f, const char *str, int size) {
  int n = f->path_len;
  int left = sizeof(f->path) - n - 1;
  if (size > left) size = left;
  memcpy(f->path + n, str, size);
  f->path[n + size] = '\0';
  f->path_len += size;
  return n;
}

static void truncate_path(struct frozen *f, size_t len) {
  f->path_len = len;
  f->path[len] = '\0';
}

static int parse_object(struct frozen *f);
static int parse_value(struct frozen *f);

#define EXPECT(cond, err_code)      \
  do {                              \
    if (!(cond)) return (err_code); \
  } while (0)

#define TRY(expr)          \
  do {                     \
    int _n = expr;         \
    if (_n < 0) return _n; \
  } while (0)

#define END_OF_STRING (-1)

static int left(const struct frozen *f) {
  return f->end - f->cur;
}

static void skip_whitespaces(struct frozen *f) {
  while (f->cur < f->end && is_space(*f->cur)) f->cur++;
}

static int cur(struct frozen *f) {
  skip_whitespaces(f);
  return f->cur >= f->end ? END_OF_STRING : *(unsigned char *) f->cur;
}

static int test_and_skip(struct frozen *f, int expected) {
  int ch = cur(f);
  if (ch == expected) {
    f->cur++;
    return 0;
  }
  return ch == END_OF_STRING ? JSON_STRING_INCOMPLETE : JSON_STRING_INVALID;
}

/* identifier = letter { letter | digit | '_' } */
static int parse_identifier(struct frozen *f) {
  EXPECT(is_alpha(cur(f)), JSON_STRING_INVALID);
  {
    SET_STATE(f, f->cur, "", 0);
    while (f->cur < f->end &&
           (*f->cur == '_' || is_alpha(*f->cur) || is_digit(*f->cur))) {
      f->cur++;
    }
    truncate_path(f, fstate.path_len);
    CALL_BACK(f, JSON_TYPE_STRING, fstate.ptr, f->cur - fstate.ptr);
  }
  return 0;
}

/* string = '"' { quoted_printable_chars } '"' */
static int parse_string(struct frozen *f) {
  int n, ch = 0, len = 0;
  TRY(test_and_skip(f, '"'));
  {
    SET_STATE(f, f->cur, "", 0);
    for (; f->cur < f->end; f->cur += len) {
      ch = *(unsigned char *) f->cur;
      len = get_utf8_char_len((unsigned char) ch);
      EXPECT(ch >= 32 && len > 0, JSON_STRING_INVALID); /* No control chars */
      EXPECT(len <= left(f), JSON_STRING_INCOMPLETE);
      if (ch == '\\') {
        EXPECT((n = get_escape_len(f->cur + 1, left(f))) > 0, n);
        len += n;
      } else if (ch == '"') {
        truncate_path(f, fstate.path_len);
        CALL_BACK(f, JSON_TYPE_STRING, fstate.ptr, f->cur - fstate.ptr);
        f->cur++;
        break;
      };
    }
  }
  return ch == '"' ? 0 : JSON_STRING_INCOMPLETE;
}

/* number = [ '-' ] digit+ [ '.' digit+ ] [ ['e'|'E'] ['+'|'-'] digit+ ] */
static int parse_number(struct frozen *f) {
  int ch = cur(f);
  SET_STATE(f, f->cur, "", 0);
  if (ch == '-') f->cur++;
  EXPECT(f->cur < f->end, JSON_STRING_INCOMPLETE);
  EXPECT(is_digit(f->cur[0]), JSON_STRING_INVALID);
  while (f->cur < f->end && is_digit(f->cur[0])) f->cur++;
  if (f->cur < f->end && f->cur[0] == '.') {
    f->cur++;
    EXPECT(f->cur < f->end, JSON_STRING_INCOMPLETE);
    EXPECT(is_digit(f->cur[0]), JSON_STRING_INVALID);
    while (f->cur < f->end && is_digit(f->cur[0])) f->cur++;
  }
  if (f->cur < f->end && (f->cur[0] == 'e' || f->cur[0] == 'E')) {
    f->cur++;
    EXPECT(f->cur < f->end, JSON_STRING_INCOMPLETE);
    if ((f->cur[0] == '+' || f->cur[0] == '-')) f->cur++;
    EXPECT(f->cur < f->end, JSON_STRING_INCOMPLETE);
    EXPECT(is_digit(f->cur[0]), JSON_STRING_INVALID);
    while (f->cur < f->end && is_digit(f->cur[0])) f->cur++;
  }
  truncate_path(f, fstate.path_len);
  CALL_BACK(f, JSON_TYPE_NUMBER, fstate.ptr, f->cur - fstate.ptr);
  return 0;
}

/* array = '[' [ value { ',' value } ] ']' */
static int parse_array(struct frozen *f) {
  int i = 0, current_path_len;
  char buf[20];
  CALL_BACK(f, JSON_TYPE_ARRAY_START, NULL, 0);
  TRY(test_and_skip(f, '['));
  {
    {
      SET_STATE(f, f->cur - 1, "", 0);
      while (cur(f) != ']') {
        snprintf(buf, sizeof(buf), "[%d]", i);
        i++;
        current_path_len = append_to_path(f, buf, strlen(buf));
        f->cur_name =
            f->path + strlen(f->path) - strlen(buf) + 1 /*opening brace*/;
        f->cur_name_len = strlen(buf) - 2 /*braces*/;
        TRY(parse_value(f));
        truncate_path(f, current_path_len);
        if (cur(f) == ',') f->cur++;
      }
      TRY(test_and_skip(f, ']'));
      truncate_path(f, fstate.path_len);
      CALL_BACK(f, JSON_TYPE_ARRAY_END, fstate.ptr, f->cur - fstate.ptr);
    }
  }
  return 0;
}

static int expect(struct frozen *f, const char *s, int len,
                  enum json_token_type tok_type) {
  int i, n = left(f);
  SET_STATE(f, f->cur, "", 0);
  for (i = 0; i < len; i++) {
    if (i >= n) return JSON_STRING_INCOMPLETE;
    if (f->cur[i] != s[i]) return JSON_STRING_INVALID;
  }
  f->cur += len;
  truncate_path(f, fstate.path_len);

  CALL_BACK(f, tok_type, fstate.ptr, f->cur - fstate.ptr);

  return 0;
}

/* value = 'null' | 'true' | 'false' | number | string | array | object */
static int parse_value(struct frozen *f) {
  int ch = cur(f);

  switch (ch) {
    case '"':
      TRY(parse_string(f));
      break;
    case '{':
      TRY(parse_object(f));
      break;
    case '[':
      TRY(parse_array(f));
      break;
    case 'n':
      TRY(expect(f, "null", 4, JSON_TYPE_NULL));
      break;
    case 't':
      TRY(expect(f, "true", 4, JSON_TYPE_TRUE));
      break;
    case 'f':
      TRY(expect(f, "false", 5, JSON_TYPE_FALSE));
      break;
    case '-':
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
      TRY(parse_number(f));
      break;
    default:
      return ch == END_OF_STRING ? JSON_STRING_INCOMPLETE : JSON_STRING_INVALID;
  }

  return 0;
}

/* key = identifier | string */
static int parse_key(struct frozen *f) {
  int ch = cur(f);
  if (is_alpha(ch)) {
    TRY(parse_identifier(f));
  } else if (ch == '"') {
    TRY(parse_string(f));
  } else {
    return ch == END_OF_STRING ? JSON_STRING_INCOMPLETE : JSON_STRING_INVALID;
  }
  return 0;
}

/* pair = key ':' value */
static int parse_pair(struct frozen *f) {
  int current_path_len;
  const char *tok;
  skip_whitespaces(f);
  tok = f->cur;
  TRY(parse_key(f));
  {
    f->cur_name = *tok == '"' ? tok + 1 : tok;
    f->cur_name_len = *tok == '"' ? f->cur - tok - 2 : f->cur - tok;
    current_path_len = append_to_path(f, f->cur_name, f->cur_name_len);
  }
  TRY(test_and_skip(f, ':'));
  TRY(parse_value(f));
  truncate_path(f, current_path_len);
  return 0;
}

/* object = '{' pair { ',' pair } '}' */
static int parse_object(struct frozen *f) {
  CALL_BACK(f, JSON_TYPE_OBJECT_START, NULL, 0);
  TRY(test_and_skip(f, '{'));
  {
    SET_STATE(f, f->cur - 1, ".", 1);
    while (cur(f) != '}') {
      TRY(parse_pair(f));
      if (cur(f) == ',') f->cur++;
    }
    TRY(test_and_skip(f, '}'));
    truncate_path(f, fstate.path_len);
    CALL_BACK(f, JSON_TYPE_OBJECT_END, fstate.ptr, f->cur - fstate.ptr);
  }
  return 0;
}

static int doit(struct frozen *f) {
  if (f->cur == 0 || f->end < f->cur) return JSON_STRING_INVALID;
  if (f->end == f->cur) return JSON_STRING_INCOMPLETE;
  return parse_value(f);
}

int json_walk(const char *json_string, int json_string_length,
              json_walk_callback_t callback, void *callback_data) {
  struct frozen frozen;

  memset(&frozen, 0, sizeof(frozen));
  frozen.end = json_string + json_string_length;
  frozen.cur = json_string;
  frozen.callback_data = callback_data;
  frozen.callback = callback;

  TRY(doit(&frozen));

  return frozen.cur - json_string;
}
