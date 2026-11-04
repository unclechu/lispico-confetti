#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lispico/src/evaluate.h"
#include "lispico/src/expression.h"
#include "lispico/src/read-file.h"
#include "lispico/src/storage.h"
#include "lispico/src/stdlib-print.h"
#include "lispico/src/stdlib-sleep.h"
#include "lispico/src/stdlib-env-var.h"
#include "lispico/src/stdlib-file-io.h"
#include "lispico/src/symbol-error.h"
#include "lispico/src/symbol-print.h"
#include "src/confetti-translate.h"

typedef struct {
  int argc;
  Array argv;
} SettingsState;

// Preserve Lispico's normal host-provided native functions and arguments.
static void eval_settings_cb(EvalSettingsInterface *interface) {
  SettingsState *state = (SettingsState *)interface->state;
  Interpreter *interpreter = interface->interpreter;
  char fail_msg[ERR_MSG_SIZE] = {'\0'};

  Symbol *argc_symbol = interface->define_symbol(interface, "argc", SYM_VARIABLE);
  if (argc_symbol == NULL) return;
  argc_symbol->as.variable = (Value) { .type = TYPE_U32, .as.u32 = state->argc };

  Symbol *argv_symbol = interface->define_symbol(interface, "argv", SYM_ARRAY);
  if (argv_symbol == NULL) return;
  argv_symbol->as.array = state->argv;

  const char *symbol_name;
  POPULATE_STDLIB_PRINT();
  POPULATE_STDLIB_SLEEP();
  POPULATE_STDLIB_ENV_VAR();
  POPULATE_STDLIB_FILE_IO();
  return;

  symbol_define_fail:
    extract_error_message_from_symbol_define_error(interpreter, &fail_msg, symbol_name);
    interface->report_failure(interface, fail_msg);
}

int main(int argc, char **argv) {
  // Match Lispico's host-owned, fixed-capacity interpreter storage.
  static Expression expressions[EXPR_CAPACITY] = {0};
  static ExpressionAllocation expression_allocations[EXPR_ALLOCATION_CAPACITY] = {0};
  static Symbol symbols[SYMBOL_CAPACITY] = {0};
  static EvalRuntimeAllocation runtime_allocations[EVAL_RUNTIME_ALLOCATION_CAPACITY] = {0};
  static uint32_t deferred_expressions[EVAL_DEFER_CAPACITY] = {0};

  InterpreterStorage storage = {
    .expressions = expressions,
    .expression_capacity = EXPR_CAPACITY,
    .expression_allocations = expression_allocations,
    .expression_allocation_capacity = EXPR_ALLOCATION_CAPACITY,
    .symbols = symbols,
    .symbol_capacity = SYMBOL_CAPACITY,
    .runtime_allocations = runtime_allocations,
    .runtime_allocation_capacity = EVAL_RUNTIME_ALLOCATION_CAPACITY,
    .deferred_expression_indices = deferred_expressions,
    .deferred_expression_capacity = EVAL_DEFER_CAPACITY,
  };

  Interpreter interpreter = {0};
  if (!configure_interpreter_storage(&interpreter, &storage)) {
    fprintf(stderr, "[FAIL] Unable to configure interpreter storage\n");
    return 1;
  }

  int exit_code = 0;
  Array argv_array = {
    .element_type = TYPE_STRING,
    .length = (uint32_t)argc,
    .data = malloc(sizeof(String) * (size_t)argc),
  };
  if (argv_array.data == NULL) {
    fprintf(stderr, "[FAIL] Unable to allocate argv array\n");
    return 1;
  }

  for (int i = 0; i < argc; ++i) {
    ((String *)argv_array.data)[i] = (String) {
      .data = argv[i],
      .length = strlen(argv[i]),
    };
  }

  SettingsState state = { .argc = argc, .argv = argv_array };
  static char source[SOURCE_CAPACITY] = {0};
  size_t source_length = 0;
  char parse_error[ERR_MSG_SIZE] = {'\0'};
  const char *source_path = argc > 1 ? argv[1] : "/dev/stdin";

  if (!read_file_contents(source_path, &source, &source_length)) {
    exit_code = 1;
    goto done;
  }

  // Unlike Lispico’s length-based parser, Confetti requires a terminating
  // NUL byte. Reserve it explicitly within the source buffer.
  if (source_length >= SOURCE_CAPACITY) {
    fprintf(stderr, "[FAIL] Confetti source is too large\n");
    exit_code = 1;
    goto done;
  }
  source[source_length] = '\0';

  if (!parse_confetti_source(&interpreter, source, source_length, &parse_error)) {
    fprintf(stderr, "Parse error: %s\n", parse_error);
    exit_code = 1;
    goto done;
  }

  EvalOutcome outcome = evaluate(&interpreter, eval_settings_cb, &state);

  switch (outcome.kind) {
    case EVAL_OK:
    case EVAL_EXIT:
      if (outcome.kind == EVAL_EXIT) exit_code = outcome.as.exit_code;

      switch (outcome.result.kind) {
        case SYM_EMPTY:
          printf("%s\n", show_SymbolKind(outcome.result.kind));
          break;

        case SYM_VARIABLE: {
          char *shown = show_Value(outcome.result.as.value);
          if (shown == NULL) {
            fprintf(stderr, "[FAIL] Unable to format result\n");
            exit_code = 1;
            break;
          }
          printf("%s(%s)\n", show_SymbolKind(outcome.result.kind), shown);
          free(shown);
          break;
        }

        case SYM_ARRAY: {
          char *shown = show_Array(outcome.result.as.array);
          if (shown == NULL) {
            fprintf(stderr, "[FAIL] Unable to format result\n");
            exit_code = 1;
            break;
          }
          printf("%s(%s)\n", show_SymbolKind(outcome.result.kind), shown);
          free(shown);
          break;
        }

        default:
          fprintf(stderr, "[FAIL] Unexpected result kind: %d\n", outcome.result.kind);
          exit_code = 1;
          break;
      }
      break;

    case EVAL_FAIL:
      fprintf(stderr, "[FAIL] Evaluation failed: %s\n", outcome.as.fail_msg);
      exit_code = 1;
      break;

    default:
      fprintf(stderr, "[FAIL] Unexpected evaluation outcome: %d\n", outcome.kind);
      exit_code = 1;
      break;
  }

  done:
    clean_evaluation(&interpreter);
    clean_symbols(&interpreter);
    clean_expressions(&interpreter);
    free(argv_array.data);
    return exit_code;
}
