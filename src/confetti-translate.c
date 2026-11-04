#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../confetti/confetti.h"
#include "../lispico/src/expression.h"
#include "../lispico/src/number.h"
#include "confetti-translate.h"

typedef struct {
  Interpreter *interpreter;

  // Confetti source file contents string (must be NUL-terminated)
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
  interpreter->expression_count++;
  interpreter->storage.expressions[*index] = (Expression) {
    .kind = kind,
    .first_child = EXPR_NONE,
    .next = EXPR_NONE,
  };
  return true;
}

// Store a NUL-terminated string or name in Lispico’s immutable expression data.
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

  // Reuse identical immutable expression text, including the synthetic `do`
  // function names created for blocks. Lispico’s original parser does this too.
  Interpreter *interpreter = translator->interpreter;
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

  if (!allocate_expression_data(
    interpreter,
    text,
    length + 1,
    TYPE_STRING,
    (uint32_t)length,
    allocation_index
  )) {
    snprintf(*translator->error, ERR_MSG_SIZE, "Unable to allocate expression text");
    return false;
  }

  return true;
}

// Translate one Confetti argument into a Lispico literal or symbol reference.
static bool translate_argument(
  ConfettiTranslator *translator,
  const conf_argument *argument,
  uint32_t *index
) {
  if (argument == NULL || argument->value == NULL) {
    snprintf(*translator->error, ERR_MSG_SIZE, "Invalid Confetti argument");
    return false;
  }

  // Confetti groups parenthesized expressions but does not interpret them.
  // They will be handled by the infix-expression parser in a later milestone.
  if (argument->is_expression) {
    snprintf(
      *translator->error,
      ERR_MSG_SIZE,
      "Parenthesized expressions are not implemented yet (byte %zu)",
      argument->lexeme_offset
    );
    return false;
  }

  if (argument->lexeme_offset >= translator->source_length) {
    snprintf(*translator->error, ERR_MSG_SIZE, "Invalid Confetti argument offset");
    return false;
  }

  const char *text = argument->value;
  size_t length = strlen(text);

  // `conf_argument.value` has its quotes removed. Inspect the original lexeme
  // to distinguish `name` (a symbol reference) from `"name"` (a string).
  if (translator->source[argument->lexeme_offset] == '"') {
    if (!new_expression(translator, EXPR_STRING, index)) return false;
    return store_text(
      translator,
      text,
      length,
      &translator->interpreter->storage.expressions[*index].data
    );
  }

  String atom = { .data = text, .length = length };

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
      snprintf(
        *translator->error,
        ERR_MSG_SIZE,
        "%s at byte %zu",
        problem,
        argument->lexeme_offset
      );
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

  if (length == 0 || length >= SYMBOL_NAME_SIZE) {
    snprintf(*translator->error, ERR_MSG_SIZE, "Invalid or too long symbol name");
    return false;
  }

  if (!new_expression(translator, EXPR_NAME, index)) return false;
  return store_text(
    translator,
    text,
    length,
    &translator->interpreter->storage.expressions[*index].data
  );
}

// Append one child to a Lispico expression list, preserving evaluation order.
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

// Create the function-name expression used by an implicit `(do)` block.
static bool new_do_name(ConfettiTranslator *translator, uint32_t *index) {
  if (!new_expression(translator, EXPR_NAME, index)) return false;
  return store_text(
    translator,
    "do",
    sizeof("do") - 1, // Exclude `\0` terminator coming with the C string literal
    &translator->interpreter->storage.expressions[*index].data
  );
}

// The Confetti AST exposes a block’s subdirectives, but does not explicitly
// distinguish an empty `{}` block from a directive with no block. Inspect the
// source immediately after the final argument to recognize empty blocks too.
static bool has_directive_block(
  const ConfettiTranslator *translator,
  const conf_directive *directive
) {
  if (conf_get_directive_count(directive) != 0) return true;

  long argument_count = conf_get_argument_count(directive);
  if (argument_count <= 0) return false;

  const conf_argument *last_argument = conf_get_argument(directive, argument_count - 1);
  if (
    last_argument == NULL ||
    last_argument->lexeme_offset > translator->source_length ||
    last_argument->lexeme_length > translator->source_length - last_argument->lexeme_offset
  ) return false;

  size_t position = last_argument->lexeme_offset + last_argument->lexeme_length;

  while (position < translator->source_length) {
    char c = translator->source[position];

    // Confetti allows whitespace and line terminators before a block.
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f') {
      position++;
      continue;
    }

    // Skip comments so a brace inside a comment is not mistaken for a block.
    if (c == '#') {
      while (position < translator->source_length && translator->source[position] != '\n')
        position++;
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

// A block supplies one final argument, represented by a Lispico `(do)` list.
// For `do { ... }`, attach the expressions directly to avoid an extra `(do)`
// scope, which would otherwise change the behavior of `(depth)` and `(leave)`.
static bool translate_block(
  ConfettiTranslator *translator,
  const conf_directive *directive,
  uint32_t list_index,
  uint32_t *previous_child,
  bool flatten_do
) {
  Interpreter *interpreter = translator->interpreter;
  uint32_t block_index = list_index;
  uint32_t block_previous_child = EXPR_NONE;
  uint32_t *block_previous = previous_child;

  if (!flatten_do) {
    if (!new_expression(translator, EXPR_LIST, &block_index)) return false;

    uint32_t do_name = EXPR_NONE;
    if (!new_do_name(translator, &do_name)) return false;

    append_child(interpreter, block_index, &block_previous_child, do_name);
    block_previous = &block_previous_child;
  }

  long directive_count = conf_get_directive_count(directive);

  for (long i = 0; i < directive_count; ++i) {
    const conf_directive *child_directive = conf_get_directive(directive, i);
    uint32_t child_index = EXPR_NONE;

    if (!translate_directive(translator, child_directive, &child_index)) return false;
    append_child(interpreter, block_index, block_previous, child_index);
  }

  if (!flatten_do)
    append_child(interpreter, list_index, previous_child, block_index);

  return true;
}

// Each Confetti directive becomes a Lispico function-call expression.
// `println "Hello"` becomes `(println "Hello")`.
// `var "x" { + 1 2 }` becomes `(var "x" (do (+ 1 2)))`.
static bool translate_directive(
  ConfettiTranslator *translator,
  const conf_directive *directive,
  uint32_t *index
) {
  long argument_count = conf_get_argument_count(directive);
  if (argument_count <= 0) {
    snprintf(*translator->error, ERR_MSG_SIZE, "Confetti directive has no arguments");
    return false;
  }

  if (!new_expression(translator, EXPR_LIST, index)) return false;

  uint32_t previous_child = EXPR_NONE;

  for (long i = 0; i < argument_count; ++i) {
    const conf_argument *argument = conf_get_argument(directive, i);
    uint32_t child = EXPR_NONE;

    if (!translate_argument(translator, argument, &child)) return false;

    append_child(translator->interpreter, *index, &previous_child, child);
  }

  // Function names must be bare identifiers, not string literals or numbers.
  uint32_t fn_index = translator->interpreter->storage.expressions[*index].first_child;
  if (translator->interpreter->storage.expressions[fn_index].kind != EXPR_NAME) {
    snprintf(*translator->error, ERR_MSG_SIZE, "Expected a function name at directive start");
    return false;
  }

  if (has_directive_block(translator, directive)) {
    const conf_argument *fn_argument = conf_get_argument(directive, 0);
    bool flatten_do = (
      argument_count == 1 &&
      fn_argument != NULL &&
      strcmp(fn_argument->value, "do") == 0
    );

    if (!translate_block(translator, directive, *index, &previous_child, flatten_do))
      return false;
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

  // Confetti's C parser accepts a NUL-terminated buffer, while Lispico uses
  // an explicit source length. Reject embedded NUL bytes instead of silently
  // ignoring everything that follows the first one.
  if (memchr(source, '\0', length) != NULL) {
    snprintf(*error, ERR_MSG_SIZE, "Source contains a NUL byte");
    return false;
  }

  conf_extensions extensions = { .expression_arguments = true };
  conf_options options = { .extensions = &extensions };
  conf_error parse_error = {0};
  conf_unit *unit = conf_parse(source, &options, &parse_error);

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
  };

  const conf_directive *root = conf_get_root(unit);
  long directive_count = conf_get_directive_count(root);
  uint32_t previous_expression = EXPR_NONE;

  for (long i = 0; i < directive_count; ++i) {
    const conf_directive *directive = conf_get_directive(root, i);
    uint32_t current_expression = EXPR_NONE;

    if (!translate_directive(&translator, directive, &current_expression)) goto fail;

    if (previous_expression != EXPR_NONE)
      interpreter->storage.expressions[previous_expression].next = current_expression;

    previous_expression = current_expression;
  }

  conf_free(unit);
  return true;

  fail:
    conf_free(unit);
    clean_expressions(interpreter);
    return false;
}
