#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "../lispico/src/storage.h"

// Parse Confetti directives directly into Lispico expression storage.
// `source` must be NUL-terminated at `source[length]` for Confetti’s parser.
// On failure, the expression tables are cleaned and `error` is populated.
bool parse_confetti_source(
  Interpreter *interpreter,
  const char *source,
  size_t length,
  char (*error)[ERR_MSG_SIZE]
);
