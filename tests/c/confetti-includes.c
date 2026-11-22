#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../lispico/src/evaluate.h"
#include "../../lispico/src/expression.h"
#include "../../lispico/src/source-location.h"
#include "../../lispico/src/source-registry.h"
#include "../../src/confetti-translate.h"

static void write_file(const char *path, const char *contents) {
  FILE *file = fopen(path, "wb");
  assert(file != NULL);
  size_t length = strlen(contents);
  assert(fwrite(contents, 1, length, file) == length);
  assert(fclose(file) == 0);
}

static bool parse(
  Interpreter *interpreter,
  bool with_map,
  const char *path,
  const char *source,
  SourceRegistry *registry,
  char (*error)[ERR_MSG_SIZE],
  ParseDiagnostic *diagnostic
) {
  *registry = (SourceRegistry) { .dynamic = true };
  if (with_map) {
    uint32_t id = UINT32_MAX;
    assert(register_source_file(registry, path, source, strlen(source), &id));
    assert(id == 0);
  }
  return parse_confetti_source_with_includes_diagnostic(
    interpreter,
    source,
    strlen(source),
    path,
    with_map ? registry : NULL,
    error,
    diagnostic
  );
}

int main(void) {
  static Expression expressions[512];
  static ExpressionSourceLocation locations[512];
  static ExpressionAllocation allocations[512];
  static Symbol symbols[512];
  static EvalRuntimeAllocation runtime_allocations[512];
  static uint32_t defers[64];
  InterpreterStorage storage = {
    .expressions = expressions,
    .expression_capacity = 512,
    .expression_source_locations = locations,
    .expression_source_location_capacity = 512,
    .expression_allocations = allocations,
    .expression_allocation_capacity = 512,
    .symbols = symbols,
    .symbol_capacity = 512,
    .runtime_allocations = runtime_allocations,
    .runtime_allocation_capacity = 512,
    .deferred_expression_indices = defers,
    .deferred_expression_capacity = 64,
  };
  Interpreter interpreter = {0};
  assert(configure_interpreter_storage(&interpreter, &storage));

  char directory[] = "/tmp/lispico-confetti-includes-XXXXXX";
  assert(mkdtemp(directory) != NULL);
  char nested[256], main_path[256], first[256], second[256];
  char empty[256], values[256], bad[256], missing[256], alias[256];
  snprintf(nested, sizeof(nested), "%s/nested", directory);
  snprintf(main_path, sizeof(main_path), "%s/main.confetti", directory);
  snprintf(first, sizeof(first), "%s/first.confetti", directory);
  snprintf(second, sizeof(second), "%s/nested/second.confetti", directory);
  snprintf(empty, sizeof(empty), "%s/empty.confetti", directory);
  snprintf(values, sizeof(values), "%s/values.confetti", directory);
  snprintf(bad, sizeof(bad), "%s/nested/bad.confetti", directory);
  snprintf(missing, sizeof(missing), "%s/missing.confetti", directory);
  snprintf(alias, sizeof(alias), "%s/main-link.confetti", directory);
  assert(mkdir(nested, 0700) == 0);

  write_file(first, "include \"nested/second.confetti\"\n");
  write_file(second, "id 3\n");
  write_file(empty, "# Nothing to insert.\n");
  write_file(values, "id 2\nid 3\nid 4\n");
  const char *program =
    "include \"empty.confetti\"\n"
    "var \"x\" 2\n"
    "do {\n"
    "  assert {1 + {include \"first.confetti\"}}\n"
    "  assert {+ {include \"values.confetti\"}}\n"
    "  include \"first.confetti\"\n"
    "  include \"first.confetti\"\n"
    "}\n";
  // The same file can be expanded repeatedly, including within an argument
  // block. Each occurrence receives its own source registry entry.
  write_file(main_path, program);
  SourceRegistry registry = {0};
  char error[ERR_MSG_SIZE] = {'\0'};
  ParseDiagnostic diagnostic = {0};
  assert(parse(&interpreter, true, main_path, program, &registry, &error, &diagnostic));
  assert(registry.count == 9); // Root, empty, values, 3 copies of first and second.
  for (uint32_t i = 1; i < registry.count; ++i) {
    const SourceFile *file = get_source_file(&registry, i);
    assert(file != NULL);
    assert(file->has_include_location);
  }
  bool found_nested = false;
  for (uint32_t i = 0; i < interpreter.expression_count; ++i) {
    const ExpressionSourceLocation *location = get_expression_source_location(&interpreter, i);
    if (location != NULL && location->source_id > 0) found_nested = true;
  }
  assert(found_nested);
  EvalOutcome outcome = evaluate(&interpreter, NULL, NULL);
  assert(outcome.kind == EVAL_OK);
  clean_evaluation(&interpreter);
  clean_symbols(&interpreter);
  clean_source_registry(&registry);

  // A syntax error in a nested include points to the child's actual file,
  // not the parent directive, even when source maps are disabled.
  write_file(bad, "do {\n");
  write_file(first, "include \"nested/bad.confetti\"\n");
  program = "id 99\ninclude \"first.confetti\"\n";
  write_file(main_path, program);
  assert(!parse(&interpreter, true, main_path, program, &registry, &error, &diagnostic));
  assert(diagnostic.has_location);
  assert(strcmp(diagnostic.path, bad) == 0);
  assert(diagnostic.location.line == 2);
  assert(strstr(diagnostic.include_trace, "Included from") != NULL);
  assert(strstr(diagnostic.include_trace, first) != NULL);
  assert(strstr(diagnostic.include_trace, main_path) != NULL);
  assert(interpreter.expression_count == 0); // Cleanup after partial translation.
  clean_source_registry(&registry);

  // Translation errors identify the precise malformed argument within the
  // deepest included source, not merely the parent's include directive.
  write_file(bad, "id 4294967296\n");
  assert(!parse(&interpreter, true, main_path, program, &registry, &error, &diagnostic));
  assert(diagnostic.has_location);
  assert(strcmp(diagnostic.path, bad) == 0);
  assert(diagnostic.location.line == 1);
  assert(diagnostic.location.column == 4);
  assert(strstr(diagnostic.include_trace, main_path) != NULL);
  clean_source_registry(&registry);
  write_file(bad, "do {\n");

  storage.expression_source_locations = NULL;
  storage.expression_source_location_capacity = 0;
  Interpreter no_map = {0};
  assert(configure_interpreter_storage(&no_map, &storage));
  assert(!parse(&no_map, false, main_path, program, &registry, &error, &diagnostic));
  assert(diagnostic.has_location);
  assert(strcmp(diagnostic.path, bad) == 0);
  assert(strstr(diagnostic.include_trace, main_path) != NULL);
  clean_source_registry(&registry);

  // File expansion works without source maps or a registry. For stdin, the
  // containing directory is cwd, rather than the path of a previous source.
  write_file(first, "include \"nested/second.confetti\"\n");
  program = "include \"first.confetti\"\n";
  assert(parse(&no_map, false, main_path, program, &registry, &error, &diagnostic));
  outcome = evaluate(&no_map, NULL, NULL);
  assert(outcome.kind == EVAL_OK);
  clean_evaluation(&no_map);
  clean_symbols(&no_map);
  clean_source_registry(&registry);

  char original_directory[4096] = {'\0'};
  assert(getcwd(original_directory, sizeof(original_directory)) != NULL);
  assert(chdir(directory) == 0);
  assert(parse(&no_map, false, "/dev/stdin", program, &registry, &error, &diagnostic));
  outcome = evaluate(&no_map, NULL, NULL);
  assert(outcome.kind == EVAL_OK);
  clean_evaluation(&no_map);
  clean_symbols(&no_map);
  clean_source_registry(&registry);
  assert(chdir(original_directory) == 0);

  // Invalid paths are reported at the including directive.
  program = "include missing\n";
  assert(!parse(&no_map, false, main_path, program, &registry, &error, &diagnostic));
  assert(strstr(error, "literal string") != NULL);
  assert(strcmp(diagnostic.path, main_path) == 0);
  clean_source_registry(&registry);

  // Confetti consumes the first escape layer. The surviving \0 must be
  // decoded by Lispico so the include path is rejected for containing NUL.
  program = "include \"no\\\\0path.confetti\"\n";
  assert(!parse(&no_map, false, main_path, program, &registry, &error, &diagnostic));
  assert(strstr(error, "without NUL") != NULL);
  clean_source_registry(&registry);

  program = "include \"missing.confetti\"\n";
  assert(!parse(&no_map, false, main_path, program, &registry, &error, &diagnostic));
  assert(strstr(error, "Unable to open include") != NULL);
  assert(strcmp(diagnostic.path, main_path) == 0);
  clean_source_registry(&registry);

  // A hard link to the entry script cannot bypass circular-include detection.
  program = "include \"main-link.confetti\"\n";
  write_file(main_path, program);
  assert(link(main_path, alias) == 0);
  assert(!parse(&no_map, false, main_path, program, &registry, &error, &diagnostic));
  assert(strstr(error, "Circular include") != NULL);
  assert(no_map.expression_count == 0); // Failed partial parsing is cleaned.
  clean_source_registry(&registry);

  assert(unlink(alias) == 0);
  assert(unlink(main_path) == 0);
  assert(unlink(first) == 0);
  assert(unlink(second) == 0);
  assert(unlink(empty) == 0);
  assert(unlink(values) == 0);
  assert(unlink(bad) == 0);
  assert(rmdir(nested) == 0);
  assert(rmdir(directory) == 0);
  clean_expressions(&interpreter);
  puts("Confetti includes tests passed");
  return 0;
}
