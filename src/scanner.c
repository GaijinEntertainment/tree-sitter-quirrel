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
  FOREACH_INDEX_MARKER,
  ENUM_MEMBERS_MARKER,
  AFTER_CLONE,
  AFTER_OPENING_ITEM,
  AFTER_BLOCK,
  NEVER_RETURNED,
  ERROR_SENTINEL,
};

// constraint: the runtime restores this state from the last external token, so each scan that changes it returns one
typedef struct {
  bool after_terminator;
  bool imports_closed;
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
    // constraint: after the clone operator the compiler reads `[` on a later line as the start of an array
    bool breaks_index = gap->newline && !valid_symbols[AFTER_CLONE];
    return accept(lexer, breaks_index ? UNEXPECTED_NEWLINE : INDEX_BRACKET);
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

// constraint: where `clone` is an operator, the compiler reads its operand from the next line; these words start an
// operand there and no statement, except in the declaration forms `const name =`, `const function`, and `class name`
static bool starts_operand_and_no_statement(TSLexer *lexer) {
  char word[16];
  Gap gap = {false, false, false, false};
  if (!read_word(lexer, word, sizeof word)) {
    return false;
  }
  if (strcmp(word, "function") == 0 || strcmp(word, "async") == 0) {
    return true;
  }
  bool is_const = strcmp(word, "const") == 0;
  if (!is_const && strcmp(word, "class") != 0) {
    return false;
  }
  skip_gap(lexer, &gap);
  if (!is_word_char(lexer->lookahead) || is_digit(lexer->lookahead)) {
    return !gap.slash && !gap.directive;
  }
  if (!is_const || !read_word(lexer, word, sizeof word) || strcmp(word, "function") == 0) {
    return false;
  }
  skip_gap(lexer, &gap);
  if (lexer->lookahead != '=') {
    return true;
  }
  advance(lexer);
  return lexer->lookahead == '=';
}

typedef struct {
  char *text;
  unsigned size;
  unsigned capacity;
  unsigned count;
} NameList;

static void name_list_append(NameList *names, char c) {
  if (names->size == names->capacity) {
    names->capacity = names->capacity ? names->capacity * 2 : 256;
    names->text = ts_realloc(names->text, names->capacity);
  }
  names->text[names->size++] = c;
}

static bool starts_name(int32_t c) { return is_word_char(c) && !is_digit(c); }

static void read_name(TSLexer *lexer, NameList *names) {
  while (is_word_char(lexer->lookahead)) {
    name_list_append(names, (char)lexer->lookahead);
    advance(lexer);
  }
  name_list_append(names, '\0');
  names->count++;
}

static bool is_next_word(TSLexer *lexer, const char *expected) {
  while (is_word_char(lexer->lookahead) && lexer->lookahead == *expected) {
    expected++;
    advance(lexer);
  }
  return *expected == '\0' && !is_word_char(lexer->lookahead);
}

// constraint: the compiler rejects a `foreach` whose index and value are the same name
static bool scan_foreach_index_marker(TSLexer *lexer) {
  Gap gap = {false, false, false, false};
  skip_gap(lexer, &gap);
  if (!starts_name(lexer->lookahead)) {
    return false;
  }
  NameList index = {NULL, 0, 0, 0};
  read_name(lexer, &index);
  skip_gap(lexer, &gap);
  bool has_index = !gap.slash && !gap.directive && lexer->lookahead == ',';
  bool has_same_value = false;
  if (has_index) {
    advance(lexer);
    skip_gap(lexer, &gap);
    has_same_value = is_next_word(lexer, index.text);
  }
  ts_free(index.text);
  return has_index && !has_same_value && accept(lexer, FOREACH_INDEX_MARKER);
}

static bool name_list_holds_last_name_twice(const NameList *names, unsigned last_name) {
  for (unsigned start = 0; start < last_name; start += (unsigned)strlen(names->text + start) + 1) {
    if (strcmp(names->text + start, names->text + last_name) == 0) {
      return true;
    }
  }
  return false;
}

static bool skip_quoted(TSLexer *lexer, int32_t quote, bool verbatim) {
  advance(lexer);
  for (;;) {
    int32_t c = lexer->lookahead;
    if (at_text_end(lexer) || (c == '\n' && !verbatim)) {
      return false;
    }
    advance(lexer);
    if (c == '\\' && !verbatim) {
      advance(lexer);
    } else if (c == quote) {
      if (!verbatim || lexer->lookahead != quote) {
        return true;
      }
      advance(lexer);
    }
  }
}

static bool skip_enum_value(TSLexer *lexer) {
  if (lexer->lookahead == '-') {
    advance(lexer);
  }
  int32_t c = lexer->lookahead;
  if (c == '"' || c == '\'') {
    return skip_quoted(lexer, c, false);
  }
  if (c == '@') {
    advance(lexer);
    return lexer->lookahead == '"' && skip_quoted(lexer, '"', true);
  }
  if (!is_word_char(c)) {
    return false;
  }
  if (!is_digit(c)) {
    while (is_word_char(lexer->lookahead)) {
      advance(lexer);
    }
    return true;
  }
  advance_digit(lexer);
  if (c == '0' && (lexer->lookahead == 'x' || lexer->lookahead == 'X')) {
    advance_digit(lexer);
    while (is_hex_digit(lexer->lookahead)) {
      advance_digit(lexer);
    }
    return true;
  }
  while (is_alnum(lexer->lookahead) || lexer->lookahead == '.') {
    int32_t previous = lexer->lookahead;
    advance_digit(lexer);
    if ((previous == 'e' || previous == 'E') && (lexer->lookahead == '+' || lexer->lookahead == '-')) {
      advance(lexer);
    }
  }
  return true;
}

enum { ENUM_MEMBERS_COMPARED_MAX = 4096 };

// constraint: the compiler rejects an enum that has two members of the same name
// shortcut: the scan compares the first 4096 members - use a hash set when a larger enum needs the check
static bool scan_enum_members_marker(TSLexer *lexer) {
  NameList names = {NULL, 0, 0, 0};
  bool has_duplicate = false;
  while (!has_duplicate && names.count < ENUM_MEMBERS_COMPARED_MAX) {
    Gap gap = {false, false, false, false};
    skip_gap(lexer, &gap);
    if (gap.slash || gap.directive || !starts_name(lexer->lookahead)) {
      break;
    }
    unsigned name = names.size;
    read_name(lexer, &names);
    has_duplicate = name_list_holds_last_name_twice(&names, name);
    skip_gap(lexer, &gap);
    if (lexer->lookahead == '=' && !gap.slash && !gap.directive) {
      advance(lexer);
      skip_gap(lexer, &gap);
      if (gap.slash || gap.directive || !skip_enum_value(lexer)) {
        break;
      }
      skip_gap(lexer, &gap);
    }
    if (lexer->lookahead == ',' && !gap.slash && !gap.directive) {
      advance(lexer);
    }
  }
  ts_free(names.text);
  return !has_duplicate && accept(lexer, ENUM_MEMBERS_MARKER);
}

enum { TEMPLATE_NESTING_MAX = 32 };

typedef struct {
  unsigned open_brackets;
  unsigned hole_levels[TEMPLATE_NESTING_MAX];
  unsigned open_holes;
  bool in_template_text;
} CodeNesting;

static bool read_word_outside_brackets(TSLexer *lexer, CodeNesting *nesting, char *word, unsigned size) {
  for (;;) {
    if (nesting->in_template_text) {
      int32_t c = lexer->lookahead;
      if (at_text_end(lexer) || c == '\n') {
        return false;
      }
      advance(lexer);
      if (c == '\\') {
        advance(lexer);
      } else if (c == '"') {
        nesting->in_template_text = false;
      } else if (c == '{') {
        if (nesting->open_holes == TEMPLATE_NESTING_MAX) {
          return false;
        }
        nesting->hole_levels[nesting->open_holes++] = nesting->open_brackets++;
        nesting->in_template_text = false;
      }
      continue;
    }
    Gap gap = {false, false, false, false};
    skip_gap(lexer, &gap);
    if (at_text_end(lexer)) {
      return false;
    }
    int32_t c = lexer->lookahead;
    if (is_word_char(c)) {
      bool fits = read_word(lexer, word, size);
      while (is_word_char(lexer->lookahead)) {
        advance(lexer);
      }
      if (fits && nesting->open_brackets == 0) {
        return true;
      }
    } else if (c == '"' || c == '\'') {
      if (!skip_quoted(lexer, c, false)) {
        return false;
      }
    } else if (c == '@') {
      advance(lexer);
      if (lexer->lookahead == '@') {
        advance(lexer);
      }
      if (lexer->lookahead == '"' && !skip_quoted(lexer, '"', true)) {
        return false;
      }
    } else if (c == '$') {
      advance(lexer);
      if (lexer->lookahead == '"') {
        advance(lexer);
        nesting->in_template_text = true;
      }
    } else if (c == '{' || c == '(' || c == '[') {
      advance(lexer);
      nesting->open_brackets++;
    } else if (c == '}' || c == ')' || c == ']') {
      if (nesting->open_brackets == 0) {
        return false;
      }
      advance(lexer);
      nesting->open_brackets--;
      if (nesting->open_holes > 0 && nesting->hole_levels[nesting->open_holes - 1] == nesting->open_brackets) {
        nesting->open_holes--;
        nesting->in_template_text = true;
      }
    } else {
      advance(lexer);
    }
  }
}

typedef struct {
  NameList types;
  bool has_catch_all;
} CatchChain;

static bool breaks_catch_chain(TSLexer *lexer, CodeNesting *nesting, CatchChain *chain) {
  if (chain->has_catch_all) {
    return true;
  }
  Gap gap = {false, false, false, false};
  skip_gap(lexer, &gap);
  if (lexer->lookahead != '(') {
    return false;
  }
  advance(lexer);
  nesting->open_brackets++;
  skip_gap(lexer, &gap);
  if (!starts_name(lexer->lookahead)) {
    return false;
  }
  unsigned type = chain->types.size;
  read_name(lexer, &chain->types);
  skip_gap(lexer, &gap);
  if (starts_name(lexer->lookahead)) {
    return name_list_holds_last_name_twice(&chain->types, type);
  }
  chain->types.size = type;
  chain->has_catch_all = lexer->lookahead == ')';
  return false;
}

// constraint: the compiler gives a `catch` to the nearest `try`, and rejects a second clause for one type and a clause
// after the catch-all clause
// shortcut: the scan stops at 32 nested template strings - use a growing list when deeper text needs the check
static bool is_valid_catch_chain(TSLexer *lexer) {
  CatchChain chain = {{NULL, 0, 0, 0}, false};
  CodeNesting nesting = {0, {0}, 0, false};
  char word[8];
  bool is_broken = breaks_catch_chain(lexer, &nesting, &chain);
  while (!is_broken && read_word_outside_brackets(lexer, &nesting, word, sizeof(word))) {
    if (strcmp(word, "try") == 0) {
      break;
    }
    if (strcmp(word, "catch") == 0) {
      is_broken = breaks_catch_chain(lexer, &nesting, &chain);
    }
  }
  ts_free(chain.types.text);
  return !is_broken;
}

void *tree_sitter_quirrel_external_scanner_create(void) { return ts_calloc(1, sizeof(Scanner)); }

void tree_sitter_quirrel_external_scanner_destroy(void *payload) { ts_free(payload); }

unsigned tree_sitter_quirrel_external_scanner_serialize(void *payload, char *buffer) {
  Scanner *scanner = payload;
  buffer[0] = (char)scanner->after_terminator;
  buffer[1] = (char)scanner->imports_closed;
  return 2;
}

void tree_sitter_quirrel_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
  Scanner *scanner = payload;
  scanner->after_terminator = length == 2 && buffer[0] != 0;
  scanner->imports_closed = length == 2 && buffer[1] != 0;
}

static bool scan_token(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols, bool after_terminator) {
  if (valid_symbols[TEMPLATE_CHARS]) {
    return scan_template_chars(lexer);
  }
  if (valid_symbols[NAME_ADJACENT]) {
    int32_t c = lexer->lookahead;
    return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') && accept(lexer, NAME_ADJACENT);
  }
  if (valid_symbols[FOREACH_INDEX_MARKER]) {
    return scan_foreach_index_marker(lexer);
  }
  if (valid_symbols[ENUM_MEMBERS_MARKER]) {
    return scan_enum_members_marker(lexer);
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
    // constraint: the compiler takes a `;` on the line of the `}` or `;` that ended a statement as an empty statement
    if (after_terminator && !gap.newline && may_terminate) {
      return accept_statement_end(scanner, lexer, statement_end, after_terminator, &gap);
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
  if (c == 'c' && valid_symbols[CATCH_MARKER]) {
    if (is_next_word(lexer, "catch")) {
      return is_valid_catch_chain(lexer) && accept(lexer, CATCH_MARKER);
    }
    return may_terminate && line_break && accept_statement_end(scanner, lexer, statement_end, after_terminator, &gap);
  }
  if (!may_terminate || !line_break) {
    if (is_digit(c)) {
      return valid_symbols[INTEGER] && scan_number(lexer);
    }
    bool starts_import = valid_symbols[IMPORT_MARKER] && !scanner->imports_closed && next_word_starts_import(lexer);
    return starts_import && accept(lexer, IMPORT_MARKER);
  }
  if (c == 'e' && valid_symbols[ELSE_MARKER]) {
    return !next_word_is(lexer, "else") && accept_statement_end(scanner, lexer, statement_end, after_terminator, &gap);
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
  if (valid_symbols[AFTER_CLONE] && gap.newline && starts_operand_and_no_statement(lexer)) {
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
  // constraint: the compiler takes `import` and `from` as names once an opening statement or a function body ended
  bool closes_imports = valid_symbols[AFTER_OPENING_ITEM] || valid_symbols[AFTER_BLOCK];
  bool changes_state = after_terminator || (closes_imports && !scanner->imports_closed);
  scanner->imports_closed = scanner->imports_closed || closes_imports;
  if (scan_token(scanner, lexer, valid_symbols, after_terminator)) {
    return true;
  }
  return changes_state && valid_symbols[TERMINATOR_RESET] && accept(lexer, TERMINATOR_RESET);
}
