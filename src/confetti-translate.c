#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../confetti/confetti.h"
#include "../lispico/src/expression.h"
#include "../lispico/src/number.h"
#include "confetti-translate.h"

typedef struct {
  Interpreter *interpreter;
  const char *source;
  size_t source_length;
  char (*error)[ERR_MSG_SIZE];
} ConfettiTranslator;

static bool new_expression(
  ConfettiTranslator *translator,
  ExpressionKind kind,
  uint32_t *index
) {
  Interpreter *interpreter = translator->interpreter;
  if (interpreter->expression_count >= interpreter->storage.expression_capacity) {
    snprintf(*translator->error, ERR_MSG_SIZE, "Expression capacity exceeded");
    return false;
  }

  *index = interpreter->expression_count;
  interpreter->storage.expressions[*index] = (Expression) {
    .kind = kind,
    .first_child = EXPR_NONE,
    .next = EXPR_NONE,
  };
  interpreter->expression_count++;
  return true;
}

// Store immutable expression text. Reuse identical values, including embedded NUL bytes.
static bool store_text(
  ConfettiTranslator *translator,
  const char *text,
  size_t length,
  uint32_t *allocation_index
) {
  if (length >= EXPR_TEXT_SIZE || length > UINT32_MAX) {
    snprintf(*translator->error, ERR_MSG_SIZE, "Expression text is too long");
    return false;
  }

  Interpreter *interpreter = translator->interpreter;

  // Try to find existing allocation first to re-use it.
  for (uint32_t i = 0; i < interpreter->expression_allocation_count; ++i) {
    const ExpressionAllocation *allocation = &interpreter->storage.expression_allocations[i];
    if (
      allocation->data != NULL &&
      allocation->element_type == TYPE_STRING &&
      allocation->length == length &&
      allocation->size == length + 1 &&
      memcmp(allocation->data, text, length + 1) == 0
    ) {
      *allocation_index = i;
      return true;
    }
  }

  // Allocate a new string.
  if (!allocate_expression_data(
    interpreter, text, length + 1, TYPE_STRING, (uint32_t)length, allocation_index
  )) {
    snprintf(*translator->error, ERR_MSG_SIZE, "Unable to allocate expression text");
    return false;
  }
  return true;
}

// Allocate new EXPR_NAME expression entry.
static bool new_name(ConfettiTranslator *translator, const char *name, uint32_t *index) {
  if (!new_expression(translator, EXPR_NAME, index)) return false;
  return store_text(
    translator,
    name,
    strlen(name),
    &translator->interpreter->storage.expressions[*index].data
  );
}

// Confetti removes its own escape layer first. Decode surviving backslashes
// according to Lispico’s string rules, preserving the explicit byte length.
static bool translate_string_value(
  ConfettiTranslator *translator,
  const char *value,
  size_t value_length,
  size_t byte_offset,
  uint32_t *index
) {
  if (value_length >= EXPR_TEXT_SIZE) {
    snprintf(*translator->error, ERR_MSG_SIZE, "String literal is too long");
    return false;
  }

  char text[EXPR_TEXT_SIZE] = {'\0'};
  size_t length = 0;

  for (size_t i = 0; i < value_length; ++i) {
    char c = value[i];
    // Detected escaping sequence.
    if (c == '\\') {
      ++i; // Advance one char forward to ignore escaping backslash
      if (i == value_length) {
        snprintf(
          *translator->error,
          ERR_MSG_SIZE,
          "Unterminated Lispico string escape in argument starting at byte %zu",
          byte_offset
        );
        return false;
      }
      // Recognized Lispico escapes.
      switch (value[i]) {
        case '"': c = '"'; break;
        case '\\': c = '\\'; break; // Escaping backslash as regular text
        case 'n': c = '\n'; break;
        case 'r': c = '\r'; break;
        case 't': c = '\t'; break;
        case '0': c = '\0'; break;
        // Unsupported escaped char
        default:
          snprintf(
            *translator->error,
            ERR_MSG_SIZE,
            "Unknown Lispico string escape “\\%c” in argument starting at byte %zu",
            value[i], byte_offset
          );
          return false;
      }
    }
    text[length] = c;
    length++;
  }
  text[length] = '\0';

  if (!new_expression(translator, EXPR_STRING, index)) return false;
  return store_text(
    translator,
    text,
    length,
    &translator->interpreter->storage.expressions[*index].data
  );
}

// Only Confetti’s own argument boundaries are used here: no expression tokenizer.
static bool translate_bare_atom(
  ConfettiTranslator *translator,
  const char *text,
  size_t length,
  size_t byte_offset,
  uint32_t *index
) {
  String atom = { .data = text, .length = length };

  // Parse a numeric literal.
  if (is_number_start(atom)) {
    Value number = {0};
    NumberParseResult parsed = parse_number_text(atom, TYPE_VOID, &number);
    if (parsed != NUMBER_PARSE_OK) {
      const char *problem = "unknown numeric parsing error";
      switch (parsed) {
        case NUMBER_PARSE_INVALID: problem = "invalid numeric literal"; break;
        case NUMBER_PARSE_INVALID_EXPONENT: problem = "invalid numeric exponent"; break;
        case NUMBER_PARSE_OUT_OF_RANGE: problem = "numeric literal out of range"; break;
        case NUMBER_PARSE_TOO_LONG: problem = "numeric literal is too long"; break;
        default: break;
      }
      snprintf(*translator->error, ERR_MSG_SIZE, "%s at byte %zu", problem, byte_offset);
      return false;
    }

    ExpressionKind kind = EXPR_U32;
    uint32_t data = 0;
    switch (number.type) {
      case TYPE_U32: data = number.as.u32; break;
      case TYPE_I32:
        kind = EXPR_I32;
        memcpy(&data, &number.as.i32, sizeof(data));
        break;
      case TYPE_F32:
        kind = EXPR_F32;
        memcpy(&data, &number.as.f32, sizeof(data));
        break;
      default:
        snprintf(*translator->error, ERR_MSG_SIZE, "Unsupported numeric type");
        return false;
    }

    if (!new_expression(translator, kind, index)) return false;
    translator->interpreter->storage.expressions[*index].data = data;
    return true;
  }

  // Parse textual entry.
  if (length == 0 || length >= SYMBOL_NAME_SIZE) {
    snprintf(*translator->error, ERR_MSG_SIZE, "Invalid or too long symbol name");
    return false;
  }

  char name[SYMBOL_NAME_SIZE] = {'\0'};
  memcpy(name, text, length);
  if (!new_expression(translator, EXPR_NAME, index)) return false;
  return store_text(
    translator,
    name,
    length,
    &translator->interpreter->storage.expressions[*index].data
  );
}

static bool translate_argument(
  ConfettiTranslator *translator,
  const conf_argument *argument,
  uint32_t *index
) {
  if (argument == NULL || argument->value == NULL || argument->is_expression) {
    snprintf(*translator->error, ERR_MSG_SIZE, "Invalid Confetti argument");
    return false;
  }
  if (argument->lexeme_offset >= translator->source_length) {
    snprintf(*translator->error, ERR_MSG_SIZE, "Invalid Confetti argument offset");
    return false;
  }

  const char *text = argument->value;
  size_t length = strlen(text);

  // Detect a quoted string (which translates to Lispico string type).
  // Confetti by itself does not provide this information, so inspecting
  // original source `lexeme_offset` value to see the quotation mark.
  if (translator->source[argument->lexeme_offset] == '"') {
    return translate_string_value(
      translator, text, length, argument->lexeme_offset, index
    );
  } else {
    return translate_bare_atom(
      translator, text, length, argument->lexeme_offset, index
    );
  }
}

// Check whether a Confetti argument is an unquoted literal matching `name`.
// Inspect the original source to distinguish bare arguments from quoted strings,
// since Confetti removes quotes when producing argument values.
// Expression arguments are excluded, even if their contents match `name`.
static bool is_bare_argument(
  const ConfettiTranslator *translator,
  const conf_argument *argument,
  const char *name
) {
  return (
    argument != NULL && argument->value != NULL && !argument->is_expression &&
    argument->lexeme_offset < translator->source_length &&
    translator->source[argument->lexeme_offset] != '"' &&
    strcmp(argument->value, name) == 0
  );
}

static void append_child(
  Interpreter *interpreter,
  uint32_t parent_index,
  uint32_t *previous_child,
  uint32_t child_index
) {
  if (*previous_child == EXPR_NONE)
    interpreter->storage.expressions[parent_index].first_child = child_index;
  else
    interpreter->storage.expressions[*previous_child].next = child_index;
  *previous_child = child_index;
}

// Detect an operator argument
static bool is_operator(const char *name) {
  return (
    strcmp(name, "+") == 0 || strcmp(name, "-") == 0 ||
    strcmp(name, "*") == 0 || strcmp(name, "/") == 0 ||
    strcmp(name, "<") == 0 || strcmp(name, "<=") == 0 ||
    strcmp(name, ">") == 0 || strcmp(name, ">=") == 0 ||
    strcmp(name, "==") == 0 || strcmp(name, "!=") == 0
  );
}

// Detect a Lispico chainable operator
// (e.g. `1 + 2 + 3` can be chained as `(+ 1 2 3)` in Lispico).
static bool is_chainable_operator(const char *name) {
  return (
    strcmp(name, "+") == 0 || strcmp(name, "-") == 0 ||
    strcmp(name, "*") == 0 || strcmp(name, "/") == 0
  );
}

// An infix expression is one operator repeated between operands, never mixed
// operators or implicit precedence. Confetti already separates each argument.
typedef struct {
  ConfettiTranslator *translator;
  const char *operator_name;
  uint32_t expression;
  uint32_t previous_child;
  size_t operands;
} InfixCall;

static bool start_infix_call(
  InfixCall *call,
  ConfettiTranslator *translator,
  const char *operator_name
) {
  call->translator = translator;
  call->operator_name = operator_name;
  call->previous_child = EXPR_NONE;
  call->operands = 0;

  // Allocate the call before its operands. Lispico starts evaluating at
  // expression index 0, so the first top-level expression must own that slot.
  if (!new_expression(translator, EXPR_LIST, &call->expression)) return false;

  uint32_t name = EXPR_NONE;
  if (!new_name(translator, operator_name, &name)) return false;
  append_child(translator->interpreter, call->expression, &call->previous_child, name);
  return true;
}

static bool append_infix_operand(InfixCall *call, uint32_t operand) {
  ConfettiTranslator *translator = call->translator;
  if (call->operands >= 2 && !is_chainable_operator(call->operator_name)) {
    snprintf(
      *translator->error,
      ERR_MSG_SIZE,
      "Operator “%s” cannot be chained; use nested { … } groups",
      call->operator_name
    );
    return false;
  }

  // Lispico limits arguments per call. Move the completed call into a new
  // inner expression, then reuse its original index for the enclosing call.
  // Keeping the original index is essential when this is the first top-level
  // expression: the evaluator always starts with expression index 0.
  if (call->operands == EVAL_ARG_CAPACITY) {
    uint32_t inner_index = EXPR_NONE;
    if (!new_expression(translator, EXPR_LIST, &inner_index)) return false;

    Interpreter *interpreter = translator->interpreter;
    Expression *expressions = interpreter->storage.expressions;
    expressions[inner_index] = expressions[call->expression];
    expressions[inner_index].next = EXPR_NONE;
    expressions[call->expression] = (Expression) {
      .kind = EXPR_LIST,
      .first_child = EXPR_NONE,
      .next = EXPR_NONE,
    };

    call->previous_child = EXPR_NONE;
    uint32_t operator_index = EXPR_NONE;
    if (!new_name(translator, call->operator_name, &operator_index)) return false;
    append_child(interpreter, call->expression, &call->previous_child, operator_index);
    append_child(interpreter, call->expression, &call->previous_child, inner_index);
    call->operands = 1;
  }

  append_child(translator->interpreter, call->expression, &call->previous_child, operand);
  call->operands++;
  return true;
}

static bool check_infix_operator(
  ConfettiTranslator *translator,
  const conf_argument *argument,
  const char *expected
) {
  if (!is_bare_argument(translator, argument, expected)) {
    snprintf(
      *translator->error,
      ERR_MSG_SIZE,
      "Mixed or unsupported infix operators; use nested { … } groups"
    );
    return false;
  }
  return true;
}

// Confetti does not expose a distinct flag for an empty block. Detect the
// opening brace after the directive’s final argument, ignoring whitespace and
// Confetti comments, so `do {}` works without adding a custom parser.
static bool has_directive_block(
  const ConfettiTranslator *translator,
  const conf_directive *directive
) {
  if (conf_get_directive_count(directive) != 0) return true;

  long count = conf_get_argument_count(directive);
  if (count <= 0) return false;
  const conf_argument *last = conf_get_argument(directive, count - 1);
  if (
    last == NULL || last->lexeme_offset > translator->source_length ||
    last->lexeme_length > translator->source_length - last->lexeme_offset
  ) return false;

  size_t pos = last->lexeme_offset + last->lexeme_length;
  while (pos < translator->source_length) {
    char c = translator->source[pos];
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f') {
      pos++;
      continue;
    }
    if (c == '#') {
      while (pos < translator->source_length && translator->source[pos] != '\n') pos++;
      continue;
    }
    return c == '{';
  }
  return false;
}

static bool translate_directive(
  ConfettiTranslator *translator,
  const conf_directive *directive,
  uint32_t *index
);

static bool translate_single_block(
  ConfettiTranslator *translator,
  const conf_directive *directive,
  uint32_t *index
);

// An operator continuation follows a `value` directive as another Confetti
// subdirective. For example, `value {gc} == 0` is two native AST nodes.
static bool append_operator_continuation(
  ConfettiTranslator *translator,
  const conf_directive *directive,
  InfixCall *call
) {
  long argument_count = conf_get_argument_count(directive);
  bool has_block = has_directive_block(translator, directive);
  if (argument_count + (has_block ? 1 : 0) < 2) {
    snprintf(*translator->error, ERR_MSG_SIZE, "Operator continuation needs an operand");
    return false;
  }
  if (((size_t)argument_count + (has_block ? 1u : 0u)) % 2 != 0) {
    snprintf(*translator->error, ERR_MSG_SIZE, "Operator continuation is missing an operand");
    return false;
  }

  for (long i = 0; i < argument_count; i += 2) {
    const conf_argument *operator_arg = conf_get_argument(directive, i);
    if (!check_infix_operator(translator, operator_arg, call->operator_name)) return false;

    uint32_t operand = EXPR_NONE;
    if (i + 1 < argument_count) {
      if (!translate_argument(translator, conf_get_argument(directive, i + 1), &operand))
        return false;
    } else if (!translate_single_block(translator, directive, &operand)) {
      return false;
    }
    if (!append_infix_operand(call, operand)) return false;
  }
  return true;
}

// A plain block normally contains one directive. A `value` followed by one or
// more operator continuations is another single expression, assembled from
// neighboring AST directives without interpreting any source substring.
static bool translate_single_block(
  ConfettiTranslator *translator,
  const conf_directive *directive,
  uint32_t *index
) {
  long count = conf_get_directive_count(directive);
  if (count == 0) {
    snprintf(*translator->error, ERR_MSG_SIZE, "Empty expression block; use `do {}`");
    return false;
  }
  if (count == 1)
    return translate_directive(translator, conf_get_directive(directive, 0), index);

  const conf_directive *first = conf_get_directive(directive, 0);
  if (!is_bare_argument(translator, conf_get_argument(first, 0), "value")) {
    snprintf(
      *translator->error, ERR_MSG_SIZE,
      "Block contains %ld directives; use `do { … }` for a sequence, or `value` for an operator continuation",
      count
    );
    return false;
  }

  const conf_directive *continuation = conf_get_directive(directive, 1);
  const conf_argument *operator_arg = conf_get_argument(continuation, 0);
  if (
    operator_arg == NULL || operator_arg->value == NULL ||
    !is_operator(operator_arg->value) ||
    !is_bare_argument(translator, operator_arg, operator_arg->value)
  ) {
    snprintf(
      *translator->error, ERR_MSG_SIZE,
      "Expected an operator continuation after `value`; use `do { … }` for a sequence"
    );
    return false;
  }

  InfixCall call;
  if (!start_infix_call(&call, translator, operator_arg->value)) return false;

  uint32_t first_operand = EXPR_NONE;
  if (!translate_directive(translator, first, &first_operand)) return false;
  if (!append_infix_operand(&call, first_operand)) return false;

  for (long i = 1; i < count; ++i) {
    if (!append_operator_continuation(translator, conf_get_directive(directive, i), &call))
      return false;
  }
  *index = call.expression;
  return true;
}

static bool translate_sequence_block(
  ConfettiTranslator *translator,
  const conf_directive *directive,
  uint32_t *index
) {
  if (!new_expression(translator, EXPR_LIST, index)) return false;
  uint32_t previous = EXPR_NONE;
  uint32_t name = EXPR_NONE;
  if (!new_name(translator, "do", &name)) return false;
  append_child(translator->interpreter, *index, &previous, name);

  long count = conf_get_directive_count(directive);
  for (long i = 0; i < count; ++i) {
    uint32_t child = EXPR_NONE;
    if (!translate_directive(translator, conf_get_directive(directive, i), &child))
      return false;
    append_child(translator->interpreter, *index, &previous, child);
  }
  return true;
}

// Translate `value atom` or `value {call}` to the value itself, without an
// extra Lispico function call. This also works as the last item in `(do)`.
static bool translate_value_directive(
  ConfettiTranslator *translator,
  const conf_directive *directive,
  uint32_t *index
) {
  long count = conf_get_argument_count(directive);
  bool has_block = has_directive_block(translator, directive);
  if (count == 2 && !has_block)
    return translate_argument(translator, conf_get_argument(directive, 1), index);
  if (count == 1 && has_block)
    return translate_single_block(translator, directive, index);

  snprintf(
    *translator->error, ERR_MSG_SIZE,
    "`value` expects one argument or one expression block"
  );
  return false;
}

// Translate a homogeneous infix directive like `1 + 2 + 3`, optionally using
// its trailing native Confetti block as the final operand: `1 + { 2 * 3 }`.
static bool translate_infix_directive(
  ConfettiTranslator *translator,
  const conf_directive *directive,
  const char *operator_name,
  uint32_t *index
) {
  long argument_count = conf_get_argument_count(directive);
  bool has_block = has_directive_block(translator, directive);
  long total = argument_count + (has_block ? 1 : 0);
  if (total < 3 || total % 2 == 0) {
    snprintf(*translator->error, ERR_MSG_SIZE, "Infix operator is missing an operand");
    return false;
  }

  InfixCall call;
  if (!start_infix_call(&call, translator, operator_name)) return false;

  uint32_t first = EXPR_NONE;
  if (!translate_argument(translator, conf_get_argument(directive, 0), &first)) return false;
  if (!append_infix_operand(&call, first)) return false;

  for (long i = 1; i < total; i += 2) {
    if (!check_infix_operator(
      translator, conf_get_argument(directive, i), operator_name
    )) return false;

    uint32_t operand = EXPR_NONE;
    if (i + 1 < argument_count) {
      if (!translate_argument(translator, conf_get_argument(directive, i + 1), &operand))
        return false;
    } else if (!translate_single_block(translator, directive, &operand)) {
      return false;
    }
    if (!append_infix_operand(&call, operand)) return false;
  }

  *index = call.expression;
  return true;
}

static bool translate_directive(
  ConfettiTranslator *translator,
  const conf_directive *directive,
  uint32_t *index
) {
  long count = conf_get_argument_count(directive);
  if (count <= 0) {
    snprintf(*translator->error, ERR_MSG_SIZE, "Confetti directive has no arguments");
    return false;
  }

  const conf_argument *first = conf_get_argument(directive, 0);
  bool has_block = has_directive_block(translator, directive);

  if (is_bare_argument(translator, first, "value"))
    return translate_value_directive(translator, directive, index);

  // Standalone `do` consumes all of its subdirectives as a single sequence.
  if (count == 1 && has_block && is_bare_argument(translator, first, "do"))
    return translate_sequence_block(translator, directive, index);

  if (count > 1) {
    const conf_argument *second = conf_get_argument(directive, 1);
    if (
      second != NULL && second->value != NULL &&
      is_operator(second->value) &&
      is_bare_argument(translator, second, second->value)
    ) return translate_infix_directive(translator, directive, second->value, index);
  }

  // Ordinary Confetti directives call the named Lispico function. Prefix
  // operators (`+ 1 2`, `+ 1 2 3`) remain ordinary function calls.
  if (!new_expression(translator, EXPR_LIST, index)) return false;
  uint32_t previous = EXPR_NONE;
  long argument_count = count;
  bool explicit_do = false;
  if (has_block && count > 1) {
    explicit_do = is_bare_argument(
      translator, conf_get_argument(directive, count - 1), "do"
    );
    if (explicit_do) argument_count--;
  }

  for (long i = 0; i < argument_count; ++i) {
    uint32_t argument = EXPR_NONE;
    if (!translate_argument(translator, conf_get_argument(directive, i), &argument))
      return false;
    append_child(translator->interpreter, *index, &previous, argument);
  }

  uint32_t function_name = translator->interpreter->storage.expressions[*index].first_child;
  if (translator->interpreter->storage.expressions[function_name].kind != EXPR_NAME) {
    snprintf(*translator->error, ERR_MSG_SIZE, "Expected a function name at directive start");
    return false;
  }

  if (has_block) {
    uint32_t argument = EXPR_NONE;
    if (explicit_do) {
      if (!translate_sequence_block(translator, directive, &argument)) return false;
    } else {
      if (!translate_single_block(translator, directive, &argument)) return false;
    }
    append_child(translator->interpreter, *index, &previous, argument);
  }

  return true;
}

bool parse_confetti_source(
  Interpreter *interpreter,
  const char *source,
  size_t length,
  char (*error)[ERR_MSG_SIZE]
) {
  if (error == NULL) return false;
  (*error)[0] = '\0';
  if (interpreter == NULL || !interpreter->configured || source == NULL) {
    snprintf(*error, ERR_MSG_SIZE, "Interpreter or source is not initialized");
    return false;
  }

  clean_expressions(interpreter);
  if (memchr(source, '\0', length) != NULL) {
    snprintf(*error, ERR_MSG_SIZE, "Source contains a NUL byte");
    return false;
  }

  // Use only Confetti’s native directive and subdirective AST. In particular,
  // do not enable Annex B’s opaque parenthesized-expression arguments.
  conf_error parse_error = {0};
  conf_unit *unit = conf_parse(source, NULL, &parse_error);
  if (unit == NULL) {
    snprintf(
      *error, ERR_MSG_SIZE, "Confetti parse error at byte %zu: %s",
      parse_error.where, parse_error.description
    );
    return false;
  }

  ConfettiTranslator translator = {
    .interpreter = interpreter,
    .source = source,
    .source_length = length,
    .error = error,
  };

  const conf_directive *root = conf_get_root(unit);
  long count = conf_get_directive_count(root);
  uint32_t previous = EXPR_NONE;
  for (long i = 0; i < count; ++i) {
    uint32_t expression = EXPR_NONE;
    if (!translate_directive(&translator, conf_get_directive(root, i), &expression))
      goto fail;
    if (previous != EXPR_NONE)
      interpreter->storage.expressions[previous].next = expression;
    previous = expression;
  }

  conf_free(unit);
  return true;

  fail:
    conf_free(unit);
    clean_expressions(interpreter);
    return false;
}
