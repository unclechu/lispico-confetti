#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../lispico/src/evaluate.h"
#include "../../lispico/src/expression.h"
#include "../../lispico/src/storage.h"
#include "../../lispico/src/source-location.h"
#include "../../src/confetti-translate.h"

static void check_location(
  const Interpreter *interpreter,
  uint32_t index,
  const char *source,
  const char *text,
  uint32_t line,
  uint32_t column
) {
  const ExpressionSourceLocation *location =
    get_expression_source_location(interpreter, index);

  assert(location != NULL);
  assert(location->byte_offset == (size_t)(strstr(source, text) - source));
  assert(location->line == line);
  assert(location->column == column);
}

static uint32_t find_name(const Interpreter *interpreter, const char *name) {
  for (uint32_t i = 0; i < interpreter->expression_count; ++i) {
    const Expression *expression = &interpreter->storage.expressions[i];
    if (expression->kind != EXPR_NAME) continue;

    const ExpressionAllocation *allocation =
      get_expression_allocation(interpreter, expression->data);

    if (
      allocation != NULL && allocation->data != NULL &&
      strcmp(allocation->data, name) == 0
    ) return i;
  }
  return EXPR_NONE;
}

int main(void) {
  static Expression expressions[128] = {0};
  static ExpressionSourceLocation locations[128] = {0};
  static ExpressionAllocation expression_allocations[128] = {0};
  static Symbol symbols[256] = {0};
  static EvalRuntimeAllocation runtime_allocations[128] = {0};
  static uint32_t deferred_expressions[32] = {0};

  InterpreterStorage storage = {
    .expressions = expressions,
    .expression_capacity = 128,
    .expression_source_locations = locations,
    .expression_source_location_capacity = 128,
    .expression_allocations = expression_allocations,
    .expression_allocation_capacity = 128,
    .symbols = symbols,
    .symbol_capacity = 256,
    .runtime_allocations = runtime_allocations,
    .runtime_allocation_capacity = 128,
    .deferred_expression_indices = deferred_expressions,
    .deferred_expression_capacity = 32,
  };
  Interpreter interpreter = {0};
  assert(configure_interpreter_storage(&interpreter, &storage));
  char error[ERR_MSG_SIZE] = {'\0'};

  // Both ordinary Confetti arguments and generated expression calls retain
  // positions in the original source, even after comments and tabs.
  const char *source =
    "# First line is a comment\n"
    "\tvar \"x\" 1\n"
    "id missing\n";
  assert(parse_confetti_source(&interpreter, source, strlen(source), &error));
  uint32_t missing = find_name(&interpreter, "missing");
  assert(missing != EXPR_NONE);
  check_location(&interpreter, missing, source, "missing", 3, 4);
  check_location(&interpreter, 0, source, "var", 2, 2);

  // The same Lispico renderer displays Confetti source with tab-expanded
  // context and positions its arrow beneath the actual failing argument.
  const ExpressionSourceLocation *location = get_expression_source_location(&interpreter, missing);
  FILE *output = tmpfile();
  assert(output != NULL);
  assert(print_source_location(output, source, strlen(source), location, 1));
  assert(fflush(output) == 0);
  rewind(output);
  char shown[256] = {0};
  size_t shown_length = fread(shown, 1, sizeof(shown) - 1u, output);
  shown[shown_length] = '\0';
  assert(fclose(output) == 0);
  assert(strstr(shown, "2 |     var \"x\" 1\n") != NULL);
  assert(strstr(shown, "3 | id missing\n  |    ↑\n") != NULL);

  EvalOutcome result = evaluate(&interpreter, NULL, NULL);
  assert(result.kind == EVAL_FAIL);
  assert(result.failure_expression_index == missing);
  clean_evaluation(&interpreter);
  clean_symbols(&interpreter);

  // Indexing syntax is lowered into an arr-get call. Its original index
  // token must retain a distinct location, not the array name’s location.
  source =
    "arr-new \"numbers\" u32 2\n"
    "id numbers[missing]\n";
  assert(parse_confetti_source(&interpreter, source, strlen(source), &error));
  missing = find_name(&interpreter, "missing");
  assert(missing != EXPR_NONE);
  check_location(&interpreter, missing, source, "missing", 2, 12);
  assert(interpreter.storage.expressions[0].kind == EXPR_LIST);

  // Conditional lowering must map generated calls to their original tokens.
  source = "if 1 {\n  id missing\n}\n";
  assert(parse_confetti_source(&interpreter, source, strlen(source), &error));
  check_location(&interpreter, 0, source, "if", 1, 1);
  missing = find_name(&interpreter, "missing");
  assert(missing != EXPR_NONE);
  check_location(&interpreter, missing, source, "missing", 2, 6);

  // Cleanup clears the optional table, and reparsing replaces its entries.
  clean_expressions(&interpreter);
  assert(get_expression_source_location(&interpreter, 0) == NULL);
  assert(locations[0].line == 0);

  // Without the optional storage the translator must not require source maps.
  Interpreter no_map = {0};
  storage.expression_source_locations = NULL;
  storage.expression_source_location_capacity = 0;
  assert(configure_interpreter_storage(&no_map, &storage));
  assert(parse_confetti_source(&no_map, source, strlen(source), &error));
  assert(get_expression_source_location(&no_map, 0) == NULL);
  clean_expressions(&no_map);

  puts("Confetti source map tests passed");
  return 0;
}
