#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lispico/src/evaluate.h"
#include "lispico/src/evaluate-copy.h"
#include "lispico/src/expression.h"
#include "lispico/src/read-file.h"
#include "lispico/src/storage.h"
#include "lispico/src/stdlib-print.h"
#include "lispico/src/stdlib-sleep.h"
#include "lispico/src/stdlib-env-var.h"
#include "lispico/src/stdlib-file-io.h"
#include "lispico/src/symbol-error.h"
#include "lispico/src/symbol-print.h"
#include "lispico/src/parse-cli-args.h"
#include "lispico/src/source-highlight.h"
#include "src/confetti-translate.h"

#define DEFAULT_EXECUTABLE_NAME "lispico-confetti"

typedef struct {
  int argc;
  Array argv;
  Array args;
  const CliOptions *cli;
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

  Symbol *args_sym = interface->define_symbol(interface, "args", SYM_ARRAY);
  if (args_sym == NULL) return;
  args_sym->as.array = state->args;

  const char *symbol_name;
  POPULATE_STDLIB_PRINT();
  POPULATE_STDLIB_SLEEP();
  POPULATE_STDLIB_ENV_VAR();
  POPULATE_STDLIB_FILE_IO();

  // CLI definitions come last, intentionally allowing overrides of built-in
  // symbols, including `argc`, `argv`, `args` and native standard-library
  // functions.
  for (size_t i = 0; i < state->cli->definition_count; ++i) {
    const CliDefinition *definition = &state->cli->definitions[i];
    symbol_name = definition->name;
    Symbol *sym = interface->symbol_lookup(interface, symbol_name);
    if (sym == NULL) {
      sym = interface->define_symbol(interface, symbol_name, definition->kind);
      if (sym == NULL) goto symbol_define_fail;
    }

    if (definition->kind == SYM_VARIABLE) {
      Value value = definition->as.value;
      if (value.type == TYPE_STRING) {
        String copied = {0};
        if (!copy_runtime_string(interpreter, value.as.string, &copied)) {
          interface->report_failure(interface, "Unable to allocate CLI string value");
          return;
        }
        value.as.string = copied;
      }
      sym->kind = SYM_VARIABLE;
      sym->as.variable = value;
    } else if (definition->kind == SYM_ARRAY) {
      Array copied = {0};
      if (!copy_runtime_array(interpreter, definition->as.array, &copied)) {
        interface->report_failure(interface, "Unable to allocate CLI array value");
        return;
      }
      sym->kind = SYM_ARRAY;
      sym->as.array = copied;
    } else {
      interface->report_failure(interface, "Unsupported CLI definition kind");
      return;
    }

    sym->frozen = definition->frozen;
  }

  return;

  symbol_define_fail:
    extract_error_message_from_symbol_define_error(interpreter, &fail_msg, symbol_name);
    interface->report_failure(interface, fail_msg);
}

int main(int argc, char **argv) {
  CliOptions cli = {0};
  char parse_error[ERR_MSG_SIZE] = {'\0'};
  CliParseStatus cli_status = parse_cli_args(argc, argv, &cli, parse_error, sizeof(parse_error));

  switch (cli_status) {
    case CLI_PARSE_OK:
      break;

    case CLI_PARSE_HELP:
      print_cli_usage(stdout, argc > 0 ? argv[0] : DEFAULT_EXECUTABLE_NAME);
      clean_cli_args(&cli);
      return 0;

    case CLI_PARSE_ERROR:
      fprintf(stderr, "%s: %s\n\n", argc > 0 ? argv[0] : DEFAULT_EXECUTABLE_NAME, parse_error);
      print_cli_usage(stderr, argc > 0 ? argv[0] : DEFAULT_EXECUTABLE_NAME);
      clean_cli_args(&cli);
      return 1;

    default:
      fprintf(stderr, "[FAIL] Unexpected CLI parse result status: %d\n", cli_status);
      return 1;
  }

  // CLI settings override the environment. Without source mapping, the
  // highlighter configuration has no effect and need not be resolved.
  SourceHighlightMode source_highlight = SOURCE_HIGHLIGHT_NONE;
  const char *source_highlight_sh = NULL;
  if (cli.with_source_map) {
    const char *mode = cli.source_highlight;
    if (mode == NULL) mode = getenv("LISPICO_SOURCE_HIGHLIGHT");
    if (mode == NULL || mode[0] == '\0') mode = "bat";

    if (!parse_source_highlight_mode(mode, &source_highlight)) {
      fprintf(stderr, "[FAIL] Invalid source highlighter mode “%s”\n", mode);
      clean_cli_args(&cli);
      return 1;
    }

    source_highlight_sh = cli.source_highlight_sh;
    if (source_highlight_sh == NULL)
      source_highlight_sh = getenv("LISPICO_SOURCE_HIGHLIGHT_SH");

    if (
      source_highlight == SOURCE_HIGHLIGHT_SH &&
      (source_highlight_sh == NULL || source_highlight_sh[0] == '\0')
    ) {
      fprintf(stderr, "[FAIL] Source highlighter sh mode requires a shell command\n");
      clean_cli_args(&cli);
      return 1;
    }
  }

  // Match Lispico's host-owned, fixed-capacity interpreter storage.
  static Expression expressions[EXPR_CAPACITY] = {0};
  static ExpressionSourceLocation expression_source_locations[EXPR_CAPACITY] = {0};
  static ExpressionAllocation expression_allocations[EXPR_ALLOCATION_CAPACITY] = {0};
  static Symbol symbols[SYMBOL_CAPACITY] = {0};
  static EvalRuntimeAllocation runtime_allocations[EVAL_RUNTIME_ALLOCATION_CAPACITY] = {0};
  static uint32_t deferred_expressions[EVAL_DEFER_CAPACITY] = {0};

  InterpreterStorage storage = {
    .expressions = expressions,
    .expression_capacity = EXPR_CAPACITY,

    .expression_source_locations =
      cli.with_source_map ? expression_source_locations : NULL,
    .expression_source_location_capacity =
      cli.with_source_map ? EXPR_CAPACITY : 0,

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
    clean_cli_args(&cli);
    return 1;
  }

  int exit_code = 0;

  Array argv_array = {
    .element_type = TYPE_STRING,
    .length = (uint32_t)argc,
    .data = malloc(sizeof(String) * (size_t)argc),
  };
  Array args_array = {
    .element_type = TYPE_STRING,
    .length = (uint32_t)(argc - cli.raw_args_start),
    .data = NULL,
  };

  if (argc > 0 && argv_array.data == NULL) goto malloc_fail;
  for (int i = 0; i < argc; ++i) {
    String arg = { .data = argv[i], .length = strlen(argv[i]) };
    ((String*) argv_array.data)[i] = arg;
  }

  if (args_array.length > 0) {
    args_array.data = malloc(sizeof(String) * args_array.length);
    if (args_array.data == NULL) goto malloc_fail;
    for (uint32_t i = 0; i < args_array.length; ++i) {
      const char *arg = argv[cli.raw_args_start + (int)i];
      ((String*) args_array.data)[i] = (String) { .data = arg, .length = strlen(arg) };
    }
  }

  SettingsState state = {
    .argc = argc,
    .argv = argv_array,
    .args = args_array,
    .cli = &cli,
  };

  static char source[SOURCE_CAPACITY] = {0};
  size_t source_length = 0;

  if (!read_file_contents(cli.source_file, &source, &source_length)) {
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
      if (!cli.print_result) goto done;
      const char *shown_symbol_kind = show_SymbolKind(outcome.result.kind);

      switch (outcome.result.kind) {
        case SYM_EMPTY:
          printf("%s\n", shown_symbol_kind);
          goto done;

        case SYM_VARIABLE: {
          char *shown = show_Value(outcome.result.as.value);
          if (shown == NULL) {
            fprintf(stderr, "[FAIL] Unable to format result\n");
            exit_code = 1;
            goto done;
          }
          printf("%s(%s)\n", shown_symbol_kind, shown);
          free(shown);
          goto done;
        }

        case SYM_ARRAY: {
          char *shown = show_Array(outcome.result.as.array);
          if (shown == NULL) {
            fprintf(stderr, "[FAIL] Unable to format result\n");
            exit_code = 1;
            goto done;
          }
          printf("%s(%s)\n", shown_symbol_kind, shown);
          free(shown);
          goto done;
        }

        default:
          fprintf(stderr, "[FAIL] Unexpected result kind: %d\n", outcome.result.kind);
          exit_code = 1;
          goto done;
      }

    case EVAL_FAIL:
      if (outcome.failure_expression_index != EXPR_NONE) {
        const ExpressionSourceLocation *location =
          get_expression_source_location(&interpreter, outcome.failure_expression_index);
        if (location != NULL) {
          fprintf(
            stderr,
            "[FAIL] Evaluation failed: Line %" PRIu32 " Column %" PRIu32 ": %s\n",
            location->line,
            location->column,
            outcome.as.fail_msg
          );
          print_source_highlight(
            stderr,
            source,
            source_length,
            location,
            cli.source_context_before,
            "confetti",
            source_highlight,
            source_highlight_sh
          );
        } else {
          fprintf(
            stderr,
            "[FAIL] Evaluation failed: Expression Index %" PRIu32 ": %s\n",
            outcome.failure_expression_index,
            outcome.as.fail_msg
          );
        }
      } else {
        fprintf(stderr, "[FAIL] Evaluation failed: %s\n", outcome.as.fail_msg);
      }
      exit_code = 1;
      goto done;

    default:
      fprintf(stderr, "[FAIL] Unexpected evaluation outcome: %d\n", outcome.kind);
      exit_code = 1;
      goto done;
  }

  malloc_fail:
    fprintf(stderr, "[FAIL] malloc failed (OOM?)\n");
    exit_code = 1;
    goto done;

  done:
    clean_evaluation(&interpreter);
    clean_symbols(&interpreter);
    clean_expressions(&interpreter);
    free(argv_array.data);
    free(args_array.data);
    clean_cli_args(&cli);
    return exit_code;
}
