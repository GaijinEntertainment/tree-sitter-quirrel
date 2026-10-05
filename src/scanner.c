#include "tree_sitter/alloc.h"
#include "tree_sitter/parser.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// constraint: the order matches the externals array in grammar.js
enum TokenType {
  AUTOMATIC_SEMICOLON,
  CLOSE_BRACE,
  SEMICOLON,
  INDEX_BRACKET,
  NULL_INDEX_BRACKET,
  POSTFIX_INCREMENT,
  POSTFIX_DECREMENT,
  ELSE_MARKER,
  CATCH_MARKER,
  TEMPLATE_CHARS,
  UNEXPECTED_NEWLINE,
  TEXT_AFTER_NUL,
  TERMINATOR_RESET,
  INTEGER,
  FLOAT,
  MALFORMED_NUMBER,
  NAME_ADJACENT,
  IMPORT_MARKER,
  SAME_LINE_TYPE_BAR,
  CONST_DECLARATION_END,
  AFTER_POSTFIX_UPDATE,
  BEFORE_IMPORT_ALIAS,
  NEVER_RETURNED,
  ERROR_SENTINEL,
};

// constraint: the runtime restores this state from the last external token, so each scan with the flag set returns one
typedef struct {
  bool after_terminator;
} Scanner;

typedef struct {
  bool newline;
  bool crossed_comment;
  bool directive;
  bool slash;
} Gap;

static void advance(TSLexer *lexer) { lexer->advance(lexer, false); }

static void skip(TSLexer *lexer) { lexer->advance(lexer, true); }

static bool at_text_end(TSLexer *lexer) { return lexer->eof(lexer) || lexer->lookahead == 0; }

static bool is_word_char(int32_t c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static bool is_digit(int32_t c) { return c >= '0' && c <= '9'; }

static bool is_hex_digit(int32_t c) { return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

static bool is_alnum(int32_t c) { return is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

static bool is_directive_char(int32_t c) { return is_word_char(c) || c == ':' || c == '-'; }

static bool skip_position_directive(TSLexer *lexer, Gap *gap) {
  static const char prefix[] = "pos:";
  for (const char *p = prefix; *p; p++) {
    if (lexer->lookahead != *p) {
      return false;
    }
    skip(lexer);
  }
  if (!is_digit(lexer->lookahead)) {
    return false;
  }
  while (is_digit(lexer->lookahead)) {
    skip(lexer);
  }
  if (lexer->lookahead != ':') {
    return false;
  }
  skip(lexer);
  if (!is_digit(lexer->lookahead)) {
    return false;
  }
  while (is_directive_char(lexer->lookahead)) {
    skip(lexer);
  }
  gap->crossed_comment = true;
  return true;
}

static void skip_gap(TSLexer *lexer, Gap *gap) {
  for (;;) {
    int32_t c = lexer->lookahead;
    if (c == ' ' || c == '\t' || c == '\r') {
      skip(lexer);
    } else if (c == '\n') {
      gap->newline = true;
      skip(lexer);
    } else if (c == '/') {
      skip(lexer);
      if (lexer->lookahead == '/') {
        while (!at_text_end(lexer) && lexer->lookahead != '\n') {
          skip(lexer);
        }
        gap->crossed_comment = true;
      } else if (lexer->lookahead == '*') {
        skip(lexer);
        for (;;) {
          if (at_text_end(lexer)) {
            gap->crossed_comment = true;
            return;
          }
          int32_t inner = lexer->lookahead;
          skip(lexer);
          if (inner == '*' && lexer->lookahead == '/') {
            skip(lexer);
            break;
          }
        }
        gap->crossed_comment = true;
      } else {
        gap->slash = true;
        return;
      }
    } else if (c == '#') {
      skip(lexer);
      if (!skip_position_directive(lexer, gap)) {
        gap->directive = true;
        return;
      }
    } else {
      return;
    }
  }
}

static bool read_word(TSLexer *lexer, char *buffer, unsigned size) {
  unsigned length = 0;
  while (is_word_char(lexer->lookahead)) {
    if (length + 1 >= size) {
      return false;
    }
    buffer[length++] = (char)lexer->lookahead;
    advance(lexer);
  }
  buffer[length] = '\0';
  return length > 0;
}

static bool accept(TSLexer *lexer, enum TokenType token) {
  lexer->result_symbol = token;
  return true;
}

static bool accept_terminator(Scanner *scanner, TSLexer *lexer, enum TokenType token) {
  advance(lexer);
  lexer->mark_end(lexer);
  scanner->after_terminator = true;
  return accept(lexer, token);
}

// constraint: the compiler lets one `}` or `;` end each statement that it closes, so nested bodies take one `;` each
static bool accept_statement_end(
  Scanner *scanner,
  TSLexer *lexer,
  enum TokenType statement_end,
  bool after_terminator,
  const Gap *gap
) {
  scanner->after_terminator = after_terminator && !gap->newline;
  return accept(lexer, statement_end);
}

static void advance_digit(TSLexer *lexer) {
  do {
    advance(lexer);
  } while (lexer->lookahead == '_');
}

static bool reject_number(TSLexer *lexer) {
  while (is_word_char(lexer->lookahead) || lexer->lookahead == '.') {
    advance(lexer);
  }
  lexer->mark_end(lexer);
  return accept(lexer, MALFORMED_NUMBER);
}

// constraint: SQInteger has 64 bits, and the compiler has no negative integer literal
static const char INTEGER_MAX[] = "9223372036854775807";
enum { HEX_DIGITS_MAX = 16 };

// constraint: SQFloat has 32 bits in the engine build; a literal is an error if it rounds to FLT_MAX or more, or to 0
// constraint: the compiler has two float rounding paths; each limit is that of the path that accepts more
static const char FLOAT_OVERFLOW_ABOVE[] = "340282336497324076875334902989472137216";
enum { FLOAT_OVERFLOW_EXPONENT = 38 };
static const char FLOAT_UNDERFLOW_UP_TO[] =
  "700649232162408535461864791644958065640130970938257885878534141944895541342930300743319094181060791015625";
enum { FLOAT_UNDERFLOW_EXPONENT = -46 };

enum { SIGNIFICANT_DIGITS_KEPT = 112, EXPONENT_LIMIT = 100000 };

typedef struct {
  char significant[SIGNIFICANT_DIGITS_KEPT];
  unsigned kept;
  bool dropped_nonzero;
  int32_t integer_digits;
  int32_t leading_integer_zeros;
  int32_t leading_fraction_zeros;
  bool in_fraction;
  int32_t exponent;
  bool exponent_is_negative;
} NumberValue;

static void add_mantissa_digit(NumberValue *value, int32_t digit) {
  if (!value->in_fraction) {
    value->integer_digits++;
  }
  if (value->kept == 0 && digit == '0') {
    if (value->in_fraction) {
      value->leading_fraction_zeros++;
    } else {
      value->leading_integer_zeros++;
    }
    return;
  }
  if (value->kept < SIGNIFICANT_DIGITS_KEPT) {
    value->significant[value->kept++] = (char)digit;
  } else if (digit != '0') {
    value->dropped_nonzero = true;
  }
}

static void add_exponent_digit(NumberValue *value, int32_t digit) {
  if (value->exponent < EXPONENT_LIMIT) {
    value->exponent = value->exponent * 10 + (digit - '0');
  }
}

static int compare_with_limit(const NumberValue *value, const char *limit) {
  for (unsigned i = 0; i < SIGNIFICANT_DIGITS_KEPT; i++) {
    char digit = i < value->kept ? value->significant[i] : '0';
    char limit_digit = *limit ? *limit++ : '0';
    if (digit != limit_digit) {
      return digit < limit_digit ? -1 : 1;
    }
  }
  return value->dropped_nonzero ? 1 : 0;
}

static bool is_integer_in_range(const NumberValue *value) {
  unsigned digits = (unsigned)(value->integer_digits - value->leading_integer_zeros);
  unsigned max_digits = sizeof INTEGER_MAX - 1;
  return digits < max_digits || (digits == max_digits && compare_with_limit(value, INTEGER_MAX) <= 0);
}

static bool is_float_in_range(const NumberValue *value) {
  if (value->kept == 0) {
    return true;
  }
  int32_t first_digit_exponent = value->leading_integer_zeros < value->integer_digits
                                   ? value->integer_digits - value->leading_integer_zeros - 1
                                   : -(value->leading_fraction_zeros + 1);
  first_digit_exponent += value->exponent_is_negative ? -value->exponent : value->exponent;
  if (first_digit_exponent != FLOAT_OVERFLOW_EXPONENT && first_digit_exponent != FLOAT_UNDERFLOW_EXPONENT) {
    return first_digit_exponent > FLOAT_UNDERFLOW_EXPONENT && first_digit_exponent < FLOAT_OVERFLOW_EXPONENT;
  }
  if (first_digit_exponent == FLOAT_OVERFLOW_EXPONENT) {
    return compare_with_limit(value, FLOAT_OVERFLOW_ABOVE) <= 0;
  }
  return compare_with_limit(value, FLOAT_UNDERFLOW_UP_TO) > 0;
}

// constraint: follows SQLexer::ReadNumber of the compiler
static bool scan_number(TSLexer *lexer) {
  int32_t first = lexer->lookahead;
  advance_digit(lexer);
  if (first == '0' && is_digit(lexer->lookahead)) {
    return reject_number(lexer);
  }
  if (first == '0' && (lexer->lookahead == 'x' || lexer->lookahead == 'X')) {
    advance_digit(lexer);
    unsigned hex_digits = 0;
    while (is_hex_digit(lexer->lookahead)) {
      hex_digits++;
      advance_digit(lexer);
    }
    if (hex_digits == 0 || hex_digits > HEX_DIGITS_MAX) {
      return reject_number(lexer);
    }
    lexer->mark_end(lexer);
    return accept(lexer, INTEGER);
  }
  NumberValue value = {0};
  add_mantissa_digit(&value, first);
  bool has_dot = false;
  bool has_exponent = false;
  while (lexer->lookahead == '.' || is_alnum(lexer->lookahead)) {
    int32_t c = lexer->lookahead;
    if (c == '.') {
      if (has_dot || has_exponent) {
        return reject_number(lexer);
      }
      has_dot = true;
      value.in_fraction = true;
    } else if (c == 'e' || c == 'E') {
      if (has_exponent) {
        return reject_number(lexer);
      }
      has_exponent = true;
      advance(lexer);
      if (lexer->lookahead == '+' || lexer->lookahead == '-') {
        value.exponent_is_negative = lexer->lookahead == '-';
        advance(lexer);
      }
      if (!is_digit(lexer->lookahead)) {
        return reject_number(lexer);
      }
      add_exponent_digit(&value, lexer->lookahead);
    } else if (!is_digit(c)) {
      return reject_number(lexer);
    } else if (has_exponent) {
      add_exponent_digit(&value, c);
    } else {
      add_mantissa_digit(&value, c);
    }
    advance_digit(lexer);
  }
  bool is_float = has_dot || has_exponent;
  if (is_float ? !is_float_in_range(&value) : !is_integer_in_range(&value)) {
    return reject_number(lexer);
  }
  lexer->mark_end(lexer);
  return accept(lexer, is_float ? FLOAT : INTEGER);
}

static bool scan_template_chars(TSLexer *lexer) {
  bool has_content = false;
  for (;;) {
    int32_t c = lexer->lookahead;
    if (at_text_end(lexer) || c == '"' || c == '{' || c == '\\') {
      break;
    }
    if (c == '\n') {
      if (has_content) {
        break;
      }
      advance(lexer);
      lexer->mark_end(lexer);
      return accept(lexer, UNEXPECTED_NEWLINE);
    }
    advance(lexer);
    has_content = true;
  }
  if (!has_content) {
    return false;
  }
  lexer->mark_end(lexer);
  return accept(lexer, TEMPLATE_CHARS);
}

static bool scan_text_after_nul(TSLexer *lexer) {
  while (!lexer->eof(lexer)) {
    advance(lexer);
  }
  lexer->mark_end(lexer);
  return accept(lexer, TEXT_AFTER_NUL);
}

static bool scan_in_error_recovery(Scanner *scanner, TSLexer *lexer) {
  while (lexer->lookahead == ' ' || lexer->lookahead == '\t' || lexer->lookahead == '\r' || lexer->lookahead == '\n') {
    skip(lexer);
  }
  if (lexer->lookahead == '}') {
    return accept_terminator(scanner, lexer, CLOSE_BRACE);
  }
  if (lexer->lookahead == ';') {
    return accept_terminator(scanner, lexer, SEMICOLON);
  }
  if (lexer->lookahead == 0 && !lexer->eof(lexer)) {
    return scan_text_after_nul(lexer);
  }
  if (is_digit(lexer->lookahead)) {
    return scan_number(lexer);
  }
  return false;
}

// constraint: the compiler takes `[`, `?[`, `++` and `--` as postfix operators only on the line of their operand
static bool scan_postfix(TSLexer *lexer, const bool *valid_symbols, const Gap *gap, enum TokenType statement_end) {
  int32_t c = lexer->lookahead;
  if (c == '[') {
    if (gap->crossed_comment) {
      return false;
    }
    advance(lexer);
    lexer->mark_end(lexer);
    return accept(lexer, gap->newline ? UNEXPECTED_NEWLINE : INDEX_BRACKET);
  }
  if (c == '?') {
    if (gap->crossed_comment) {
      return false;
    }
    advance(lexer);
    if (lexer->lookahead != '[' || !valid_symbols[NULL_INDEX_BRACKET]) {
      return false;
    }
    advance(lexer);
    lexer->mark_end(lexer);
    return accept(lexer, gap->newline ? UNEXPECTED_NEWLINE : NULL_INDEX_BRACKET);
  }
  advance(lexer);
  if (lexer->lookahead != c) {
    return false;
  }
  if (gap->newline) {
    return valid_symbols[statement_end] && accept(lexer, statement_end);
  }
  enum TokenType token = c == '+' ? POSTFIX_INCREMENT : POSTFIX_DECREMENT;
  if (!valid_symbols[token] || gap->crossed_comment) {
    return false;
  }
  advance(lexer);
  lexer->mark_end(lexer);
  return accept(lexer, token);
}

typedef struct {
  bool call;
  bool binary_minus;
  bool import_alias;
} Continuations;

// constraint: the compiler ends a statement at a line end only where the next token can start a new statement
static bool continues_statement(TSLexer *lexer, const Continuations *can_take) {
  int32_t c = lexer->lookahead;
  switch (c) {
    case '=':
    case ',':
    case ')':
    case ']':
    case '.':
    case '?':
    case '*':
    case '/':
    case '%':
    case '<':
    case '>':
    case '|':
    case '&':
    case '^':
      return true;
    case ':':
      advance(lexer);
      return lexer->lookahead != ':';
    case '+':
      advance(lexer);
      return lexer->lookahead != '+';
    case '!':
      advance(lexer);
      return lexer->lookahead == '=';
    case '-':
      advance(lexer);
      return lexer->lookahead == '=' || (can_take->binary_minus && lexer->lookahead != '-');
    case '(':
      return can_take->call;
    case 'a': {
      char word[16];
      return can_take->import_alias && read_word(lexer, word, sizeof word) && strcmp(word, "as") == 0;
    }
    case 'i':
    case 'n': {
      char word[16];
      if (!read_word(lexer, word, sizeof word)) {
        return false;
      }
      return strcmp(word, "in") == 0 || strcmp(word, "instanceof") == 0 || strcmp(word, "not") == 0;
    }
    default:
      return false;
  }
}

static bool next_word_is(TSLexer *lexer, const char *expected) {
  char word[16];
  return read_word(lexer, word, sizeof word) && strcmp(word, expected) == 0;
}

static bool next_word_starts_import(TSLexer *lexer) {
  char word[16];
  return read_word(lexer, word, sizeof word) && (strcmp(word, "import") == 0 || strcmp(word, "from") == 0);
}

void *tree_sitter_quirrel_external_scanner_create(void) { return ts_calloc(1, sizeof(Scanner)); }

void tree_sitter_quirrel_external_scanner_destroy(void *payload) { ts_free(payload); }

unsigned tree_sitter_quirrel_external_scanner_serialize(void *payload, char *buffer) {
  Scanner *scanner = payload;
  buffer[0] = (char)scanner->after_terminator;
  return 1;
}

void tree_sitter_quirrel_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
  Scanner *scanner = payload;
  scanner->after_terminator = length == 1 && buffer[0] != 0;
}

static bool scan_token(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols, bool after_terminator) {
  if (valid_symbols[TEMPLATE_CHARS]) {
    return scan_template_chars(lexer);
  }
  if (valid_symbols[NAME_ADJACENT]) {
    int32_t c = lexer->lookahead;
    return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') && accept(lexer, NAME_ADJACENT);
  }

  Gap gap = {false, false, false, false};
  skip_gap(lexer, &gap);

  // constraint: the compiler ends a `const` declaration only at `;`, a line end, `}`, or the text end, also after `}`
  bool ends_const_declaration = valid_symbols[CONST_DECLARATION_END];
  enum TokenType statement_end = ends_const_declaration ? CONST_DECLARATION_END : AUTOMATIC_SEMICOLON;
  bool may_terminate = valid_symbols[statement_end];
  bool expression_ended = valid_symbols[INDEX_BRACKET];
  bool line_break = gap.newline || (after_terminator && !ends_const_declaration);

  if (gap.directive) {
    return may_terminate && accept(lexer, statement_end);
  }
  if (gap.slash) {
    return false;
  }

  int32_t c = lexer->lookahead;
  if (at_text_end(lexer)) {
    if (may_terminate) {
      return accept(lexer, statement_end);
    }
    if (!lexer->eof(lexer) && !gap.crossed_comment) {
      return scan_text_after_nul(lexer);
    }
    return false;
  }
  if (c == '}') {
    if (may_terminate) {
      return accept(lexer, statement_end);
    }
    return valid_symbols[CLOSE_BRACE] && !gap.crossed_comment && accept_terminator(scanner, lexer, CLOSE_BRACE);
  }
  if (c == ';') {
    if (ends_const_declaration) {
      return accept(lexer, CONST_DECLARATION_END);
    }
    return valid_symbols[SEMICOLON] && !gap.crossed_comment && accept_terminator(scanner, lexer, SEMICOLON);
  }
  if (c == '|' && valid_symbols[SAME_LINE_TYPE_BAR] && !gap.newline) {
    advance(lexer);
    lexer->mark_end(lexer);
    return accept(lexer, SAME_LINE_TYPE_BAR);
  }
  if (expression_ended && (c == '[' || c == '?' || c == '+' || c == '-')) {
    return scan_postfix(lexer, valid_symbols, &gap, statement_end);
  }
  if (!may_terminate || !line_break) {
    if (is_digit(c)) {
      return valid_symbols[INTEGER] && scan_number(lexer);
    }
    return valid_symbols[IMPORT_MARKER] && next_word_starts_import(lexer) && accept(lexer, IMPORT_MARKER);
  }
  if ((c == 'e' && valid_symbols[ELSE_MARKER]) || (c == 'c' && valid_symbols[CATCH_MARKER])) {
    return !next_word_is(lexer, c == 'e' ? "else" : "catch") &&
           accept_statement_end(scanner, lexer, statement_end, after_terminator, &gap);
  }
  // constraint: the compiler takes no postfix operator after `x++`, but a binary operator can follow it
  Continuations can_take = {
    .call = expression_ended,
    .binary_minus = expression_ended || valid_symbols[AFTER_POSTFIX_UPDATE],
    .import_alias = valid_symbols[BEFORE_IMPORT_ALIAS],
  };
  if (continues_statement(lexer, &can_take)) {
    return false;
  }
  return accept_statement_end(scanner, lexer, statement_end, after_terminator, &gap);
}

bool tree_sitter_quirrel_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
  Scanner *scanner = payload;
  bool after_terminator = scanner->after_terminator;
  scanner->after_terminator = false;

  if (valid_symbols[ERROR_SENTINEL]) {
    return scan_in_error_recovery(scanner, lexer);
  }
  lexer->mark_end(lexer);
  if (scan_token(scanner, lexer, valid_symbols, after_terminator)) {
    return true;
  }
  return after_terminator && valid_symbols[TERMINATOR_RESET] && accept(lexer, TERMINATOR_RESET);
}
