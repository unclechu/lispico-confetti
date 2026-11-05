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

// Confetti has already removed its own escapes from the argument value.
// Decode the remaining backslash sequences using Lispico’s string literal rules.
// Confetti turns "\\n" into "\n" (two literal bytes), and this second pass
// turns those bytes into a newline character.
// Use an explicit length because the resulting string may contain NUL bytes.
static bool translate_string_value(
  ConfettiTranslator *translator,
  const char *value,
  size_t value_length,
  size_t byte_offset,
  uint32_t *index
) {
  // Decoding replaces or removes bytes, so the result never grows beyond
  // the Confetti-decoded input. One final byte is reserved for the terminator.
  if (value_length >= EXPR_TEXT_SIZE) {
    snprintf(*translator->error, ERR_MSG_SIZE, "String literal is too long");
    return false;
  }

  char text[EXPR_TEXT_SIZE] = {'\0'};
  size_t length = 0;

  for (size_t i = 0; i < value_length; ++i) {
    char c = value[i];
    if (c == '\\') {
      // A surviving backslash must introduce a valid Lispico escape.
      if (++i == value_length) {
        snprintf(
          *translator->error,
          ERR_MSG_SIZE,
          "Unterminated Lispico string escape in argument starting at byte %zu",
          byte_offset
        );
        return false;
      }

      switch (value[i]) {
        case '"': c = '"'; break;
        case '\\': c = '\\'; break;
        case 'n': c = '\n'; break;
        case 'r': c = '\r'; break;
        case 't': c = '\t'; break;
        case '0': c = '\0'; break;
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
    text[length++] = c;
  }

  // Keep the terminator separate from the logical string length.
  text[length] = '\0';

  if (!new_expression(translator, EXPR_STRING, index)) return false;
  return store_text(
    translator,
    text,
    length,
    &translator->interpreter->storage.expressions[*index].data
  );
}

// Translate an unquoted atom into a numeric literal or symbol reference.
// `text` does not have to be NUL-terminated, so this also works for tokens
// inside parenthesized expressions.
static bool translate_bare_atom(
  ConfettiTranslator *translator,
  const char *text,
  size_t length,
  size_t byte_offset,
  uint32_t *index
) {
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
        byte_offset
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

static bool translate_expression_argument(
  ConfettiTranslator *translator,
  const conf_argument *argument,
  uint32_t *index
);

// Translate one Confetti argument into a Lispico expression.
static bool translate_argument(
  ConfettiTranslator *translator,
  const conf_argument *argument,
  uint32_t *index
) {
  if (argument == NULL || argument->value == NULL) {
    snprintf(*translator->error, ERR_MSG_SIZE, "Invalid Confetti argument");
    return false;
  }

  if (argument->is_expression)
    return translate_expression_argument(translator, argument, index);

  if (argument->lexeme_offset >= translator->source_length) {
    snprintf(*translator->error, ERR_MSG_SIZE, "Invalid Confetti argument offset");
    return false;
  }

  const char *text = argument->value;
  size_t length = strlen(text);

  // Confetti removes quotes from string values. Inspect the original lexeme
  // to distinguish `name` (a symbol reference) from `"name"` (a string).
  if (translator->source[argument->lexeme_offset] == '"') {
    return translate_string_value(
      translator,
      text,
      length,
      argument->lexeme_offset,
      index
    );
  }

  return translate_bare_atom(
    translator,
    text,
    length,
    argument->lexeme_offset,
    index
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

typedef enum {
  INFIX_TOKEN_END,
  INFIX_TOKEN_WORD,
  INFIX_TOKEN_STRING,
  INFIX_TOKEN_OPERATOR,
  INFIX_TOKEN_OPEN,
  INFIX_TOKEN_CLOSE,
} InfixTokenKind;

typedef struct {
  InfixTokenKind kind;
  size_t start;
  size_t length;
} InfixToken;

typedef struct {
  ConfettiTranslator *translator;
  const char *text;
  size_t length;
  size_t byte_offset;
  size_t cursor;
  InfixToken token;
} InfixParser;

static bool is_expression_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

static bool is_operator_character(char c) {
  return c == '+' || c == '-' || c == '*' || c == '/' ||
    c == '<' || c == '>' || c == '=' || c == '!' ||
    c == '%' || c == '&' || c == '|';
}

static bool is_digit(char c) {
  return c >= '0' && c <= '9';
}

static bool infix_fail(InfixParser *parser, const char *message) {
  snprintf(
    *parser->translator->error,
    ERR_MSG_SIZE,
    "%s at byte %zu",
    message,
    parser->byte_offset + parser->token.start
  );
  return false;
}

// Confetti leaves the inside of parenthesized arguments uninterpreted.
// Read one token without assuming that the whole argument is NUL-terminated.
static bool next_infix_token(InfixParser *parser) {
  const char *text = parser->text;
  size_t position = parser->cursor;

  while (position < parser->length && is_expression_space(text[position])) position++;

  parser->token = (InfixToken) {
    .kind = INFIX_TOKEN_END,
    .start = position,
    .length = 0,
  };

  if (position == parser->length) {
    parser->cursor = position;
    return true;
  }

  char c = text[position];
  InfixTokenKind kind = INFIX_TOKEN_WORD;
  size_t start = position;

  if (c == '(' || c == ')') {
    kind = c == '(' ? INFIX_TOKEN_OPEN : INFIX_TOKEN_CLOSE;
    position++;
  } else if (c == '"') {
    kind = INFIX_TOKEN_STRING;
    position++;
    bool closed = false;

    // An escaped quote does not end the string. Confetti’s ordinary quoted
    // arguments also remove the backslash from escaped characters.
    while (position < parser->length) {
      if (text[position] == '\\') {
        position++;
        if (position == parser->length) return infix_fail(parser, "Incomplete string escape");
        position++;
      } else if (text[position] == '"') {
        position++;
        closed = true;
        break;
      } else {
        position++;
      }
    }

    if (!closed)
      return infix_fail(parser, "Unterminated string in expression");
  } else if (is_operator_character(c)) {
    kind = INFIX_TOKEN_OPERATOR;
    position++;

    if (
      position < parser->length &&
      text[position] == '=' &&
      (c == '=' || c == '!' || c == '<' || c == '>')
    ) position++;
  } else if (c == '{' || c == '}' || c == ';' || c == ',') {
    return infix_fail(parser, "Unexpected punctuation in expression");
  } else {
    bool name = !is_digit(c) && c != '.';
    position++;

    while (position < parser->length) {
      char next = text[position];

      if (
        is_expression_space(next) || next == '(' || next == ')' ||
        next == '"' || next == '{' || next == '}' || next == ';' || next == ','
      ) break;

      // Keep hyphens in names such as `arr-get`. Numeric exponents may also
      // contain signs, as in `1e-3`; other operator characters end the word.
      if (is_operator_character(next)) {
        bool name_hyphen = name && next == '-';
        bool exponent_sign = (
          !name && (next == '-' || next == '+') && position > start &&
          (text[position - 1] == 'e' || text[position - 1] == 'E') &&
          position + 1 < parser->length && is_digit(text[position + 1])
        );
        if (!name_hyphen && !exponent_sign) break;
      }

      position++;
    }
  }

  parser->token = (InfixToken) {
    .kind = kind,
    .start = start,
    .length = position - start,
  };
  parser->cursor = position;
  return true;
}

static bool infix_token_equals(const InfixParser *parser, const char *text) {
  size_t length = strlen(text);
  return parser->token.length == length &&
    memcmp(parser->text + parser->token.start, text, length) == 0;
}

// Higher precedence binds more tightly. Arithmetic operators associate from
// left to right; comparison and equality operators use the same rule.
static int infix_operator_precedence(const InfixParser *parser) {
  if (parser->token.kind != INFIX_TOKEN_OPERATOR) return -1;

  if (infix_token_equals(parser, "*") || infix_token_equals(parser, "/")) return 40;
  if (infix_token_equals(parser, "+") || infix_token_equals(parser, "-")) return 30;
  if (
    infix_token_equals(parser, "<") || infix_token_equals(parser, "<=") ||
    infix_token_equals(parser, ">") || infix_token_equals(parser, ">=")
  ) return 20;
  if (infix_token_equals(parser, "==") || infix_token_equals(parser, "!=")) return 10;

  return -1;
}

// Lispico’s arithmetic functions accept two or more arguments, accumulating
// the result from left to right. Comparisons and equality are binary only.
static bool is_variadic_infix_operator(const InfixParser *parser, InfixToken token) {
  if (token.kind != INFIX_TOKEN_OPERATOR || token.length != 1) return false;

  char c = parser->text[token.start];
  return c == '+' || c == '-' || c == '*' || c == '/';
}

static bool new_infix_function_name(
  InfixParser *parser,
  InfixToken token,
  uint32_t *index
) {
  if (token.length == 0 || token.length >= SYMBOL_NAME_SIZE)
    return infix_fail(parser, "Invalid function name in expression");

  char name[SYMBOL_NAME_SIZE] = {'\0'};
  memcpy(name, parser->text + token.start, token.length);

  if (!new_expression(parser->translator, EXPR_NAME, index)) return false;
  return store_text(
    parser->translator,
    name,
    token.length,
    &parser->translator->interpreter->storage.expressions[*index].data
  );
}

// Construct `(FUNCTION ARG…)` from expression indices already parsed.
// The children must be attached only once because `next` links are mutable.
static bool new_infix_call(
  InfixParser *parser,
  uint32_t function_index,
  const uint32_t *arguments,
  size_t argument_count,
  uint32_t *index
) {
  if (!new_expression(parser->translator, EXPR_LIST, index)) return false;

  Interpreter *interpreter = parser->translator->interpreter;
  uint32_t previous_child = EXPR_NONE;
  append_child(interpreter, *index, &previous_child, function_index);

  for (size_t i = 0; i < argument_count; ++i)
    append_child(interpreter, *index, &previous_child, arguments[i]);

  return true;
}

static bool parse_infix_expression(
  InfixParser *parser,
  int minimum_precedence,
  uint32_t *index
);

// Read arguments for a parenthesized prefix call such as
// `(arr-get "numbers" 1)`. Its first expression has already been identified
// as the function name; adjacent operands are separate function arguments.
static bool parse_infix_call_arguments(
  InfixParser *parser,
  uint32_t function_index,
  uint32_t *index
) {
  uint32_t arguments[EVAL_ARG_CAPACITY] = {0};
  size_t argument_count = 0;

  while (parser->token.kind != INFIX_TOKEN_CLOSE) {
    if (parser->token.kind == INFIX_TOKEN_END)
      return infix_fail(parser, "Unclosed parenthesized function call");

    if (argument_count >= EVAL_ARG_CAPACITY)
      return infix_fail(parser, "Too many function arguments in expression");

    if (!parse_infix_expression(parser, 0, &arguments[argument_count])) return false;
    argument_count++;
  }

  if (!next_infix_token(parser)) return false;
  return new_infix_call(parser, function_index, arguments, argument_count, index);
}

// Parentheses group infix expressions. If the first expression is a function
// name followed by adjacent arguments, interpret them as a prefix native call.
static bool parse_infix_parentheses(InfixParser *parser, uint32_t *index) {
  if (!next_infix_token(parser)) return false; // Consume `(`

  if (parser->token.kind == INFIX_TOKEN_CLOSE || parser->token.kind == INFIX_TOKEN_END)
    return infix_fail(parser, "Empty parenthesized expression");

  // Symbolic Lispico function names can also be called in prefix position.
  // Require a separating space so `(-2)` remains a negative numeric literal.
  InfixToken first_token = parser->token;
  size_t after_first = first_token.start + first_token.length;
  bool operator_call = (
    first_token.kind == INFIX_TOKEN_OPERATOR &&
    infix_operator_precedence(parser) >= 0 &&
    after_first < parser->length &&
    is_expression_space(parser->text[after_first])
  );

  if (operator_call) {
    uint32_t function_index = EXPR_NONE;
    if (!new_infix_function_name(parser, first_token, &function_index)) return false;
    if (!next_infix_token(parser)) return false;
    return parse_infix_call_arguments(parser, function_index, index);
  }

  uint32_t first_expression = EXPR_NONE;
  if (!parse_infix_expression(parser, 0, &first_expression)) return false;

  if (parser->token.kind == INFIX_TOKEN_CLOSE) {
    if (!next_infix_token(parser)) return false;
    *index = first_expression;
    return true;
  }

  // `name` followed by another primary begins a prefix call. Parentheses
  // around a single name are grouping, not a zero-argument function call.
  Expression *first = &parser->translator->interpreter->storage.expressions[first_expression];
  if (
    first->kind == EXPR_NAME &&
    (
      parser->token.kind == INFIX_TOKEN_WORD ||
      parser->token.kind == INFIX_TOKEN_STRING ||
      parser->token.kind == INFIX_TOKEN_OPEN
    )
  ) return parse_infix_call_arguments(parser, first_expression, index);

  if (parser->token.kind == INFIX_TOKEN_OPERATOR)
    return infix_fail(parser, "Unsupported infix operator");

  return infix_fail(parser, "Expected closing parenthesis or function argument");
}

static bool parse_infix_primary(InfixParser *parser, uint32_t *index) {
  InfixToken token = parser->token;

  switch (token.kind) {
    case INFIX_TOKEN_WORD: {
      if (!translate_bare_atom(
        parser->translator,
        parser->text + token.start,
        token.length,
        parser->byte_offset + token.start,
        index
      )) return false;
      return next_infix_token(parser);
    }

    case INFIX_TOKEN_STRING: {
      char string[EXPR_TEXT_SIZE] = {'\0'};
      size_t length = 0;
      size_t end = token.start + token.length - 1; // Exclude the closing quote

      for (size_t i = token.start + 1; i < end; ++i) {
        if (length >= sizeof(string) - 1)
          return infix_fail(parser, "String in expression is too long");

        // Match Confetti’s quoted-argument behavior: an escape removes the
        // backslash, without interpreting `\n` as a newline character.
        if (parser->text[i] == '\\') i++;
        string[length++] = parser->text[i];
      }

      if (!translate_string_value(
        parser->translator,
        string,
        length,
        parser->byte_offset + token.start,
        index
      )) return false;

      return next_infix_token(parser);
    }

    case INFIX_TOKEN_OPEN:
      return parse_infix_parentheses(parser, index);

    case INFIX_TOKEN_OPERATOR: {
      if (!infix_token_equals(parser, "-") && !infix_token_equals(parser, "+"))
        return infix_fail(parser, "Expected a value in expression");

      char sign = parser->text[token.start];
      if (!next_infix_token(parser)) return false;

      String number = {
        .data = parser->text + parser->token.start,
        .length = parser->token.length,
      };
      if (parser->token.kind != INFIX_TOKEN_WORD || !is_number_start(number))
        return infix_fail(parser, "Unary sign expects a numeric literal");

      if (number.length >= EXPR_TEXT_SIZE - 1)
        return infix_fail(parser, "Signed numeric literal is too long");

      char signed_number[EXPR_TEXT_SIZE] = {'\0'};
      signed_number[0] = sign;
      memcpy(signed_number + 1, number.data, number.length);

      if (!translate_bare_atom(
        parser->translator,
        signed_number,
        number.length + 1,
        parser->byte_offset + token.start,
        index
      )) return false;

      return next_infix_token(parser);
    }

    default:
      return infix_fail(parser, "Expected a value in expression");
  }
}

// Precedence climbing preserves left-to-right evaluation. Consecutive uses of
// the same variadic arithmetic operator share a single Lispico function call:
// `20 - 5 - 3` becomes `(- 20 5 3)`, while `1 + 2 * 3` becomes `(+ 1 (* 2 3))`.
static bool parse_infix_expression(
  InfixParser *parser,
  int minimum_precedence,
  uint32_t *index
) {
  uint32_t left = EXPR_NONE;
  if (!parse_infix_primary(parser, &left)) return false;

  InfixToken previous_operator = {0};
  uint32_t last_argument = EXPR_NONE;
  size_t argument_count = 0;

  for (;;) {
    int precedence = infix_operator_precedence(parser);
    if (precedence < minimum_precedence) break;

    InfixToken operator = parser->token;
    if (!next_infix_token(parser)) return false;

    uint32_t right = EXPR_NONE;
    if (!parse_infix_expression(parser, precedence + 1, &right)) return false;

    // Extend only the chain created in this parsing scope. An explicitly
    // parenthesized left operand remains a separate expression. Respect the
    // evaluator’s argument capacity by starting a new call when a chain fills.
    bool extend_chain = (
      argument_count < EVAL_ARG_CAPACITY &&
      is_variadic_infix_operator(parser, operator) &&
      previous_operator.length == operator.length &&
      memcmp(
        parser->text + previous_operator.start,
        parser->text + operator.start,
        operator.length
      ) == 0
    );

    if (extend_chain) {
      append_child(parser->translator->interpreter, left, &last_argument, right);
      argument_count++;
    } else {
      uint32_t function_index = EXPR_NONE;
      if (!new_infix_function_name(parser, operator, &function_index)) return false;

      uint32_t arguments[2] = { left, right };
      if (!new_infix_call(parser, function_index, arguments, 2, &left)) return false;

      argument_count = 2;
      last_argument = right;
    }

    previous_operator = operator;
  }

  *index = left;
  return true;
}

static bool translate_expression_argument(
  ConfettiTranslator *translator,
  const conf_argument *argument,
  uint32_t *index
) {
  if (
    argument->lexeme_offset >= translator->source_length ||
    argument->lexeme_length > translator->source_length - argument->lexeme_offset ||
    argument->lexeme_length < 2
  ) {
    snprintf(*translator->error, ERR_MSG_SIZE, "Invalid parenthesized expression location");
    return false;
  }

  const char *text = translator->source + argument->lexeme_offset;
  size_t length = argument->lexeme_length;
  if (text[0] != '(' || text[length - 1] != ')') {
    snprintf(*translator->error, ERR_MSG_SIZE, "Invalid parenthesized expression");
    return false;
  }

  InfixParser parser = {
    .translator = translator,
    .text = text,
    .length = length,
    .byte_offset = argument->lexeme_offset,
  };

  if (!next_infix_token(&parser)) return false;
  if (!parse_infix_expression(&parser, 0, index)) return false;

  if (parser.token.kind != INFIX_TOKEN_END)
    return infix_fail(&parser, "Unexpected token after expression");

  return true;
}

// Create the function-name expression used by an explicit `do` block argument.
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

// A plain `{ … }` block supplies exactly one expression as the last argument.
// An explicit `do { … }` block creates a Lispico `(do)` sequence instead.
// For a standalone `do { … }`, attach the expressions directly to the
// existing `(do)` call to avoid introducing an additional sequence boundary.
static bool translate_block(
  ConfettiTranslator *translator,
  const conf_directive *directive,
  uint32_t list_index,
  uint32_t *previous_child,
  bool flatten_do,
  bool explicit_do
) {
  Interpreter *interpreter = translator->interpreter;
  long directive_count = conf_get_directive_count(directive);

  // A plain block has no implicit `(do)` scope. Requiring exactly one
  // expression prevents adding another directive from silently changing
  // the behavior of `(depth)`, `(leave)`, `(defer)`, or `(goto)`.
  if (!flatten_do && !explicit_do && directive_count != 1) {
    if (directive_count == 0) {
      snprintf(
        *translator->error,
        ERR_MSG_SIZE,
        "Empty expression block; use `do {}` for an empty sequence"
      );
    } else {
      snprintf(
        *translator->error,
        ERR_MSG_SIZE,
        "Block contains %ld expressions; use `do { … }` for a sequence",
        directive_count
      );
    }
    return false;
  }

  uint32_t block_index = list_index;
  uint32_t block_previous_child = EXPR_NONE;
  uint32_t *block_previous = previous_child;

  if (explicit_do && !flatten_do) {
    if (!new_expression(translator, EXPR_LIST, &block_index)) return false;

    uint32_t do_name = EXPR_NONE;
    if (!new_do_name(translator, &do_name)) return false;

    append_child(interpreter, block_index, &block_previous_child, do_name);
    block_previous = &block_previous_child;
  }

  for (long i = 0; i < directive_count; ++i) {
    const conf_directive *child_directive = conf_get_directive(directive, i);
    uint32_t child_index = EXPR_NONE;

    if (!translate_directive(translator, child_directive, &child_index)) return false;
    append_child(interpreter, block_index, block_previous, child_index);
  }

  if (explicit_do && !flatten_do)
    append_child(interpreter, list_index, previous_child, block_index);

  return true;
}

// Each Confetti directive becomes a Lispico function-call expression.
// `println "Hello"` becomes `(println "Hello")`.
// `var "x" { + 1 2 }` becomes `(var "x" (+ 1 2))`.
// `var "x" do { + 1 2 }` becomes `(var "x" (do (+ 1 2)))`.
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

  bool has_block = has_directive_block(translator, directive);
  bool explicit_do = false;

  // The trailing bare `do` before a block is a sequence marker, not an
  // argument to the surrounding function. Leave `do` as an ordinary argument
  // when there is no block, or when it appears inside a quoted string.
  if (has_block && argument_count > 1) {
    const conf_argument *last_argument = conf_get_argument(directive, argument_count - 1);
    explicit_do = (
      last_argument != NULL &&
      last_argument->value != NULL &&
      !last_argument->is_expression &&
      last_argument->lexeme_offset < translator->source_length &&
      translator->source[last_argument->lexeme_offset] == 'd' &&
      strcmp(last_argument->value, "do") == 0
    );
  }

  long translated_argument_count = explicit_do ? argument_count - 1 : argument_count;
  uint32_t previous_child = EXPR_NONE;

  for (long i = 0; i < translated_argument_count; ++i) {
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

  if (has_block) {
    const conf_argument *fn_argument = conf_get_argument(directive, 0);
    bool flatten_do = (
      translated_argument_count == 1 &&
      fn_argument != NULL &&
      strcmp(fn_argument->value, "do") == 0
    );

    if (!translate_block(
      translator,
      directive,
      *index,
      &previous_child,
      flatten_do,
      explicit_do
    ))
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

  // Confetti’s C parser accepts a NUL-terminated buffer, while Lispico uses
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
