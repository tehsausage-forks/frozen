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
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"

struct walk_ctx {
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

#define SET_STATE(ctx, ptr, str, len)              \
  struct fstate fstate = {(ptr), (ctx)->path_len}; \
  append_to_path((ctx), (str), (len));

#define CALL_BACK(ctx, tok, value, len)                                       \
  do {                                                                        \
    if ((ctx)->callback &&                                                    \
        ((ctx)->path_len == 0 || (ctx)->path[(ctx)->path_len - 1] != '.')) {  \
      struct json_token t = {(value), (len), (tok)};                          \
                                                                              \
      /* Call the callback with the given value and current name */           \
      (ctx)->callback((ctx)->callback_data, (ctx)->cur_name,                  \
                      (ctx)->cur_name_len, (ctx)->path, &t);                  \
                                                                              \
      /* Reset the name */                                                    \
      (ctx)->cur_name = NULL;                                                 \
      (ctx)->cur_name_len = 0;                                                \
    }                                                                         \
  } while (0)

static int append_to_path(struct walk_ctx *ctx, const char *str, int size) {
  int n = ctx->path_len;
  int left = sizeof(ctx->path) - n - 1;
  if (size > left) size = left;
  memcpy(ctx->path + n, str, size);
  ctx->path[n + size] = '\0';
  ctx->path_len += size;
  return n;
}

static void truncate_path(struct walk_ctx *ctx, size_t len) {
  ctx->path_len = len;
  ctx->path[len] = '\0';
}

static int parse_object(struct walk_ctx *ctx);
static int parse_value(struct walk_ctx *ctx);

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

static int left(const struct walk_ctx *ctx) {
  return ctx->end - ctx->cur;
}

static void skip_whitespaces(struct walk_ctx *ctx) {
  while (ctx->cur < ctx->end && is_space(*ctx->cur)) ctx->cur++;
}

static int cur(struct walk_ctx *ctx) {
  skip_whitespaces(ctx);
  return ctx->cur >= ctx->end ? END_OF_STRING : *(unsigned char *) ctx->cur;
}

static int test_and_skip(struct walk_ctx *ctx, int expected) {
  int ch = cur(ctx);
  if (ch == expected) {
    ctx->cur++;
    return 0;
  }
  return ch == END_OF_STRING ? JSON_STRING_INCOMPLETE : JSON_STRING_INVALID;
}

/* identifier = letter { letter | digit | '_' } */
static int parse_identifier(struct walk_ctx *ctx) {
  EXPECT(is_alpha(cur(ctx)), JSON_STRING_INVALID);
  {
    SET_STATE(ctx, ctx->cur, "", 0);
    while (ctx->cur < ctx->end &&
           (*ctx->cur == '_' || is_alpha(*ctx->cur) || is_digit(*ctx->cur))) {
      ctx->cur++;
    }
    truncate_path(ctx, fstate.path_len);
    CALL_BACK(ctx, JSON_TYPE_STRING, fstate.ptr, ctx->cur - fstate.ptr);
  }
  return 0;
}

/* string = '"' { quoted_printable_chars } '"' */
static int parse_string(struct walk_ctx *ctx) {
  int n, ch = 0, len = 0;
  TRY(test_and_skip(ctx, '"'));
  {
    SET_STATE(ctx, ctx->cur, "", 0);
    for (; ctx->cur < ctx->end; ctx->cur += len) {
      ch = *(unsigned char *) ctx->cur;
      len = get_utf8_char_len((unsigned char) ch);
      EXPECT(ch >= 32 && len > 0, JSON_STRING_INVALID); /* No control chars */
      EXPECT(len <= left(ctx), JSON_STRING_INCOMPLETE);
      if (ch == '\\') {
        EXPECT((n = get_escape_len(ctx->cur + 1, left(ctx))) > 0, n);
        len += n;
      } else if (ch == '"') {
        truncate_path(ctx, fstate.path_len);
        CALL_BACK(ctx, JSON_TYPE_STRING, fstate.ptr, ctx->cur - fstate.ptr);
        ctx->cur++;
        break;
      };
    }
  }
  return ch == '"' ? 0 : JSON_STRING_INCOMPLETE;
}

/* number = [ '-' ] digit+ [ '.' digit+ ] [ ['e'|'E'] ['+'|'-'] digit+ ] */
static int parse_number(struct walk_ctx *ctx) {
  int ch = cur(ctx);
  SET_STATE(ctx, ctx->cur, "", 0);
  if (ch == '-') ctx->cur++;
  EXPECT(ctx->cur < ctx->end, JSON_STRING_INCOMPLETE);
  EXPECT(is_digit(ctx->cur[0]), JSON_STRING_INVALID);
  while (ctx->cur < ctx->end && is_digit(ctx->cur[0])) ctx->cur++;
  if (ctx->cur < ctx->end && ctx->cur[0] == '.') {
    ctx->cur++;
    EXPECT(ctx->cur < ctx->end, JSON_STRING_INCOMPLETE);
    EXPECT(is_digit(ctx->cur[0]), JSON_STRING_INVALID);
    while (ctx->cur < ctx->end && is_digit(ctx->cur[0])) ctx->cur++;
  }
  if (ctx->cur < ctx->end && (ctx->cur[0] == 'e' || ctx->cur[0] == 'E')) {
    ctx->cur++;
    EXPECT(ctx->cur < ctx->end, JSON_STRING_INCOMPLETE);
    if ((ctx->cur[0] == '+' || ctx->cur[0] == '-')) ctx->cur++;
    EXPECT(ctx->cur < ctx->end, JSON_STRING_INCOMPLETE);
    EXPECT(is_digit(ctx->cur[0]), JSON_STRING_INVALID);
    while (ctx->cur < ctx->end && is_digit(ctx->cur[0])) ctx->cur++;
  }
  truncate_path(ctx, fstate.path_len);
  CALL_BACK(ctx, JSON_TYPE_NUMBER, fstate.ptr, ctx->cur - fstate.ptr);
  return 0;
}

/* array = '[' [ value { ',' value } ] ']' */
static int parse_array(struct walk_ctx *ctx) {
  int i = 0, current_path_len;
  char buf[20];
  CALL_BACK(ctx, JSON_TYPE_ARRAY_START, NULL, 0);
  TRY(test_and_skip(ctx, '['));
  {
    {
      SET_STATE(ctx, ctx->cur - 1, "", 0);
      while (cur(ctx) != ']') {
        snprintf(buf, sizeof(buf), "[%d]", i);
        i++;
        current_path_len = append_to_path(ctx, buf, strlen(buf));
        ctx->cur_name =
            ctx->path + strlen(ctx->path) - strlen(buf) + 1 /*opening brace*/;
        ctx->cur_name_len = strlen(buf) - 2 /*braces*/;
        TRY(parse_value(ctx));
        truncate_path(ctx, current_path_len);
        if (cur(ctx) == ',') ctx->cur++;
      }
      TRY(test_and_skip(ctx, ']'));
      truncate_path(ctx, fstate.path_len);
      CALL_BACK(ctx, JSON_TYPE_ARRAY_END, fstate.ptr, ctx->cur - fstate.ptr);
    }
  }
  return 0;
}

static int expect(struct walk_ctx *ctx, const char *s, int len,
                  enum json_token_type tok_type) {
  int i, n = left(ctx);
  SET_STATE(ctx, ctx->cur, "", 0);
  for (i = 0; i < len; i++) {
    if (i >= n) return JSON_STRING_INCOMPLETE;
    if (ctx->cur[i] != s[i]) return JSON_STRING_INVALID;
  }
  ctx->cur += len;
  truncate_path(ctx, fstate.path_len);

  CALL_BACK(ctx, tok_type, fstate.ptr, ctx->cur - fstate.ptr);

  return 0;
}

/* value = 'null' | 'true' | 'false' | number | string | array | object */
static int parse_value(struct walk_ctx *ctx) {
  int ch = cur(ctx);

  switch (ch) {
    case '"':
      TRY(parse_string(ctx));
      break;
    case '{':
      TRY(parse_object(ctx));
      break;
    case '[':
      TRY(parse_array(ctx));
      break;
    case 'n':
      TRY(expect(ctx, "null", 4, JSON_TYPE_NULL));
      break;
    case 't':
      TRY(expect(ctx, "true", 4, JSON_TYPE_TRUE));
      break;
    case 'f':
      TRY(expect(ctx, "false", 5, JSON_TYPE_FALSE));
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
      TRY(parse_number(ctx));
      break;
    default:
      return ch == END_OF_STRING ? JSON_STRING_INCOMPLETE : JSON_STRING_INVALID;
  }

  return 0;
}

/* key = identifier | string */
static int parse_key(struct walk_ctx *ctx) {
  int ch = cur(ctx);
  if (is_alpha(ch)) {
    TRY(parse_identifier(ctx));
  } else if (ch == '"') {
    TRY(parse_string(ctx));
  } else {
    return ch == END_OF_STRING ? JSON_STRING_INCOMPLETE : JSON_STRING_INVALID;
  }
  return 0;
}

/* pair = key ':' value */
static int parse_pair(struct walk_ctx *ctx) {
  int current_path_len;
  const char *tok;
  skip_whitespaces(ctx);
  tok = ctx->cur;
  TRY(parse_key(ctx));
  {
    ctx->cur_name = *tok == '"' ? tok + 1 : tok;
    ctx->cur_name_len = *tok == '"' ? ctx->cur - tok - 2 : ctx->cur - tok;
    current_path_len = append_to_path(ctx, ctx->cur_name, ctx->cur_name_len);
  }
  TRY(test_and_skip(ctx, ':'));
  TRY(parse_value(ctx));
  truncate_path(ctx, current_path_len);
  return 0;
}

/* object = '{' pair { ',' pair } '}' */
static int parse_object(struct walk_ctx *ctx) {
  CALL_BACK(ctx, JSON_TYPE_OBJECT_START, NULL, 0);
  TRY(test_and_skip(ctx, '{'));
  {
    SET_STATE(ctx, ctx->cur - 1, ".", 1);
    while (cur(ctx) != '}') {
      TRY(parse_pair(ctx));
      if (cur(ctx) == ',') ctx->cur++;
    }
    TRY(test_and_skip(ctx, '}'));
    truncate_path(ctx, fstate.path_len);
    CALL_BACK(ctx, JSON_TYPE_OBJECT_END, fstate.ptr, ctx->cur - fstate.ptr);
  }
  return 0;
}

static int doit(struct walk_ctx *ctx) {
  if (ctx->cur == 0 || ctx->end < ctx->cur) return JSON_STRING_INVALID;
  if (ctx->end == ctx->cur) return JSON_STRING_INCOMPLETE;
  return parse_value(ctx);
}

int json_walk(const char *json_string, int json_string_length,
              json_walk_callback_t callback, void *callback_data) {
  struct walk_ctx ctx;

  memset(&ctx, 0, sizeof(ctx));
  ctx.end = json_string + json_string_length;
  ctx.cur = json_string;
  ctx.callback_data = callback_data;
  ctx.callback = callback;

  TRY(doit(&ctx));

  return ctx.cur - json_string;
}
