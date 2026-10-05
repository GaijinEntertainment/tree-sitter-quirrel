/**
 * @file Dagor Quirrel grammar for tree-sitter
 * @author Gaijin Entertainment
 * @author Anton Zinovyev <xog3@yandex.ru>
 * @license MIT
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

const PREC = {
  LAMBDA: -1,
  ASSIGN: 1,
  NULL_COALESCE: 2,
  OR: 3,
  AND: 4,
  BIT_OR: 5,
  BIT_XOR: 6,
  BIT_AND: 7,
  EQUALITY: 8,
  RELATIONAL: 9,
  SHIFT: 10,
  ADDITIVE: 11,
  MULTIPLICATIVE: 12,
  UNARY: 13,
  POSTFIX: 14,
  FOLDED_LITERAL: 15,
};

const TYPE_NAMES = [
  'bool', 'number', 'int', 'float', 'string', 'table', 'array', 'userdata', 'function', 'generator',
  'userpointer', 'thread', 'instance', 'class', 'weakref', 'null', 'any',
];

const CONTEXTUAL_NAMES = ['switch', 'case', 'default', 'clone', 'import', 'from', 'as'];

const DIRECTIVES = [
  'strict', 'relaxed', 'forbid-root-table', 'allow-root-table', 'disable-optimizer', 'enable-optimizer',
  'forbid-delete-operator', 'allow-delete-operator', 'forbid-clone-operator', 'allow-clone-operator',
  'forbid-switch-statement', 'allow-switch-statement', 'forbid-implicit-type-methods',
  'allow-implicit-type-methods', 'forbid-auto-freeze', 'allow-auto-freeze', 'forbid-compiler-internals',
  'allow-compiler-internals',
];

const HEX = '[0-9a-fA-F]';
const SURROGATE = `[dD][89a-fA-F]${HEX}{2}`;
const NOT_SURROGATE = `([0-9a-ce-fA-CE-F]${HEX}{3}|[dD][0-7]${HEX}{2})`;

// constraint: the compiler reads up to 4 digits after `\u`, 8 after `\U`; a surrogate or a value above 0x10FFFF errors
const UNICODE_ESCAPE = [
  `u(${HEX}{1,3}|${NOT_SURROGATE})`,
  `U(${HEX}{1,3}|${NOT_SURROGATE}|[1-9a-fA-F]${HEX}{4}|0${NOT_SURROGATE})`,
  `U0{0,2}(0[1-9a-fA-F]${HEX}{4}|00${NOT_SURROGATE}|10${HEX}{4})`,
].join('|');
const INVALID_UNICODE_ESCAPE = new RegExp('\\\\(' + [
  `u${SURROGATE}`,
  `U0{0,4}${SURROGATE}`,
  `U0{0,2}([2-9a-fA-F]${HEX}{5}|1[1-9a-fA-F]${HEX}{4})`,
  `U[1-9a-fA-F]${HEX}{6,7}`,
  `U0[1-9a-fA-F]${HEX}{6}`,
].join('|') + ')');

// constraint: a character literal holds one byte, so a unicode escape in it stays below 0x80
const CHARACTER_ESCAPE = new RegExp(
  `\\\\(x${HEX}{1,2}|u(${HEX}|0{0,2}[0-7]${HEX})|U(${HEX}|0{0,6}[0-7]${HEX})|[tabnrvf0\\\\"'])`,
);
const ESCAPE = new RegExp(`\\\\(x${HEX}{1,2}|${UNICODE_ESCAPE}|[tabnrvf0\\\\"'])`);
const TEMPLATE_ESCAPE = new RegExp(`\\\\(x${HEX}{1,2}|${UNICODE_ESCAPE}|[tabnrvf0\\\\"'{}])`);
// constraint: the compiler allows `=` in the right operand of `??`, `||` and `&&` where it allows `=` in the left one
const RIGHT_OPERAND_KEEPS_PLACE = true;
const BINARY_OPERATORS = [
  [prec.right, PREC.NULL_COALESCE, '??', RIGHT_OPERAND_KEEPS_PLACE],
  [prec.right, PREC.OR, '||', RIGHT_OPERAND_KEEPS_PLACE],
  [prec.right, PREC.AND, '&&', RIGHT_OPERAND_KEEPS_PLACE],
  [prec.right, PREC.BIT_OR, '|'],
  [prec.left, PREC.BIT_XOR, '^'],
  [prec.left, PREC.BIT_AND, '&'],
  [prec.left, PREC.EQUALITY, choice('==', '!=', '<=>')],
  [prec.left, PREC.SHIFT, choice('<<', '>>', '>>>')],
  [prec.left, PREC.ADDITIVE, choice('+', '-')],
  [prec.left, PREC.MULTIPLICATIVE, choice('*', '/', '%')],
];

const UNARY_OPERATORS = ['-', '!', '~', 'typeof', 'resume', 'await', 'clone', 'static', 'delete'];

// constraint: the compiler allows `=` only in an expression that no operator, call, or bracket contains
const RVALUE = '';
const OUTER = '_outer';
// constraint: the compiler starts other statements with `{`, `function`, `class`, `const` and `async`
const STATEMENT = '_statement';

// constraint: a rule copy with fields gets a visible name; the generator copies an aliased hidden rule's fields up
/**
 * @param {string} prefix
 */
function spineNames(prefix) {
  /**
   * @param {string} name
   */
  const isCopy = name => prefix !== RVALUE && !(prefix === OUTER && name === 'assignment_expression');

  /**
   * @param {string} name
   */
  const ruleName = name => (isCopy(name) ? `${prefix.slice(1)}_${name}` : name);

  /**
   * @param {GrammarSymbols<string>} $
   * @param {string} name
   */
  const hidden = ($, name) => $[`${prefix}${name}`];

  /**
   * @param {GrammarSymbols<string>} $
   * @param {string} name
   */
  const node = ($, name) => (isCopy(name) ? alias($[ruleName(name)], $[name]) : $[name]);

  return {hidden, node, ruleName};
}

/**
 * @param {string} prefix
 * @returns {Record<string, ($: GrammarSymbols<string>) => RuleOrLiteral>}
 */
function expressionSpine(prefix) {
  const {hidden, node, ruleName} = spineNames(prefix);
  // constraint: the compiler allows `=` in the operand of a unary operator where it allows `=` before the operator
  const operand = spineNames(prefix === RVALUE ? RVALUE : OUTER);
  const unaryOperator = choice(...UNARY_OPERATORS, ...(prefix === STATEMENT ? [] : ['const']));

  return {
    ...(prefix === RVALUE ? {} : {
      [`${prefix}_regular_expression`]: $ => choice(node($, 'assignment_expression'), hidden($, '_expression')),

      [ruleName('assignment_expression')]: $ => prec.right(PREC.ASSIGN, seq(
        field('left', hidden($, '_operand')),
        '=',
        field('right', $._expression),
      )),
    }),

    ...(prefix === STATEMENT ? {} : {
      [ruleName('increment_expression')]: $ => prec(PREC.UNARY, seq(
        field('operator', choice('++', '--')),
        field('argument', hidden($, '_open_operand')),
      )),

      [ruleName('update_increment_expression')]: $ => prec(PREC.UNARY, seq(
        field('operator', choice('++', '--')),
        field('argument', alias($[ruleName('postfix_increment_expression')], $.increment_expression)),
      )),

      // constraint: `clone` is also a name, so the scanner returns the `++` or `--` after it as a postfix token
      [ruleName('clone_update_expression')]: $ => seq(
        field('operator', 'clone'),
        field('argument', choice(
          alias($[ruleName('clone_prefix_increment_expression')], $.increment_expression),
          $[`${prefix}_clone_update_unary_operand`],
        )),
      ),

      [ruleName('clone_prefix_increment_expression')]: $ => seq(
        field('operator', choice(alias($._postfix_increment, '++'), alias($._postfix_decrement, '--'))),
        field('argument', hidden($, '_open_operand')),
      ),

      [`${prefix}_clone_update_primary_expression`]: $ => alias(
        $[ruleName('clone_prefix_update_increment_expression')],
        $.increment_expression,
      ),

      [ruleName('clone_prefix_update_increment_expression')]: $ => seq(
        field('operator', choice(alias($._postfix_increment, '++'), alias($._postfix_decrement, '--'))),
        field('argument', alias($[ruleName('postfix_increment_expression')], $.increment_expression)),
      ),

      [`${prefix}_clone_update_unary_operand`]: $ => choice(
        $[`${prefix}_clone_update_postfix_operand`],
        alias(
          $[spineNames(`${prefix}_clone_update`).ruleName('postfix_increment_expression')],
          $.increment_expression,
        ),
      ),

      ...postfixSpine(`${prefix}_clone_update`),
    }),

    [`${prefix}_expression`]: $ => choice(
      node($, 'ternary_expression'),
      node($, 'compound_assignment_expression'),
      node($, 'newslot_expression'),
      hidden($, '_operand'),
    ),

    [`${prefix}_operand`]: $ => choice(
      node($, 'binary_expression'),
      alias($[ruleName('not_in_expression')], $.binary_expression),
      hidden($, '_relational_operand'),
    ),

    [`${prefix}_relational_operand`]: $ => choice(
      alias($[ruleName('relational_expression')], $.binary_expression),
      hidden($, '_shift_operand'),
    ),

    [`${prefix}_shift_operand`]: $ => choice(
      alias($[ruleName('arithmetic_expression')], $.binary_expression),
      hidden($, '_unary_operand'),
    ),

    // constraint: the compiler reads the right operand of a relational operator at the level of the shift operators
    [ruleName('relational_expression')]: $ => prec.left(PREC.RELATIONAL, seq(
      field('left', hidden($, '_relational_operand')),
      field('operator', choice('<', '>', '<=', '>=', 'in', 'instanceof')),
      field('right', $._shift_operand),
    )),

    // constraint: the compiler ends a chain of relational operators after `not in`
    [ruleName('not_in_expression')]: $ => prec.left(PREC.RELATIONAL, seq(
      field('left', hidden($, '_relational_operand')),
      field('operator', seq('not', 'in')),
      field('right', $._shift_operand),
    )),

    [ruleName('arithmetic_expression')]: $ => choice(
      ...BINARY_OPERATORS.filter(([, level]) => level >= PREC.SHIFT).map(([fn, level, operator]) => fn(level, seq(
        field('left', hidden($, '_shift_operand')),
        field('operator', operator),
        field('right', $._shift_operand),
      ))),
    ),

    [`${prefix}_unary_operand`]: $ => choice(
      hidden($, '_open_operand'),
      alias($[ruleName('postfix_increment_expression')], $.increment_expression),
    ),

    // constraint: after a postfix update the compiler leaves the operand, and a postfix operator applies to the
    // unary expression around it: `~x--.y` is `(~(x--)).y`
    [`${prefix}_open_operand`]: $ => choice(
      node($, 'unary_expression'),
      alias($[operand.ruleName('clone_update_expression')], $.unary_expression),
      operand.node($, 'increment_expression'),
      hidden($, '_postfix_operand'),
    ),

    [ruleName('update_unary_expression')]: $ => prec(PREC.UNARY, seq(
      field('operator', unaryOperator),
      field('argument', alias($[operand.ruleName('postfix_increment_expression')], $.increment_expression)),
    )),

    [`${prefix}_primary_expression`]: $ => {
      const parenthesized = prefix === RVALUE ?
        $.parenthesized_expression :
        alias($.outer_parenthesized_expression, $.parenthesized_expression);
      const functionsAndTable = prefix === STATEMENT ?
        [alias($.statement_lambda_expression, $.lambda_expression)] :
        [$.table, $.function_expression, $.lambda_expression, $.class_expression];
      return choice(
        $._name,
        $.this,
        $.base,
        $.root_table_access,
        $.null,
        $.true,
        $.false,
        $.integer,
        $.float,
        $.character,
        $.string,
        $.verbatim_string,
        $.interpolated_string,
        $.line_macro,
        $.file_macro,
        alias($.folded_literal_expression, $.unary_expression),
        parenthesized,
        $.array,
        ...functionsAndTable,
        $.code_block_expression,
      );
    },

    [ruleName('compound_assignment_expression')]: $ => prec.right(PREC.ASSIGN, seq(
      field('left', hidden($, '_operand')),
      field('operator', choice('+=', '-=', '*=', '/=', '%=')),
      field('right', $._expression),
    )),

    [ruleName('newslot_expression')]: $ => prec.right(PREC.ASSIGN, seq(
      field('left', hidden($, '_operand')),
      '<-',
      field('right', $._expression),
    )),

    [ruleName('ternary_expression')]: $ => prec.right(PREC.ASSIGN, seq(
      field('condition', hidden($, '_operand')),
      '?',
      field('consequence', $._expression),
      ':',
      field('alternative', $._expression),
    )),

    [ruleName('binary_expression')]: $ => choice(...BINARY_OPERATORS.filter(([, level]) => level < PREC.RELATIONAL).map(
      ([fn, level, operator, rightOperandKeepsPlace]) => fn(level, seq(
        field('left', hidden($, '_operand')),
        field('operator', operator),
        field('right', rightOperandKeepsPlace ? operand.hidden($, '_operand') : $._operand),
      )),
    )),

    [ruleName('unary_expression')]: $ => prec(PREC.UNARY, choice(
      seq(field('operator', unaryOperator), field('argument', operand.hidden($, '_open_operand'))),
      seq(field('operator', 'clone'), field('argument', $._clone_postfix_operand)),
    )),

    ...postfixSpine(prefix, $ => [
      alias($[ruleName('update_unary_expression')], $.unary_expression),
      alias($[operand.ruleName('update_increment_expression')], $.increment_expression),
      alias($.clone_array_update_expression, $.unary_expression),
    ]),
  };
}

// constraint: the compiler reads `clone [` as the clone operator on an array, not as an index
/**
 * @param {string} prefix
 * @param {($: GrammarSymbols<string>) => RuleOrLiteral[]} closedOperands
 * @returns {Record<string, ($: GrammarSymbols<string>) => RuleOrLiteral>}
 */
function postfixSpine(prefix, closedOperands = () => []) {
  const {hidden, node, ruleName} = spineNames(prefix);

  return {
    [ruleName('postfix_increment_expression')]: $ => prec.left(PREC.POSTFIX, seq(
      field('argument', hidden($, '_postfix_operand')),
      field('operator', choice(alias($._postfix_increment, '++'), alias($._postfix_decrement, '--'))),
      optional(choice($._after_postfix_update, seq('--', $._never_returned))),
    )),

    [ruleName('field_access_expression')]: $ => prec(PREC.POSTFIX, seq(
      field('receiver', hidden($, '_postfix_operand')),
      field('operator', choice('.', '?.', '.$', '?.$')),
      $._name_adjacent,
      field('field', choice(
        alias($.identifier, $.field_identifier),
        $._contextual_field_name,
        alias('constructor', $.field_identifier),
      )),
    )),

    [ruleName('slot_access_expression')]: $ => prec(PREC.POSTFIX, seq(
      field('receiver', hidden($, '_postfix_operand')),
      field('operator', choice(alias($._index_bracket, '['), alias($._null_index_bracket, '?['))),
      field('key', $._expression),
      ']',
    )),

    [ruleName('call_expression')]: $ => prec(PREC.POSTFIX, seq(
      field('callee', hidden($, '_postfix_operand')),
      field('arguments', $.arguments),
    )),

    [`${prefix}_postfix_operand`]: $ => choice(
      node($, 'field_access_expression'),
      node($, 'slot_access_expression'),
      node($, 'call_expression'),
      hidden($, '_primary_expression'),
      ...closedOperands($),
    ),
  };
}

/**
 * @param {GrammarSymbols<string>} $
 */
function functionMethod($) {
  return seq('function', optional($.function_attributes), field('name', $._name), $._function_tail);
}

/**
 * @param {GrammarSymbols<string>} $
 */
function methodForms($) {
  return choice(
    seq(optional('async'), functionMethod($)),
    seq(field('name', alias('constructor', $.identifier)), optional($.function_attributes), $._function_tail),
  );
}

export default grammar({
  name: 'quirrel',

  externals: $ => [
    $._automatic_semicolon,
    $._close_brace,
    $._semicolon,
    $._index_bracket,
    $._null_index_bracket,
    $._postfix_increment,
    $._postfix_decrement,
    $._else_marker,
    $._catch_marker,
    $._template_chars,
    $._unexpected_newline,
    $._text_after_nul,
    $._terminator_reset,
    $.integer,
    $.float,
    $._malformed_number,
    $._name_adjacent,
    $._import_marker,
    $._same_line_type_bar,
    $._const_declaration_end,
    $._after_postfix_update,
    $._never_returned,
    $._error_sentinel,
  ],

  extras: $ => [/[ \t\r\n]/, $.comment, $.position_directive, $._text_after_nul, $._terminator_reset],

  word: $ => $.identifier,

  reserved: {
    global: _ => [
      'while', 'do', 'if', 'else', 'break', 'continue', 'return', 'null', 'function', 'local', 'for', 'foreach',
      'in', 'typeof', 'base', 'delete', 'try', 'catch', 'throw', 'yield', 'resume', 'this', 'class', 'instanceof',
      'true', 'false', 'static', 'enum', 'const', '__LINE__', '__FILE__', 'global', 'not', 'let', 'async', 'await',
    ],
  },

  supertypes: $ => [$._statement, $._expression],

  inline: $ => [$._name, $._terminator, $._body_statement],

  conflicts: $ => [
    [$._contextual_name, $.switch_statement],
    [$._contextual_name, $.clone_update_expression],
    [$._contextual_name, $.outer_clone_update_expression],
  ],

  rules: {
    source_file: $ => seq(
      repeat($._prelude_item),
      optional(seq($._opening_statement, $._terminator, repeat($._top_level_item))),
    ),

    _prelude_item: $ => choice(
      seq(choice(seq($._import_marker, $.import_statement), $._prelude_statement), $._terminator),
      alias($._semicolon, $.empty_statement),
    ),

    _top_level_item: $ => choice(
      seq(choice($._prelude_statement, $._opening_statement), $._terminator),
      alias($._semicolon, $.empty_statement),
    ),

    _prelude_statement: $ => choice(
      $.expression_statement,
      $.return_statement,
      $.yield_statement,
      $.break_statement,
      $.continue_statement,
      $.throw_statement,
      $.directive,
    ),

    _opening_statement: $ => choice($._opening_statement_without_block, $.block),

    _opening_statement_without_block: $ => choice(
      $.local_declaration,
      $.function_declaration,
      $.class_declaration,
      $.const_declaration,
      $.enum_declaration,
      $.if_statement,
      $.while_statement,
      $.do_while_statement,
      $.for_statement,
      $.foreach_statement,
      $.switch_statement,
      $.try_statement,
      $.docstring,
    ),

    _statement: $ => choice($._prelude_statement, $._opening_statement, $.import_statement),

    _statement_item: $ => choice(
      seq($._statement, $._terminator),
      alias($._semicolon, $.empty_statement),
    ),

    _terminator: $ => choice(alias($._semicolon, ';'), $._automatic_semicolon),

    _body_statement: $ => choice(
      $.block,
      seq(choice($._prelude_statement, $._opening_statement_without_block, $.import_statement), $._terminator),
      alias($._semicolon, $.empty_statement),
    ),

    _unterminated_body: $ => choice($._statement, alias($._semicolon, $.empty_statement)),

    import_statement: $ => choice(
      seq('import', field('module', $._module_name), optional(seq('as', field('alias', $.identifier)))),
      seq(
        'from',
        field('module', $._module_name),
        'import',
        sep1(choice($.import_slot, alias('*', $.wildcard_import)), ','),
      ),
    ),

    _module_name: $ => choice($.string, $.verbatim_string),

    import_slot: $ => seq(field('name', $.identifier), optional(seq('as', field('alias', $.identifier)))),

    expression_statement: $ => $._statement_regular_expression,

    block: $ => seq('{', repeat($._statement_item), alias($._close_brace, '}')),

    directive: _ => token(seq('#', optional('default:'), choice(...DIRECTIVES))),

    docstring: _ => token(seq('@@"', repeat(choice(/[^"]/, '""')), '"')),

    local_declaration: $ => seq(
      choice('local', 'let'),
      choice(
        seq('function', optional($.function_attributes), field('name', $._name), $._function_tail),
        seq('class', field('name', $._name), $._class_tail),
        sep1($.variable_declaration, ','),
        seq(field('pattern', $._pattern), '=', field('value', $._expression)),
      ),
    ),

    variable_declaration: $ => seq(
      field('name', $._name),
      optional(field('type', alias($._declarator_type_annotation, $.type_annotation))),
      optional(seq('=', field('value', $._outer_regular_expression))),
    ),

    _pattern: $ => choice($.table_pattern, $.array_pattern),

    table_pattern: $ => seq('{', repeat(seq($.pattern_field, optional(','))), alias($._close_brace, '}')),

    array_pattern: $ => seq('[', repeat(seq($.pattern_field, optional(','))), ']'),

    pattern_field: $ => seq(
      field('name', $._name),
      optional(field('type', $.type_annotation)),
      optional(seq('=', field('default', $._outer_regular_expression))),
    ),

    function_declaration: $ => seq(
      optional('async'),
      'function',
      optional($.function_attributes),
      field('name', $._name),
      $._function_tail,
    ),

    class_declaration: $ => seq('class', field('name', $._name), $._class_tail),

    const_declaration: $ => seq(
      optional('global'),
      'const',
      choice(
        seq(field('name', $._name), '=', field('value', $._expression), $._const_declaration_end),
        seq('function', optional($.function_attributes), field('name', $._name), $._function_tail),
      ),
    ),

    enum_declaration: $ => seq(
      optional('global'),
      'enum',
      field('name', $._name),
      '{',
      repeat(seq($.enum_member, optional(','))),
      alias($._close_brace, '}'),
    ),

    enum_member: $ => seq(field('name', $._name), optional(seq('=', field('value', $._enum_value)))),

    _enum_value: $ => choice(
      $.null,
      $.true,
      $.false,
      $.integer,
      $.float,
      $.character,
      $.string,
      $.verbatim_string,
      $.negative_literal,
    ),

    negative_literal: $ => seq('-', choice($.integer, $.float, $.character)),

    if_statement: $ => prec.right(seq(
      'if',
      '(',
      choice(
        field('condition', $._expression),
        seq(
          field('declaration', alias($.if_local_declaration, $.local_declaration)),
          optional(seq(alias($._semicolon, ';'), field('condition', $._expression))),
        ),
      ),
      ')',
      field('then_branch', $._body_statement),
      optional(seq(optional($._else_marker), 'else', field('else_branch', $._body_statement))),
    )),

    if_local_declaration: $ => seq(choice('local', 'let'), alias($.if_variable_declaration, $.variable_declaration)),

    if_variable_declaration: $ => seq(
      field('name', $._name),
      optional(field('type', alias($._declarator_type_annotation, $.type_annotation))),
      '=',
      field('value', $._outer_regular_expression),
    ),

    while_statement: $ => seq('while', '(', field('condition', $._expression), ')', field('body', $._body_statement)),

    do_while_statement: $ => seq(
      'do',
      field('body', $._body_statement),
      'while',
      '(',
      field('condition', $._expression),
      ')',
    ),

    for_statement: $ => seq(
      'for',
      '(',
      optional(field('initializer', choice(alias($.for_local_declaration, $.local_declaration), $._comma_expression))),
      alias($._semicolon, ';'),
      optional(field('condition', $._expression)),
      alias($._semicolon, ';'),
      optional(field('step', $._comma_expression)),
      ')',
      field('body', $._body_statement),
    ),

    for_local_declaration: $ => seq(
      'local',
      choice(
        sep1($.variable_declaration, ','),
        seq(field('pattern', $._pattern), '=', field('value', $._expression)),
      ),
    ),

    _comma_expression: $ => choice($._outer_regular_expression, $.comma_expression),

    comma_expression: $ => seq($._outer_regular_expression, repeat1(seq(',', $._outer_regular_expression))),

    foreach_statement: $ => seq(
      'foreach',
      '(',
      optional(seq(field('index', $._name), ',')),
      field('value', choice($._name, $._pattern)),
      'in',
      field('container', $._expression),
      ')',
      field('body', $._body_statement),
    ),

    switch_statement: $ => seq(
      'switch',
      '(',
      field('expression', $._expression),
      ')',
      '{',
      repeat($.switch_case),
      optional($.default_case),
      alias($._close_brace, '}'),
    ),

    switch_case: $ => prec.left(seq('case', field('value', $._expression), ':', repeat($._statement_item))),

    default_case: $ => prec.left(seq('default', ':', repeat($._statement_item))),

    try_statement: $ => seq('try', field('body', $._unterminated_body), $._catch_clauses),

    _catch_clauses: $ => prec.right(choice(
      seq(alias($.typed_catch_clause, $.catch_clause), optional($._catch_clauses)),
      alias($.catch_all_clause, $.catch_clause),
    )),

    typed_catch_clause: $ => seq(
      optional($._catch_marker),
      'catch',
      '(',
      field('class', $._name),
      field('name', $._name),
      ')',
      field('body', $._unterminated_body),
    ),

    catch_all_clause: $ => seq(
      optional($._catch_marker),
      'catch',
      '(',
      field('name', $._name),
      ')',
      field('body', $._unterminated_body),
    ),

    throw_statement: $ => seq('throw', $._expression),

    return_statement: $ => seq('return', optional($._expression)),

    yield_statement: $ => seq('yield', optional($._expression)),

    break_statement: _ => 'break',

    continue_statement: _ => 'continue',

    _name: $ => choice($.identifier, $._contextual_name, alias('constructor', $.identifier)),

    _slot_name: $ => choice($.identifier, $._contextual_name),

    _contextual_name: $ => choice(...CONTEXTUAL_NAMES.map(name => alias(name, $.identifier))),

    _contextual_field_name: $ => choice(...CONTEXTUAL_NAMES.map(name => alias(name, $.field_identifier))),

    function_attributes: _ => {
      const attribute = (/** @type {string} */ name) => seq(name, optional(','));
      return seq('[', optional(choice(
        seq(attribute('pure'), optional(attribute('nodiscard'))),
        seq(attribute('nodiscard'), optional(attribute('pure'))),
      )), ']');
    },

    _function_tail: $ => seq(
      field('parameters', $.parameters),
      optional(field('return_type', $.type_annotation)),
      field('body', $.block),
    ),

    parameters: $ => seq(
      '(',
      optional(choice($._required_parameters, $._default_parameters, $.vararg_parameter)),
      ')',
    ),

    _required_parameters: $ => seq(
      choice(alias($.required_parameter, $.parameter), alias($.pattern_parameter, $.parameter)),
      optional(seq(',', optional(choice($._required_parameters, $._default_parameters, $.vararg_parameter)))),
    ),

    _default_parameters: $ => seq(
      alias($.default_parameter, $.parameter),
      optional(seq(',', optional($._parameters_after_default))),
    ),

    _parameters_after_default: $ => seq(
      choice(alias($.default_parameter, $.parameter), alias($.pattern_parameter, $.parameter)),
      optional(seq(',', optional($._parameters_after_default))),
    ),

    required_parameter: $ => seq(field('name', $._name), optional(field('type', $.type_annotation))),

    default_parameter: $ => seq(
      field('name', $._name),
      optional(field('type', $.type_annotation)),
      '=',
      field('default', $._expression),
    ),

    pattern_parameter: $ => field('pattern', $._pattern),

    vararg_parameter: $ => seq('...', optional(field('type', $.type_annotation))),

    type_annotation: $ => seq(':', choice($._type_union, seq('(', $._type_union, ')'))),

    _declarator_type_annotation: $ => seq(':', choice(
      sep1($._type, alias($._same_line_type_bar, '|')),
      seq('(', $._type_union, ')'),
    )),

    _type_union: $ => sep1($._type, '|'),

    _type: $ => choice(...TYPE_NAMES.map(name => alias(name, $.type))),

    _class_tail: $ => seq(optional(seq('(', field('base', $._expression), ')')), field('body', $.class_body)),

    class_body: $ => seq(
      '{',
      repeat(seq($._class_member, optional(alias($._semicolon, ';')))),
      alias($._close_brace, '}'),
    ),

    _class_member: $ => choice(
      alias($.class_slot, $.slot),
      alias($.class_computed_slot, $.computed_slot),
      alias($.class_method, $.method),
      $.docstring,
    ),

    class_slot: $ => seq(optional('static'), field('key', $._slot_name), '=', field('value', $._expression)),

    class_computed_slot: $ => seq(
      optional('static'),
      '[',
      field('key', $._expression),
      ']',
      '=',
      field('value', $._expression),
    ),

    class_method: $ => seq(optional('static'), methodForms($)),

    method: $ => methodForms($),

    function_method: $ => functionMethod($),

    ...expressionSpine(RVALUE),

    ...expressionSpine(OUTER),

    ...expressionSpine(STATEMENT),

    ...postfixSpine('_clone'),

    _clone_primary_expression: $ => alias($._clone_array, $.array),

    // constraint: the compiler folds `-` or `~` and the number or character literal after it into one literal
    folded_literal_expression: $ => prec(PREC.FOLDED_LITERAL, choice(
      seq(field('operator', '-'), field('argument', choice($.integer, $.float, $.character))),
      seq(field('operator', '~'), field('argument', choice($.integer, $.character))),
    )),

    clone_array_update_expression: $ => prec(PREC.UNARY, seq(
      field('operator', 'clone'),
      field('argument', alias($.clone_postfix_increment_expression, $.increment_expression)),
    )),

    _clone_array: $ => seq(
      alias($._index_bracket, '['),
      repeat(seq(choice($._expression, $.spread), optional(','))),
      ']',
    ),

    arguments: $ => seq(choice('(', '?('), repeat(seq($._expression, optional(','))), ')'),

    this: _ => 'this',
    base: _ => 'base',
    null: _ => 'null',
    true: _ => 'true',
    false: _ => 'false',
    line_macro: _ => '__LINE__',
    file_macro: _ => '__FILE__',

    root_table_access: $ => seq('::', $._name_adjacent, field('name', $._name)),

    parenthesized_expression: $ => seq('(', $._expression, ')'),

    outer_parenthesized_expression: $ => seq('(', $._outer_regular_expression, ')'),

    array: $ => seq('[', repeat(seq(choice($._expression, $.spread), optional(','))), ']'),

    spread: $ => seq('...', $._expression),

    table: $ => seq('{', repeat($._table_group), optional($._shorthand_run), alias($._close_brace, '}')),

    _table_group: $ => choice(
      seq($._table_member, optional(',')),
      seq($._shorthand_run, choice(',', seq($._shorthand_follower, optional(',')))),
    ),

    _shorthand_run: $ => repeat1($.shorthand_slot),

    _table_member: $ => choice(
      $.slot,
      $.quoted_key_slot,
      $.computed_slot,
      $.spread,
      $.method,
    ),

    _shorthand_follower: $ => choice(
      $.slot,
      $.computed_slot,
      $.spread,
      alias($.function_method, $.method),
    ),

    slot: $ => seq(field('key', $._slot_name), '=', field('value', $._expression)),

    shorthand_slot: $ => field('key', $._slot_name),

    quoted_key_slot: $ => seq(field('key', choice($.string, $.verbatim_string)), ':', field('value', $._expression)),

    computed_slot: $ => seq('[', field('key', $._expression), ']', '=', field('value', $._expression)),

    function_expression: $ => seq(
      optional('async'),
      'function',
      optional($.function_attributes),
      optional(field('name', $._name)),
      $._function_tail,
    ),

    lambda_expression: $ => choice(seq('async', $._lambda), $._lambda),

    statement_lambda_expression: $ => $._lambda,

    _lambda: $ => prec.right(PREC.LAMBDA, seq(
      '@',
      optional($.function_attributes),
      optional(field('name', $._name)),
      field('parameters', $.parameters),
      optional(field('return_type', $.type_annotation)),
      field('body', $._outer_regular_expression),
      optional(seq('=', $._never_returned)),
    )),

    class_expression: $ => seq('class', $._class_tail),

    code_block_expression: $ => seq('$${', repeat($._statement_item), alias($._close_brace, '}')),

    string: $ => seq(
      '"',
      repeat(choice(
        alias(token.immediate(prec(1, /[^"\\\n]+/)), $.string_content),
        $.escape_sequence,
        $._invalid_unicode_escape,
      )),
      token.immediate('"'),
    ),

    escape_sequence: _ => token.immediate(ESCAPE),

    _invalid_unicode_escape: $ => seq(token.immediate(INVALID_UNICODE_ESCAPE), $._never_returned),

    verbatim_string: _ => token(seq('@"', repeat(choice(/[^"]/, '""')), '"')),

    character: _ => token(seq(
      '\'',
      choice(/[\x00-\x09\x0b-\x26\x28-\x5b\x5d-\x7f]/, CHARACTER_ESCAPE),
      '\'',
    )),

    interpolated_string: $ => seq(
      '$"',
      repeat(choice(
        alias($._template_chars, $.string_content),
        alias(token.immediate(TEMPLATE_ESCAPE), $.escape_sequence),
        $._invalid_unicode_escape,
        $.hole,
      )),
      '"',
    ),

    hole: $ => seq('{', $._expression, alias($._close_brace, '}')),

    identifier: _ => /[A-Za-z_][A-Za-z0-9_]*/,

    comment: _ => token(choice(
      seq('//', /[^\n]*/),
      seq('/*', /[^*]*\*+([^/*][^*]*\*+)*/, '/'),
    )),

    position_directive: _ => token(seq('#pos:', /[0-9]+/, ':', /[0-9][A-Za-z0-9_:-]*/)),
  },
});

/**
 * @param {RuleOrLiteral} rule
 * @param {RuleOrLiteral} separator
 * @returns {SeqRule}
 */
function sep1(rule, separator) {
  return seq(rule, repeat(seq(separator, rule)));
}
