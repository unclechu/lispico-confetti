#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../lispico/src/evaluate.h"
#include "../../lispico/src/expression.h"
#include "../../lispico/src/source-location.h"
#include "../../src/confetti-translate.h"

static const char *expression_name(const Interpreter *interpreter, uint32_t index) {
  const Expression *expression = &interpreter->storage.expressions[index];
  if (expression->kind != EXPR_NAME) return NULL;
  const ExpressionAllocation *name = get_expression_allocation(interpreter, expression->data);
  return name != NULL ? name->data : NULL;
}

static void check_translation(
  Interpreter *interpreter,
  const char *source,
  bool infix_condition,
  bool explicit_do
) {
  char error[ERR_MSG_SIZE] = {'\0'};
  assert(parse_confetti_source(interpreter, source, strlen(source), &error));

  const Expression *outer = &interpreter->storage.expressions[0];
  assert(outer->kind == EXPR_LIST);
  uint32_t name_index = outer->first_child;
  assert(strcmp(expression_name(interpreter, name_index), "while") == 0);
  uint32_t condition_index = interpreter->storage.expressions[name_index].next;
  uint32_t body_index = interpreter->storage.expressions[condition_index].next;
  assert(body_index != EXPR_NONE);
  assert(interpreter->storage.expressions[body_index].next == EXPR_NONE);

  const Expression *condition = &interpreter->storage.expressions[condition_index];
  if (infix_condition) {
    assert(condition->kind == EXPR_LIST);
    assert(strcmp(expression_name(interpreter, condition->first_child), "<") == 0);
  } else {
    assert(condition->kind != EXPR_LIST);
  }

  const Expression *body = &interpreter->storage.expressions[body_index];
  if (explicit_do) {
    assert(body->kind == EXPR_LIST);
    assert(strcmp(expression_name(interpreter, body->first_child), "do") == 0);
  } else if (body->kind == EXPR_LIST) {
    assert(strcmp(expression_name(interpreter, body->first_child), "do") != 0);
  }
  clean_expressions(interpreter);
}

static void check_invalid(Interpreter *interpreter, const char *source, const char *message) {
  char error[ERR_MSG_SIZE] = {'\0'};
  assert(!parse_confetti_source(interpreter, source, strlen(source), &error));
  assert(strstr(error, message) != NULL);
}

int main(void) {
  static Expression expressions[2048] = {0};
  static ExpressionSourceLocation locations[2048] = {0};
  static ExpressionAllocation expression_allocations[2048] = {0};
  static Symbol symbols[2048] = {0};
  static EvalRuntimeAllocation runtime_allocations[2048] = {0};
  static uint32_t deferred_expressions[128] = {0};
  InterpreterStorage storage = {
    .expressions = expressions,
    .expression_capacity = 2048,
    .expression_source_locations = locations,
    .expression_source_location_capacity = 2048,
    .expression_allocations = expression_allocations,
    .expression_allocation_capacity = 2048,
    .symbols = symbols,
    .symbol_capacity = 2048,
    .runtime_allocations = runtime_allocations,
    .runtime_allocation_capacity = 2048,
    .deferred_expression_indices = deferred_expressions,
    .deferred_expression_capacity = 128,
  };
  Interpreter interpreter = {0};
  assert(configure_interpreter_storage(&interpreter, &storage));

  // `do` always contributes a real Lispico sequence scope, including for
  // empty blocks and a body containing just one expression.
  check_translation(&interpreter, "while 1 < 2 do {id 123}\n", true, true);
  check_translation(&interpreter, "while {1 < 2} do {id 123}\n", true, true);
  check_translation(&interpreter, "while true do {id 123}\n", false, true);
  check_translation(&interpreter, "while false do {}\n", false, true);

  // Ordinary two-argument calls and argument blocks do not acquire a `do`.
  check_translation(&interpreter, "while true 123\n", false, false);
  check_translation(&interpreter, "while {1 < 2; id 123}\n", true, false);

  check_invalid(&interpreter, "while do {id 123}\n", "requires a condition");
  check_invalid(&interpreter, "while true then {id 123}\n", "not `then { … }`");
  check_invalid(&interpreter, "while 1 < 2 == 3 do {id 123}\n", "Mixed or unsupported");
  check_invalid(&interpreter, "while {id true; id false} do {id 123}\n", "multiple expressions");
  check_invalid(&interpreter, "while {id true} do\n", "expects `do { … }`");

  // Full evaluation checks scope depths, repeated condition evaluation,
  // function-local loops, and `(return)` from a nested explicit `do`.
  const char *program =
    "var \"counter\" 0\n"
    "var \"observed\" 0\n"
    "while counter < 3 do {\n"
    "  override \"observed\" {depth}\n"
    "  counter = {counter + 1}\n"
    "}\n"
    "assert {observed == 2}\n"
    "while {counter < 5} do {counter = {counter + 1}}\n"
    "while {counter < 7; counter = {counter + 1}}\n"
    "assert {counter == 7}\n"
    "while false 123\n"
    "fn \"find\" args target do {\n"
    "  local \"i\" 0\n"
    "  while i < 10 do {\n"
    "    if i == target then {return i}\n"
    "    set \"i\" {i + 1}\n"
    "  }\n"
    "  return 99\n"
    "}\n"
    "assert {find 4 == 4}\n"
    "assert {find 20 == 99}\n"
    "id empty\n";
  char error[ERR_MSG_SIZE] = {'\0'};
  assert(parse_confetti_source(&interpreter, program, strlen(program), &error));
  EvalOutcome result = evaluate(&interpreter, NULL, NULL);
  if (result.kind != EVAL_OK) fprintf(stderr, "While evaluation failed: %s\n", result.as.fail_msg);
  assert(result.kind == EVAL_OK);
  clean_evaluation(&interpreter);
  clean_symbols(&interpreter);
  clean_expressions(&interpreter);

  puts("Confetti while translation tests passed");
  return 0;
}
