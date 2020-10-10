#include "elsa.h"

#include <stdio.h>

int main() {
  struct json_out out_file = JSON_OUT_FILE(stdout);
  char json1[] = "{}";
  char json2[100];
  struct json_out out_json2 = JSON_OUT_BUF(json2, sizeof json2);
  json_setf(json1, sizeof json1 - 1, &out_json2, ".bar", "456");
  json_printf(&out_file, "{foo:%s}", json2);
}

