#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "../lispico/src/parse.h" // ParseDiagnostic (without linking Lispico’s S-expression parser).
#include "../lispico/src/source-registry.h"
#include "../lispico/src/storage.h"

// Parse one in-memory Confetti source. Preserves the existing no-filesystem API.
// Source must be NUL-terminated at source[length]. Expressions are reset first.
bool parse_confetti_source(
  Interpreter *interpreter,
  const char *source,
  size_t length,
  char (*error)[ERR_MSG_SIZE]
);

// Expand literal `include "PATH"` directives recursively during translation.
// Resolve paths relative to their containing file, or cwd for stdin. Source
// maps are optional; when enabled the main source must be registry entry 0.
bool parse_confetti_source_with_includes(
  Interpreter *interpreter,
  const char *source,
  size_t length,
  const char *source_path,
  SourceRegistry *registry,
  char (*error)[ERR_MSG_SIZE]
);

// Capture the failing source position and include chain for host rendering.
// The caller owns both the root source and the source registry.
bool parse_confetti_source_with_includes_diagnostic(
  Interpreter *interpreter,
  const char *source,
  size_t length,
  const char *source_path,
  SourceRegistry *registry,
  char (*error)[ERR_MSG_SIZE],
  ParseDiagnostic *diagnostic
);
