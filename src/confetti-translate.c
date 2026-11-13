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

  // Cache the last source position while assigning optional source locations.
  size_t location_scan_offset;
  uint32_t location_scan_line;
  uint32_t location_scan_column;
} ConfettiTranslator;

// Confetti’s argument lexeme offsets refer to the original source buffer.
// Advance through that buffer only when a source-map table was supplied.
static void record_source_location(
  ConfettiTranslator *translator,
  uint32_t index,
  size_t byte_offset
) {
  ExpressionSourceLocation *locations =
    translator->interpreter->storage.expression_source_locations;

  if (locations == NULL || byte_offset >= translator->source_length) return;

  if (byte_offset < translator->location_scan_offset) {
    translator->location_scan_offset = 0;
    translator->location_scan_line = 1;
    translator->location_scan_column = 1;
  }

  while (translator->location_scan_offset < byte_offset) {
    char c = translator->source[translator->location_scan_offset];
    if (c == '\n') {
      ++translator->location_scan_line;
      translator->location_scan_column = 1;
    } else {
      ++translator->location_scan_column;
    }
    ++translator->location_scan_offset;
  }

  locations[index] = (ExpressionSourceLocation) {
    .byte_offset = (uint32_t)byte_offset,
    .line = translator->location_scan_line,
    .column = translator->location_scan_column,
  };
}

static size_t argument_offset(const conf_argument *argument) {
  return argument != NULL ? argument->lexeme_offset : 0;
}

static size_t directive_offset(const conf_directive *directive) {
  return argument_offset(conf_get_argument(directive, 0));
}

static bool new_expression(
  ConfettiTranslator *translator,
  ExpressionKind kind,
  size_t byte_offset,
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
  record_source_location(translator, *index, byte_offset);
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
  if (
    !allocate_expression_data(
      interpreter,
      text,
      length + 1,
      TYPE_STRING,
      (uint32_t)length,
      allocation_index
    )
  ) {
    snprintf(*translator->error, ERR_MSG_SIZE, "Unable to allocate expression text");
    return false;
  } else {
    return true;
  }
}

// Allocate new EXPR_NAME expression entry.
static bool new_name(
  ConfettiTranslator *translator,
  const char *name,
  size_t byte_offset,
  uint32_t *index
) {
  if (!new_expression(translator, EXPR_NAME, byte_offset, index)) return false;
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
            value[i],
            byte_offset
          );
          return false;
      }
    }
    text[length] = c;
    length++;
  }
  text[length] = '\0';

  if (!new_expression(translator, EXPR_STRING, byte_offset, index)) return false;
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

    if (!new_expression(translator, kind, byte_offset, index)) return false;
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
  if (!new_expression(translator, EXPR_NAME, byte_offset, index)) return false;
  return store_text(
    translator,
    name,
    length,
    &translator->interpreter->storage.expressions[*index].data
  );
}

// The square brackets and `$` are ordinary Confetti argument characters.
// Recognize only a single, unquoted NAME[INDEX] or $NAME[INDEX] argument.
// Empty [] means its index comes from an adjacent native Confetti block.
typedef struct {
  const char *name;
  size_t name_length;
  const char *index_text;
  size_t index_length;
  bool is_string;
  bool block_index;
  size_t byte_offset;
  size_t index_byte_offset;
} IndexedArgument;

static bool parse_indexed_argument(
  const ConfettiTranslator *translator,
  const conf_argument *argument,
  IndexedArgument *result
) {
  if (
    (argument == NULL || argument->value == NULL || argument->is_expression) ||
    argument->lexeme_offset >= translator->source_length ||
    // Quoted strings/arguments indicating this is a regular string literal
    translator->source[argument->lexeme_offset] == '"'
  ) return false;

  const char *text = argument->value;
  size_t length = strlen(text);
  if (length < 3 || text[length - 1] != ']') return false;

  const char *opening = strchr(text, '[');
  if (
    (opening == NULL || strchr(opening + 1, '[') != NULL) ||
    strchr(opening, ']') != text + length - 1
  ) return false;

  bool is_string = text[0] == '$';
  const char *name = text + (is_string ? 1 : 0);
  size_t name_length = (size_t)(opening - name);
  if (name_length == 0 || name_length >= SYMBOL_NAME_SIZE) return false;

  *result = (IndexedArgument) {
    .name = name,
    .name_length = name_length,
    .index_text = opening + 1,
    .index_length = (size_t)(text + length - 1 - (opening + 1)),
    .is_string = is_string,
    .block_index = opening[1] == ']',
    .byte_offset = argument->lexeme_offset,
    .index_byte_offset = argument->lexeme_offset + (size_t)(opening + 1 - text),
  };

  return true;
}

static bool translate_indexed_operation(
  ConfettiTranslator *translator,
  const IndexedArgument *target,
  const conf_directive *index_block,
  const conf_directive *assignment,
  long assignment_value_start,
  uint32_t *index
);

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
    return translate_string_value(translator, text, length, argument->lexeme_offset, index);
  } else {
    IndexedArgument indexed = {0};
    if (parse_indexed_argument(translator, argument, &indexed)) {
      return translate_indexed_operation(translator, &indexed, NULL, NULL, 0, index);
    } else {
      return translate_bare_atom(translator, text, length, argument->lexeme_offset, index);
    }
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
    (argument != NULL && argument->value != NULL && !argument->is_expression) &&
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
    strcmp(name, "==") == 0 || strcmp(name, "!=") == 0 ||
    strcmp(name, "&&") == 0 || strcmp(name, "||") == 0
  );
}

// Detect a Lispico chainable operator
// (e.g. `1 + 2 + 3` becomes `(+ 1 2 3)` and
// `1 && 0 && 1` becomes `(and 1 0 1)`).
static bool is_chainable_operator(const char *name) {
  return (
    strcmp(name, "+") == 0 || strcmp(name, "-") == 0 ||
    strcmp(name, "*") == 0 || strcmp(name, "/") == 0 ||
    strcmp(name, "&&") == 0 || strcmp(name, "||") == 0
  );
}

// Translate Confetti’s logical operator spelling into Lispico’s existing
// short-circuit special forms. Keep the original name when checking chains
// so mixing `&&` with `||` still produces a translation error.
static const char *lispico_operator_name(const char *name) {
  if (strcmp(name, "&&") == 0) return "and";
  if (strcmp(name, "||") == 0) return "or";
  return name;
}

// An infix expression is one operator repeated between operands, never mixed
// operators or implicit precedence. Confetti already separates each argument.
typedef struct {
  ConfettiTranslator *translator;
  const char *operator_name;
  uint32_t expression;
  uint32_t previous_child;
  size_t operands;
  size_t source_offset;
} InfixCall;

static bool start_infix_call(
  InfixCall *call,
  ConfettiTranslator *translator,
  const char *operator_name,
  size_t byte_offset
) {
  call->translator = translator;
  call->operator_name = operator_name;
  call->previous_child = EXPR_NONE;
  call->operands = 0;
  call->source_offset = byte_offset;

  // Allocate the call before its operands. Lispico starts evaluating at
  // expression index 0, so the first top-level expression must own that slot.
  if (!new_expression(translator, EXPR_LIST, byte_offset, &call->expression)) return false;

  uint32_t name = EXPR_NONE;
  if (!new_name(translator, lispico_operator_name(operator_name), byte_offset, &name)) return false;
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
    if (!new_expression(translator, EXPR_LIST, call->source_offset, &inner_index)) return false;

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
    if (
      !new_name(
        translator,
        lispico_operator_name(call->operator_name),
        call->source_offset,
        &operator_index
      )
    ) return false;
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
    last == NULL ||
    last->lexeme_offset > translator->source_length ||
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

// Some expressions, notably `if` with an adjacent `then` or `else`, span
// multiple sibling Confetti directives. Advance `position` past all of them.
static bool translate_sequence_item(
  ConfettiTranslator *translator,
  const conf_directive *container,
  long *position,
  uint32_t *index
);

// Append all native subdirectives as operands of an infix call. Each
// subdirective is one logical argument, except an `id` continuation or
// conditional chain, which may consume several neighboring directives.
static bool append_infix_block_operands(
  InfixCall *call,
  const conf_directive *directive
) {
  ConfettiTranslator *translator = call->translator;
  long count = conf_get_directive_count(directive);
  if (count == 0) {
    snprintf(*translator->error, ERR_MSG_SIZE, "Infix operator is missing an operand");
    return false;
  }

  for (long position = 0; position < count;) {
    uint32_t operand = EXPR_NONE;
    if (!translate_sequence_item(translator, directive, &position, &operand)) return false;
    if (!append_infix_operand(call, operand)) return false;
  }
  return true;
}

// An operand containing several native arguments or an argument block is a
// function call. A single argument without a block remains an atom, so
// `x == foo` references `foo`, while `x == foo 42` evaluates `(foo 42)`.
// Operators delimit adjacent operands.
static bool translate_infix_argument_span(
  ConfettiTranslator *translator,
  const conf_directive *directive,
  long first,
  long end,
  bool include_block,
  uint32_t *index
) {
  if (end - first == 1 && !include_block)
    return translate_argument(translator, conf_get_argument(directive, first), index);

  if (
    !new_expression(
      translator,
      EXPR_LIST,
      argument_offset(conf_get_argument(directive, first)),
      index
    )
  ) return false;
  uint32_t previous_child = EXPR_NONE;
  for (long i = first; i < end; ++i) {
    uint32_t argument = EXPR_NONE;
    if (!translate_argument(translator, conf_get_argument(directive, i), &argument))
      return false;
    append_child(translator->interpreter, *index, &previous_child, argument);
  }

  if (include_block) {
    long block_count = conf_get_directive_count(directive);
    for (long position = 0; position < block_count;) {
      uint32_t argument = EXPR_NONE;
      if (!translate_sequence_item(translator, directive, &position, &argument))
        return false;
      append_child(translator->interpreter, *index, &previous_child, argument);
    }
  }

  uint32_t name = translator->interpreter->storage.expressions[*index].first_child;
  if (translator->interpreter->storage.expressions[name].kind != EXPR_NAME) {
    snprintf(*translator->error, ERR_MSG_SIZE, "Expected a function name at operand start");
    return false;
  }
  return true;
}

// Construct a Lispico string literal directly from a bare symbol name.
// This must not go through Lispico’s quoted-string escape decoder.
static bool new_symbol_string(
  ConfettiTranslator *translator,
  const char *name,
  size_t length,
  size_t byte_offset,
  uint32_t *index
) {
  if (length == 0 || length >= SYMBOL_NAME_SIZE) {
    snprintf(*translator->error, ERR_MSG_SIZE, "Invalid indexed or assignment symbol name");
    return false;
  }
  if (!new_expression(translator, EXPR_STRING, byte_offset, index)) return false;
  return store_text(
    translator,
    name,
    length,
    &translator->interpreter->storage.expressions[*index].data
  );
}

// Interpret the right-hand side as one expression. A bare atom is a value,
// several inline arguments form a function call, and a native block supplies
// one full expression. This also handles `= str-char "A"` and `= {x + 1}`.
static bool translate_assignment_rhs(
  ConfettiTranslator *translator,
  const conf_directive *directive,
  long first,
  uint32_t *index
) {
  long count = conf_get_argument_count(directive);
  bool has_block = has_directive_block(translator, directive);
  if (first == count && has_block)
    return translate_single_block(translator, directive, index);
  if (first >= count) {
    snprintf(*translator->error, ERR_MSG_SIZE, "Assignment is missing a value");
    return false;
  }
  return translate_infix_argument_span(translator, directive, first, count, has_block, index);
}

// Lower foo[n], $foo[n], foo[] {EXPR}, or their assignment forms to
// (arr-get/arr-set "foo" INDEX [VALUE]) or (str-get/str-set ...).
static bool translate_indexed_operation(
  ConfettiTranslator *translator,
  const IndexedArgument *target,
  const conf_directive *index_block,
  const conf_directive *assignment,
  long assignment_value_start,
  uint32_t *index
) {
  if (target->block_index != (index_block != NULL)) {
    snprintf(
      *translator->error,
      ERR_MSG_SIZE,
      target->block_index
        ? "Indexed access requires a { … } index block"
        : "Indexed access already has an index inside brackets"
    );
    return false;
  }

  if (!new_expression(translator, EXPR_LIST, target->byte_offset, index)) return false;
  uint32_t previous = EXPR_NONE;
  uint32_t child = EXPR_NONE;
  const char *fn_name =
    assignment != NULL
    ? (target->is_string ? "str-set" : "arr-set")
    : (target->is_string ? "str-get" : "arr-get");
  if (!new_name(translator, fn_name, target->byte_offset, &child)) return false;
  append_child(translator->interpreter, *index, &previous, child);

  if (
    !new_symbol_string(
      translator,
      target->name,
      target->name_length,
      target->byte_offset,
      &child
    )
  ) return false;
  append_child(translator->interpreter, *index, &previous, child);

  if (index_block != NULL) {
    if (!translate_single_block(translator, index_block, &child)) return false;
  } else {
    // The index is inside the same unquoted Confetti argument as its name.
    if (
      !translate_bare_atom(
        translator,
        target->index_text,
        target->index_length,
        target->index_byte_offset,
        &child
      )
    ) return false;
  }
  append_child(translator->interpreter, *index, &previous, child);

  if (assignment != NULL) {
    if (
      !translate_assignment_rhs(translator, assignment, assignment_value_start, &child)
    ) return false;
    append_child(translator->interpreter, *index, &previous, child);
  }
  return true;
}

// Lower NAME = VALUE. An indexed left-hand side uses the operation above.
// The left-hand name is data, not a reference to the variable’s current value.
static bool translate_scalar_assignment(
  ConfettiTranslator *translator,
  const conf_directive *directive,
  uint32_t *index
) {
  const conf_argument *name = conf_get_argument(directive, 0);
  if (
    (name == NULL || name->value == NULL || name->is_expression) ||
    name->lexeme_offset >= translator->source_length ||
    translator->source[name->lexeme_offset] == '"'
  ) {
    snprintf(*translator->error, ERR_MSG_SIZE, "Assignment expects a bare symbol name");
    return false;
  }

  if (!new_expression(translator, EXPR_LIST, name->lexeme_offset, index)) return false;
  uint32_t previous = EXPR_NONE;
  uint32_t child = EXPR_NONE;
  if (!new_name(translator, "set", name->lexeme_offset, &child)) return false;
  append_child(translator->interpreter, *index, &previous, child);

  if (
    !new_symbol_string(
      translator,
      name->value,
      strlen(name->value),
      name->lexeme_offset,
      &child
    )
  ) return false;
  append_child(translator->interpreter, *index, &previous, child);

  if (!translate_assignment_rhs(translator, directive, 2, &child)) return false;
  append_child(translator->interpreter, *index, &previous, child);
  return true;
}

// An operator continuation follows a completed expression as another
// Confetti subdirective. For example, `sin {pi / 2.0} - 1.0` is two
// native AST nodes, as is `id {gc} == 0`.
static bool append_operator_continuation(
  ConfettiTranslator *translator,
  const conf_directive *directive,
  InfixCall *call,
  long first_argument
) {
  long argument_count = conf_get_argument_count(directive);
  bool has_block = has_directive_block(translator, directive);
  if (argument_count - first_argument + (has_block ? 1 : 0) < 2) {
    snprintf(*translator->error, ERR_MSG_SIZE, "Operator continuation needs an operand");
    return false;
  }

  for (long i = first_argument; i < argument_count;) {
    const conf_argument *operator_arg = conf_get_argument(directive, i);
    if (!check_infix_operator(translator, operator_arg, call->operator_name)) return false;
    ++i;

    // An explicit `id` before an operand is optional. This is useful for
    // continuations such as `id {gc} == id 0`, where Confetti puts the
    // operator and its right-hand operand in one native directive.
    bool explicit_id =
      i < argument_count &&
      is_bare_argument(translator, conf_get_argument(directive, i), "id") &&
      (i + 1 < argument_count || has_block);
    if (explicit_id) ++i;

    uint32_t operand = EXPR_NONE;
    if (i < argument_count) {
      long end = i + 1;
      if (!explicit_id) {
        const conf_argument *first = conf_get_argument(directive, i);
        if (
          first != NULL &&
          first->value != NULL &&
          is_operator(first->value) &&
          is_bare_argument(translator, first, first->value)
        ) {
          snprintf(*translator->error, ERR_MSG_SIZE, "Infix operator is missing an operand");
          return false;
        }

        // Stop at the next bare operator. Quoted operator spellings remain
        // ordinary function arguments, as represented by Confetti’s AST.
        while (end < argument_count) {
          const conf_argument *next = conf_get_argument(directive, end);
          if (
            next != NULL &&
            next->value != NULL &&
            is_operator(next->value) &&
            is_bare_argument(translator, next, next->value)
          ) break;
          ++end;
        }
      }
      // A trailing block belongs to the final right-hand call and expands
      // its native subdirectives into that call’s arguments. Never discard it.
      bool include_block = end == argument_count && has_block;
      if (include_block && explicit_id) {
        snprintf(
          *translator->error,
          ERR_MSG_SIZE,
          "`id` expects one argument or one expression block"
        );
        return false;
      }
      if (
        !translate_infix_argument_span(translator, directive, i, end, include_block, &operand)
      ) return false;
      i = end;
    } else if (has_block) {
      // The final block may contribute multiple operands to a chainable
      // operator. An explicit `id` requires exactly one expression.
      if (explicit_id) {
        if (!translate_single_block(translator, directive, &operand)) return false;
      } else {
        return append_infix_block_operands(call, directive);
      }
    } else {
      snprintf(*translator->error, ERR_MSG_SIZE, "Operator continuation is missing an operand");
      return false;
    }
    if (!append_infix_operand(call, operand)) return false;
  }
  return true;
}

// A directive beginning with an operator can continue a preceding expression
// when it has operator/operand pairs. An operator without any operand is also
// treated as an incomplete continuation for a useful error. A prefix call
// such as `+ 1 2` remains a separate expression.
static bool begins_operator_continuation(
  ConfettiTranslator *translator,
  const conf_directive *directive
) {
  const conf_argument *first = conf_get_argument(directive, 0);
  if (
    first == NULL ||
    first->value == NULL ||
    !is_operator(first->value) ||
    !is_bare_argument(translator, first, first->value)
  ) return false;

  long count = conf_get_argument_count(directive);
  if (has_directive_block(translator, directive)) count++;
  return (
    count == 1 ||
    (count >= 2 && count % 2 == 0) ||
    (count >= 3 && is_bare_argument(translator, conf_get_argument(directive, 1), "id"))
  );
}

// A block call followed by `- 1.0` or `+ 1 + 2` can supply the left
// operand of an infix expression. Distinguish those operator/operand pairs
// from a standalone prefix call such as `+ 1 2 3` using Confetti’s arguments.
static bool has_inline_operator_pairs(
  ConfettiTranslator *translator,
  const conf_directive *directive
) {
  if (directive == NULL || has_directive_block(translator, directive)) return false;
  long count = conf_get_argument_count(directive);
  if (count < 2 || count % 2 != 0) return false;

  for (long i = 2; i < count; i += 2) {
    const conf_argument *argument = conf_get_argument(directive, i);
    if (
      argument == NULL ||
      argument->value == NULL ||
      !is_operator(argument->value) ||
      !is_bare_argument(translator, argument, argument->value)
    ) return false;
  }
  return true;
}

// Contexts such as `if` branches and `id { … }` require exactly one
// expression. A single expression may still span several Confetti directives
// (an `id` plus operator continuations, or a full conditional chain).
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
  long position = 0;
  if (!translate_sequence_item(translator, directive, &position, index)) return false;
  if (position == count) return true;

  snprintf(
    *translator->error,
    ERR_MSG_SIZE,
    "Block contains multiple expressions; use `do { … }` for a single sequence"
  );
  return false;
}

static bool translate_sequence_block(
  ConfettiTranslator *translator,
  const conf_directive *directive,
  uint32_t *index
) {
  size_t byte_offset = directive_offset(directive);
  if (!new_expression(translator, EXPR_LIST, byte_offset, index)) return false;
  uint32_t previous = EXPR_NONE;
  uint32_t name = EXPR_NONE;
  if (!new_name(translator, "do", byte_offset, &name)) return false;
  append_child(translator->interpreter, *index, &previous, name);

  long count = conf_get_directive_count(directive);
  for (long i = 0; i < count;) {
    uint32_t child = EXPR_NONE;
    if (!translate_sequence_item(translator, directive, &i, &child)) return false;
    append_child(translator->interpreter, *index, &previous, child);
  }
  return true;
}

// Translate `id atom` or `id {call}` to the value itself, without an
// extra Lispico function call. This also works as the last item in `(do)`.
static bool translate_id_directive(
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
    *translator->error,
    ERR_MSG_SIZE,
    "`id` expects one argument or one expression block"
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
  long total =
    conf_get_argument_count(directive)
    + (has_directive_block(translator, directive) ? 1 : 0);
  if (total < 3) {
    snprintf(*translator->error, ERR_MSG_SIZE, "Infix operator is missing an operand");
    return false;
  }

  InfixCall call;
  if (
    !start_infix_call(
      &call,
      translator,
      operator_name,
      argument_offset(conf_get_argument(directive, 1))
    )
  ) return false;

  uint32_t first = EXPR_NONE;
  if (!translate_argument(translator, conf_get_argument(directive, 0), &first)) return false;
  if (!append_infix_operand(&call, first)) return false;

  if (!append_operator_continuation(translator, directive, &call, 1)) return false;

  *index = call.expression;
  return true;
}

// A regular function call may precede an infix operator within one native
// Confetti directive: `div 7.5 2.0 == 3.0` becomes `(== (div 7.5 2.0) 3.0)`.
// The operator separates already-parsed Confetti arguments, so no additional
// source scanning or expression parser is necessary.
static bool translate_call_with_operator_suffix(
  ConfettiTranslator *translator,
  const conf_directive *directive,
  long operator_position,
  const char *operator_name,
  uint32_t *index
) {
  InfixCall call;
  if (
    !start_infix_call(
      &call,
      translator,
      operator_name,
      argument_offset(conf_get_argument(directive, operator_position))
    )
  ) return false;

  uint32_t function = EXPR_NONE;
  if (!new_expression(translator, EXPR_LIST, directive_offset(directive), &function)) return false;
  uint32_t previous = EXPR_NONE;
  for (long i = 0; i < operator_position; ++i) {
    uint32_t argument = EXPR_NONE;
    if (!translate_argument(translator, conf_get_argument(directive, i), &argument)) return false;
    append_child(translator->interpreter, function, &previous, argument);
  }

  uint32_t function_name = translator->interpreter->storage.expressions[function].first_child;
  if (translator->interpreter->storage.expressions[function_name].kind != EXPR_NAME) {
    snprintf(*translator->error, ERR_MSG_SIZE, "Expected a function name at directive start");
    return false;
  }
  if (!append_infix_operand(&call, function)) return false;
  if (!append_operator_continuation(translator, directive, &call, operator_position)) return false;

  *index = call.expression;
  return true;
}

// An inline condition, such as `if 5 > 2 { … }`, uses Confetti’s ordinary
// arguments. Only one infix operator may occur in a condition.
static bool translate_condition_arguments(
  ConfettiTranslator *translator,
  const conf_directive *directive,
  long first,
  long end,
  uint32_t *index
) {
  long count = end - first;
  if (count == 1)
    return translate_argument(translator, conf_get_argument(directive, first), index);

  if (count < 3 || count % 2 == 0) {
    snprintf(
      *translator->error,
      ERR_MSG_SIZE,
      "`if` expects one condition or a homogeneous infix condition; use { … } for a call"
    );
    return false;
  }

  const conf_argument *operator_arg = conf_get_argument(directive, first + 1);
  if (
    operator_arg == NULL ||
    operator_arg->value == NULL ||
    !is_operator(operator_arg->value) ||
    !is_bare_argument(translator, operator_arg, operator_arg->value)
  ) {
    snprintf(
      *translator->error,
      ERR_MSG_SIZE,
      "Unsupported condition; use a single infix operator or { … } for a call"
    );
    return false;
  }

  InfixCall call;
  if (
    !start_infix_call(
      &call,
      translator,
      operator_arg->value,
      operator_arg->lexeme_offset
    )
  ) return false;

  for (long i = first; i < end; i += 2) {
    if (
      i != first &&
      !check_infix_operator(translator, conf_get_argument(directive, i - 1), call.operator_name)
    ) return false;

    uint32_t operand = EXPR_NONE;
    if (!translate_argument(translator, conf_get_argument(directive, i), &operand)) return false;
    if (!append_infix_operand(&call, operand)) return false;
  }

  *index = call.expression;
  return true;
}

typedef struct {
  uint32_t expression;
  uint32_t previous_child;
} ConditionalCall;

static bool start_conditional_call(
  ConfettiTranslator *translator,
  ConditionalCall *call,
  size_t byte_offset
) {
  call->previous_child = EXPR_NONE;
  if (!new_expression(translator, EXPR_LIST, byte_offset, &call->expression)) return false;

  uint32_t name = EXPR_NONE;
  if (!new_name(translator, "if", byte_offset, &name)) return false;
  append_child(translator->interpreter, call->expression, &call->previous_child, name);
  return true;
}

static bool translate_conditional_branch(
  ConfettiTranslator *translator,
  const conf_directive *directive,
  bool sequence,
  uint32_t *index
) {
  if (!has_directive_block(translator, directive)) {
    snprintf(*translator->error, ERR_MSG_SIZE, "Conditional branch requires a { … } block");
    return false;
  }
  if (sequence) return translate_sequence_block(translator, directive, index);
  else return translate_single_block(translator, directive, index);
}

// Confetti gives each directive only one trailing block. Thus
// `if condition then {branch}` is one directive, but
// `if {condition} then {branch}` is an `if` node followed by `then`.
static bool translate_if_clause(
  ConfettiTranslator *translator,
  const conf_directive *container,
  long *position,
  long condition_start,
  uint32_t *condition,
  uint32_t *branch
) {
  const conf_directive *clause = conf_get_directive(container, *position);
  long argc = conf_get_argument_count(clause);
  bool has_block = has_directive_block(translator, clause);

  if (argc == condition_start && has_block) {
    // The first block is the condition; the next sibling supplies the branch.
    if (!translate_single_block(translator, clause, condition)) return false;
    ++*position;
    if (*position >= conf_get_directive_count(container)) {
      snprintf(
        *translator->error,
        ERR_MSG_SIZE,
        "A blocked `if` condition must be followed by `then { … }` or `do { … }`"
      );
      return false;
    }

    const conf_directive *next = conf_get_directive(container, *position);
    const conf_argument *first = conf_get_argument(next, 0);
    long next_argc = conf_get_argument_count(next);
    bool is_then = is_bare_argument(translator, first, "then");
    bool is_do = is_bare_argument(translator, first, "do");
    bool sequence = is_do;
    if (
      (!is_then && !is_do) ||
      !(next_argc == 1 || (
        is_then && next_argc == 2 &&
        is_bare_argument(translator, conf_get_argument(next, 1), "do")
      ))
    ) {
      snprintf(
        *translator->error, ERR_MSG_SIZE,
        "Expected `then { … }`, `do { … }`, or `then do { … }` after blocked condition"
      );
      return false;
    }
    if (next_argc == 2) sequence = true;
    if (!translate_conditional_branch(translator, next, sequence, branch)) return false;
    ++*position;
    return true;
  }

  if (!has_block) {
    snprintf(*translator->error, ERR_MSG_SIZE, "`if` requires a branch block");
    return false;
  }

  long condition_end = argc;
  bool sequence = false;
  if (
    condition_end > condition_start &&
    is_bare_argument(translator, conf_get_argument(clause, condition_end - 1), "do")
  ) {
    sequence = true;
    --condition_end;
  }
  if (
    condition_end > condition_start &&
    is_bare_argument(translator, conf_get_argument(clause, condition_end - 1), "then")
  ) --condition_end;

  if (condition_end == condition_start) {
    snprintf(*translator->error, ERR_MSG_SIZE, "`if` requires a condition");
    return false;
  }
  if (
    !translate_condition_arguments(translator, clause, condition_start, condition_end, condition)
  ) return false;
  if (!translate_conditional_branch(translator, clause, sequence, branch)) return false;
  ++*position;
  return true;
}

// Consume the full `if` / `else if` / `else` chain from sibling directives.
// Each `else if` becomes the false branch of the preceding Lispico `(if)`.
static bool translate_if_chain(
  ConfettiTranslator *translator,
  const conf_directive *container,
  long *position,
  uint32_t *index
) {
  ConditionalCall current;
  // Reserve the outermost expression before its children so root index 0
  // always points at the complete conditional call.
  if (
    !start_conditional_call(
      translator,
      &current,
      directive_offset(conf_get_directive(container, *position))
    )
  ) return false;
  *index = current.expression;

  uint32_t condition = EXPR_NONE;
  uint32_t branch = EXPR_NONE;
  if (!translate_if_clause(translator, container, position, 1, &condition, &branch)) return false;
  append_child(translator->interpreter, current.expression, &current.previous_child, condition);
  append_child(translator->interpreter, current.expression, &current.previous_child, branch);

  long sibling_count = conf_get_directive_count(container);
  while (*position < sibling_count) {
    const conf_directive *next = conf_get_directive(container, *position);
    if (!is_bare_argument(translator, conf_get_argument(next, 0), "else")) break;

    long argc = conf_get_argument_count(next);
    if (argc > 1 && is_bare_argument(translator, conf_get_argument(next, 1), "if")) {
      ConditionalCall nested;
      if (
        !start_conditional_call(
          translator,
          &nested,
          argument_offset(conf_get_argument(next, 1))
        )
      ) return false;
      append_child(
        translator->interpreter,
        current.expression,
        &current.previous_child,
        nested.expression
      );
      current = nested;
      if (
        !translate_if_clause(translator, container, position, 2, &condition, &branch)
      ) return false;
      append_child(translator->interpreter, current.expression, &current.previous_child, condition);
      append_child(translator->interpreter, current.expression, &current.previous_child, branch);
      continue;
    }

    bool sequence = argc == 2 && is_bare_argument(translator, conf_get_argument(next, 1), "do");
    if (argc != 1 && !sequence) {
      snprintf(*translator->error, ERR_MSG_SIZE, "`else` expects a block or `do { … }`");
      return false;
    }
    if (!translate_conditional_branch(translator, next, sequence, &branch)) return false;
    append_child(translator->interpreter, current.expression, &current.previous_child, branch);
    ++*position;
    break;
  }

  return true;
}

static bool translate_sequence_item(
  ConfettiTranslator *translator,
  const conf_directive *container,
  long *position,
  uint32_t *index
) {
  const conf_directive *directive = conf_get_directive(container, *position);
  const conf_argument *first = conf_get_argument(directive, 0);
  if (is_bare_argument(translator, first, "if"))
    return translate_if_chain(translator, container, position, index);

  long count = conf_get_directive_count(container);
  const conf_directive *next =
    *position + 1 < count ? conf_get_directive(container, *position + 1) : NULL;

  // A computed-index assignment is two adjacent native Confetti directives:
  // `foo[] {i * 2} = 42` or `foo[] {i * 2} = {x + 1}`.
  // The first directive owns the index block; the second supplies the value.
  IndexedArgument indexed = {0};
  if (
    parse_indexed_argument(translator, first, &indexed) &&
    indexed.block_index &&
    has_directive_block(translator, directive) &&
    next != NULL &&
    is_bare_argument(translator, conf_get_argument(next, 0), "=")
  ) {
    if (!translate_indexed_operation(translator, &indexed, directive, next, 1, index)) return false;
    *position += 2;
    return true;
  }

  const conf_argument *next_operator = next == NULL ? NULL : conf_get_argument(next, 0);
  bool next_has_id_marker =
    next != NULL &&
    is_bare_argument(translator, conf_get_argument(next, 1), "id") &&
    (conf_get_argument_count(next) > 2 || has_directive_block(translator, next));
  bool next_has_inline_pairs = has_inline_operator_pairs(translator, next);
  if (
    (
      is_bare_argument(translator, first, "id") ||
      (
        has_directive_block(translator, directive) &&
        next_operator != NULL &&
        next_operator->value != NULL &&
        conf_get_argument_count(next) >= 2 &&
        (
          !is_chainable_operator(next_operator->value) ||
          next_has_id_marker ||
          next_has_inline_pairs
        )
      )
    ) &&
    next != NULL &&
    begins_operator_continuation(translator, next)
  ) {
    const char *operator_name = conf_get_argument(next, 0)->value;
    InfixCall call;
    // Allocate the outer expression first to preserve Lispico’s entry index.
    if (
      !start_infix_call(
        &call,
        translator,
        operator_name,
        argument_offset(next_operator)
      )
    ) return false;
    uint32_t first_operand = EXPR_NONE;
    if (!translate_directive(translator, directive, &first_operand)) return false;
    if (!append_infix_operand(&call, first_operand)) return false;
    ++*position;

    while (
      *position < count &&
      begins_operator_continuation(translator, conf_get_directive(container, *position))
    ) {
      if (
        !append_operator_continuation(
          translator,
          conf_get_directive(container, *position),
          &call,
          0
        )
      ) return false;
      ++*position;
    }
    *index = call.expression;
    return true;
  }

  if (
    is_bare_argument(translator, first, "else") ||
    is_bare_argument(translator, first, "then")
  ) {
    snprintf(*translator->error, ERR_MSG_SIZE, "Unexpected `else` or `then` without `if`");
    return false;
  }

  if (!translate_directive(translator, directive, index)) return false;
  ++*position;
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

  if (is_bare_argument(translator, first, "id"))
    return translate_id_directive(translator, directive, index);

  // Standalone `do` consumes all of its subdirectives as a single sequence.
  if (count == 1 && has_block && is_bare_argument(translator, first, "do"))
    return translate_sequence_block(translator, directive, index);

  IndexedArgument indexed = {0};
  bool has_index = parse_indexed_argument(translator, first, &indexed);

  // Assignment has its own translation path: the left-hand name must become
  // a literal string, and an indexed target selects arr-set or str-set.
  if (count >= 2 && is_bare_argument(translator, conf_get_argument(directive, 1), "=")) {
    if (has_index) {
      return translate_indexed_operation(translator, &indexed, NULL, directive, 2, index);
    } else {
      return translate_scalar_assignment(translator, directive, index);
    }
  }

  // An indexed argument by itself is an expression, not a function call.
  // A computed [] index takes exactly one nested Confetti expression.
  if (count == 1 && has_index) {
    return translate_indexed_operation(
      translator,
      &indexed,
      has_block ? directive : NULL,
      NULL,
      0,
      index
    );
  }

  if (count > 1) {
    const conf_argument *second = conf_get_argument(directive, 1);
    if (
      second != NULL && second->value != NULL &&
      is_operator(second->value) &&
      is_bare_argument(translator, second, second->value)
    ) return translate_infix_directive(translator, directive, second->value, index);
  }

  // A named call followed by an operator in the same directive uses the
  // completed call as the operator’s left operand. Keep prefix operators
  // themselves separate: mixed operators still require explicit grouping.
  if (first != NULL && first->value != NULL && !is_operator(first->value)) {
    for (long i = 2; i < count; ++i) {
      const conf_argument *argument = conf_get_argument(directive, i);
      if (
        argument != NULL && argument->value != NULL &&
        is_operator(argument->value) &&
        is_bare_argument(translator, argument, argument->value)
      ) return translate_call_with_operator_suffix(
        translator, directive, i, argument->value, index
      );
    }
  }

  // Ordinary Confetti directives call the named Lispico function. Prefix
  // operators remain ordinary calls; logical `&&` and `||` use their Lispico
  // special-form names `and` and `or`.
  if (!new_expression(translator, EXPR_LIST, directive_offset(directive), index)) return false;
  uint32_t previous = EXPR_NONE;
  long argument_count = count;
  bool explicit_do = false;
  if (has_block && count > 1) {
    explicit_do = is_bare_argument(translator, conf_get_argument(directive, count - 1), "do");
    if (explicit_do) argument_count--;
  }

  for (long i = 0; i < argument_count; ++i) {
    uint32_t argument = EXPR_NONE;
    const conf_argument *item = conf_get_argument(directive, i);
    if (
      i == 0 &&
      item != NULL &&
      item->value != NULL &&
      (is_bare_argument(translator, item, "&&") || is_bare_argument(translator, item, "||"))
    ) {
      if (
        !new_name(
          translator,
          lispico_operator_name(item->value),
          item->lexeme_offset,
          &argument
        )
      ) return false;
    } else if (!translate_argument(translator, item, &argument)) {
      return false;
    }
    append_child(translator->interpreter, *index, &previous, argument);
  }

  uint32_t function_name = translator->interpreter->storage.expressions[*index].first_child;
  if (translator->interpreter->storage.expressions[function_name].kind != EXPR_NAME) {
    snprintf(*translator->error, ERR_MSG_SIZE, "Expected a function name at directive start");
    return false;
  }

  if (has_block) {
    if (explicit_do) {
      uint32_t argument = EXPR_NONE;
      if (!translate_sequence_block(translator, directive, &argument)) return false;
      append_child(translator->interpreter, *index, &previous, argument);
    } else {
      // An ordinary block supplies zero or more arguments, one expression
      // per logical subdirective. `do` explicitly groups a sequence into one.
      long block_count = conf_get_directive_count(directive);
      for (long position = 0; position < block_count;) {
        uint32_t argument = EXPR_NONE;
        if (!translate_sequence_item(translator, directive, &position, &argument)) return false;
        append_child(translator->interpreter, *index, &previous, argument);
      }
    }
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
      *error,
      ERR_MSG_SIZE,
      "Confetti parse error at byte %zu: %s",
      parse_error.where,
      parse_error.description
    );
    return false;
  }

  ConfettiTranslator translator = {
    .interpreter = interpreter,
    .source = source,
    .source_length = length,
    .error = error,
    .location_scan_offset = 0,
    .location_scan_line = 1,
    .location_scan_column = 1,
  };

  const conf_directive *root = conf_get_root(unit);
  long count = conf_get_directive_count(root);
  uint32_t previous = EXPR_NONE;
  for (long i = 0; i < count;) {
    uint32_t expression = EXPR_NONE;
    if (!translate_sequence_item(&translator, root, &i, &expression)) goto fail;
    if (previous != EXPR_NONE) interpreter->storage.expressions[previous].next = expression;
    previous = expression;
  }

  conf_free(unit);
  return true;

  fail:
    conf_free(unit);
    clean_expressions(interpreter);
    return false;
}
