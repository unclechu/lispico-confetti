#include <assert.h>
#include <stdio.h>
#include <string.h>

// Exercise the actual settings callback while keeping the desktop main entry
// point out of this test. Confetti’s parser and translation are tested
// separately by the existing integration tests.
#define main confetti_interpreter_main
#include "../../interpreter.c"
#undef main

#include "../../lispico/src/stdlib-immutable-refs.h"

static char reported_error[ERR_MSG_SIZE] = {'\0'};

static Symbol *test_define_symbol(
  const EvalSettingsInterface *interface,
  const char *name,
  SymbolKind kind
) {
  return symbol_define(interface->interpreter, name, kind);
}

static Symbol *test_symbol_lookup(
  const EvalSettingsInterface *interface,
  const char *name
) {
  return symbol_lookup(interface->interpreter, name);
}

static void test_report_failure(
  const EvalSettingsInterface *interface,
  const char *message
) {
  (void)interface;
  snprintf(reported_error, sizeof(reported_error), "%s", message);
}

static void check_borrowed_string(String value, const char *source) {
  assert(value.data == source);
  assert(value.base == source);
  assert(value.length == strlen(source));
  assert(value.immutable_ref);
  assert(value.nul_terminated);
}

int main(void) {
  static Expression expressions[16] = {0};
  static ExpressionAllocation expression_allocations[16] = {0};
  static Symbol symbols[256] = {0};
  static EvalRuntimeAllocation runtime_allocations[16] = {0};
  static uint32_t deferred_indices[16] = {0};

  InterpreterStorage storage = {
    .expressions = expressions,
    .expression_capacity = 16,
    .expression_allocations = expression_allocations,
    .expression_allocation_capacity = 16,
    .symbols = symbols,
    .symbol_capacity = 256,
    .runtime_allocations = runtime_allocations,
    .runtime_allocation_capacity = 16,
    .deferred_expression_indices = deferred_indices,
    .deferred_expression_capacity = 16,
  };
  Interpreter interpreter = {0};
  assert(configure_interpreter_storage(&interpreter, &storage));

  char *arguments[] = {
    "lispico-confetti", "--var", "message", "s:hello", "--freeze", "message",
    "--arr", "labels", "s", "2", "one", "two",
    "--arr-set", "labels", "1", "updated",
    "--arr-new", "empty-strings", "s", "1",
    "--arr", "numbers", "u32", "2", "11", "22",
    "--var", "counter", "u32:7", "--", "script-arg",
  };
  const int argc = (int)(sizeof(arguments) / sizeof(arguments[0]));
  CliOptions cli = {0};
  char parse_error[ERR_MSG_SIZE] = {'\0'};
  assert(parse_cli_args(argc, arguments, &cli, parse_error, sizeof(parse_error)) == CLI_PARSE_OK);
  assert(cli.definition_count == 5);
  assert(cli.raw_args_start == argc - 1);

  String argument_items[2] = {
    {
      .data = arguments[0],
      .base = arguments[0],
      .length = strlen(arguments[0]),
      .immutable_ref = true,
      .nul_terminated = true,
    },
    {
      .data = arguments[argc - 1],
      .base = arguments[argc - 1],
      .length = strlen(arguments[argc - 1]),
      .immutable_ref = true,
      .nul_terminated = true,
    },
  };
  Array argv_array = {
    .element_type = TYPE_STRING,
    .length = 2,
    .data = argument_items,
    .base = argument_items,
    .immutable_ref = true,
  };
  Array args_array = {
    .element_type = TYPE_STRING,
    .length = 1,
    .data = argument_items + 1,
    .base = argument_items + 1,
    .immutable_ref = true,
  };
  SettingsState state = {
    .argc = argc,
    .argv = argv_array,
    .args = args_array,
    .cli = &cli,
  };
  EvalSettingsInterface interface = {
    .state = &state,
    .interpreter = &interpreter,
    .define_symbol = test_define_symbol,
    .symbol_lookup = test_symbol_lookup,
    .report_failure = test_report_failure,
  };

  eval_settings_cb(&interface);
  assert(reported_error[0] == '\0');
  assert(interpreter.runtime_allocation_count == 0);

  const Symbol *message = symbol_lookup(&interpreter, "message");
  assert(message != NULL && message->kind == SYM_VARIABLE);
  assert(message->frozen);
  check_borrowed_string(message->as.variable.as.string, arguments[3] + 2);

  const Symbol *labels = symbol_lookup(&interpreter, "labels");
  assert(labels != NULL && labels->kind == SYM_ARRAY);
  assert(!labels->frozen);
  assert(labels->as.array.immutable_ref);
  assert(labels->as.array.element_type == TYPE_STRING);
  assert(labels->as.array.data == cli.definitions[1].as.array.data);
  assert(labels->as.array.base == cli.definitions[1].as.array.base);
  const String *label_items = labels->as.array.data;
  check_borrowed_string(label_items[0], arguments[10]);
  check_borrowed_string(label_items[1], arguments[15]);

  const Symbol *empty = symbol_lookup(&interpreter, "empty-strings");
  assert(empty != NULL && empty->kind == SYM_ARRAY);
  assert(empty->as.array.immutable_ref);
  const String *empty_items = empty->as.array.data;
  assert(empty_items[0].data == NULL);
  assert(empty_items[0].base == NULL);
  assert(empty_items[0].length == 0);
  assert(empty_items[0].immutable_ref);

  const Symbol *numbers = symbol_lookup(&interpreter, "numbers");
  assert(numbers != NULL && numbers->kind == SYM_ARRAY);
  assert(numbers->as.array.immutable_ref);
  assert(numbers->as.array.element_type == TYPE_U32);
  assert(numbers->as.array.data == cli.definitions[3].as.array.data);
  assert(((const uint32_t *)numbers->as.array.data)[0] == 11);
  assert(((const uint32_t *)numbers->as.array.data)[1] == 22);

  const Symbol *counter = symbol_lookup(&interpreter, "counter");
  assert(counter != NULL && counter->kind == SYM_VARIABLE);
  assert(counter->as.variable.type == TYPE_U32);
  assert(counter->as.variable.as.u32 == 7);
  assert(!counter->frozen);

  const Symbol *argv_symbol = symbol_lookup(&interpreter, "argv");
  const Symbol *args_symbol = symbol_lookup(&interpreter, "args");
  assert(argv_symbol != NULL && args_symbol != NULL);
  assert(argv_symbol->as.array.data == argv_array.data);
  assert(args_symbol->as.array.data == args_array.data);
  assert(argv_symbol->as.array.immutable_ref);
  assert(args_symbol->as.array.immutable_ref);
  const String *script_args = args_symbol->as.array.data;
  check_borrowed_string(script_args[0], arguments[argc - 1]);
  assert(interpreter.runtime_allocation_count == 0);

  // `--freeze` protects the name binding; immutable references remain
  // independent of the symbol’s frozen state.
  assert(message->frozen && !labels->frozen);

  // Confirm that Lispico’s public immutable-reference queries recognize the
  // borrowed CLI values, rather than merely inspecting the raw C flags.
  Datum label_view = { .kind = SYM_ARRAY, .as.array = labels->as.array };
  Datum is_immutable = std_is_immutable_ref(&interpreter, &label_view, 1);
  assert(is_immutable.kind == SYM_VARIABLE);
  assert(is_immutable.as.value.as.u32 == 1);
  Datum has_immutable = std_has_immutable_refs(&interpreter, &label_view, 1);
  assert(has_immutable.as.value.as.u32 == 1);

  // Explicit copying still creates independent writable buffers and elements.
  Datum copied = std_copy(&interpreter, &label_view, 1);
  assert(copied.kind == SYM_ARRAY);
  assert(!copied.as.array.immutable_ref);
  assert(copied.as.array.data != labels->as.array.data);
  const String *copied_labels = copied.as.array.data;
  for (uint32_t i = 0; i < copied.as.array.length; ++i) {
    assert(!copied_labels[i].immutable_ref);
    assert(copied_labels[i].data != label_items[i].data);
    assert(copied_labels[i].base == copied_labels[i].data);
    assert(copied_labels[i].nul_terminated);
  }
  assert(interpreter.runtime_allocation_count == 3);

  // CLI-owned arrays must remain alive through interpreter cleanup.
  clean_evaluation(&interpreter);
  assert(strcmp(label_items[0].data, "one") == 0);
  assert(strcmp(label_items[1].data, "updated") == 0);
  clean_symbols(&interpreter);
  clean_cli_args(&cli);
  puts("Confetti immutable CLI argument tests passed");
  return 0;
}
