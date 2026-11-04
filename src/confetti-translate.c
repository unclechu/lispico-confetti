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

// Store a NUL-terminated string or name in Lispico's immutable expression data.
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

  if (!allocate_expression_data(
    translator->interpreter,
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

// Each simple Confetti directive is a Lispico function-call expression.
// Example: `println "Hello"` becomes `(println "Hello")`.
static bool translate_directive(
  ConfettiTranslator *translator,
  const conf_directive *directive,
  uint32_t *index
) {
  if (conf_get_directive_count(directive) != 0) {
    snprintf(*translator->error, ERR_MSG_SIZE, "Directive blocks are not implemented yet");
    return false;
  }

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

    if (previous_child == EXPR_NONE)
      translator->interpreter->storage.expressions[*index].first_child = child;
    else
      translator->interpreter->storage.expressions[previous_child].next = child;

    previous_child = child;
  }

  // Function names must be bare identifiers, not string literals or numbers.
  uint32_t fn_index = translator->interpreter->storage.expressions[*index].first_child;
  if (translator->interpreter->storage.expressions[fn_index].kind != EXPR_NAME) {
    snprintf(*translator->error, ERR_MSG_SIZE, "Expected a function name at directive start");
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
