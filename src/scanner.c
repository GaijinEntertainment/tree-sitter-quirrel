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
  ERROR_SENTINEL,
};

// constraint: the runtime restores this state from the last external token, so a scan after a terminator returns one
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

// constraint: follows SQLexer::ReadNumber of the compiler, except that value limits stay unchecked
static bool scan_number(TSLexer *lexer) {
  int32_t first = lexer->lookahead;
  advance_digit(lexer);
  if (first == '0' && is_digit(lexer->lookahead)) {
    return reject_number(lexer);
  }
  if (first == '0' && (lexer->lookahead == 'x' || lexer->lookahead == 'X')) {
    advance_digit(lexer);
    if (!is_hex_digit(lexer->lookahead)) {
      return reject_number(lexer);
    }
    while (is_hex_digit(lexer->lookahead)) {
      advance_digit(lexer);
    }
    lexer->mark_end(lexer);
    return accept(lexer, INTEGER);
  }
  bool has_dot = false;
  bool has_exponent = false;
  while (lexer->lookahead == '.' || is_alnum(lexer->lookahead)) {
    int32_t c = lexer->lookahead;
    if (c == '.') {
      if (has_dot || has_exponent) {
        return reject_number(lexer);
      }
      has_dot = true;
    } else if (c == 'e' || c == 'E') {
      if (has_exponent) {
        return reject_number(lexer);
      }
      has_exponent = true;
      advance(lexer);
      if (lexer->lookahead == '+' || lexer->lookahead == '-') {
        advance(lexer);
      }
      if (!is_digit(lexer->lookahead)) {
        return reject_number(lexer);
      }
    } else if (!is_digit(c)) {
      return reject_number(lexer);
    }
    advance_digit(lexer);
  }
  lexer->mark_end(lexer);
  return accept(lexer, has_dot || has_exponent ? FLOAT : INTEGER);
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
static bool scan_postfix(TSLexer *lexer, const bool *valid_symbols, const Gap *gap, bool may_terminate) {
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
    return may_terminate && accept(lexer, AUTOMATIC_SEMICOLON);
  }
  enum TokenType token = c == '+' ? POSTFIX_INCREMENT : POSTFIX_DECREMENT;
  if (!valid_symbols[token] || gap->crossed_comment) {
    return false;
  }
  advance(lexer);
  lexer->mark_end(lexer);
  return accept(lexer, token);
}

// constraint: the compiler ends a statement at a line end only where the next token can start a new statement
static bool continues_statement(TSLexer *lexer, bool expression_ended) {
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
      return lexer->lookahead == '=' || (expression_ended && lexer->lookahead != '-');
    case '(':
      return expression_ended;
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

  bool may_terminate = valid_symbols[AUTOMATIC_SEMICOLON];
  bool expression_ended = valid_symbols[INDEX_BRACKET];
  bool line_break = gap.newline || after_terminator;

  if (gap.directive) {
    return may_terminate && accept(lexer, AUTOMATIC_SEMICOLON);
  }
  if (gap.slash) {
    return false;
  }

  int32_t c = lexer->lookahead;
  if (at_text_end(lexer)) {
    if (may_terminate) {
      return accept(lexer, AUTOMATIC_SEMICOLON);
    }
    if (!lexer->eof(lexer) && !gap.crossed_comment) {
      return scan_text_after_nul(lexer);
    }
    return false;
  }
  if (c == '}') {
    if (may_terminate) {
      return accept(lexer, AUTOMATIC_SEMICOLON);
    }
    return valid_symbols[CLOSE_BRACE] && !gap.crossed_comment && accept_terminator(scanner, lexer, CLOSE_BRACE);
  }
  if (c == ';') {
    return valid_symbols[SEMICOLON] && !gap.crossed_comment && accept_terminator(scanner, lexer, SEMICOLON);
  }
  if (c == '|' && valid_symbols[SAME_LINE_TYPE_BAR] && !gap.newline) {
    advance(lexer);
    lexer->mark_end(lexer);
    return accept(lexer, SAME_LINE_TYPE_BAR);
  }
  if (expression_ended && (c == '[' || c == '?' || c == '+' || c == '-')) {
    return scan_postfix(lexer, valid_symbols, &gap, may_terminate);
  }
  if (!may_terminate || !line_break) {
    if (is_digit(c)) {
      return valid_symbols[INTEGER] && scan_number(lexer);
    }
    return valid_symbols[IMPORT_MARKER] && next_word_starts_import(lexer) && accept(lexer, IMPORT_MARKER);
  }
  if ((c == 'e' && valid_symbols[ELSE_MARKER]) || (c == 'c' && valid_symbols[CATCH_MARKER])) {
    return !next_word_is(lexer, c == 'e' ? "else" : "catch") && accept(lexer, AUTOMATIC_SEMICOLON);
  }
  if (continues_statement(lexer, expression_ended)) {
    return false;
  }
  return accept(lexer, AUTOMATIC_SEMICOLON);
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
