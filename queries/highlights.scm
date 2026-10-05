(identifier) @variable

((identifier) @constant
  (#match? @constant "^[A-Z][A-Z0-9_]*$"))

(field_identifier) @variable.member

[
  (this)
  (base)
] @variable.builtin

(parameter
  name: (identifier) @variable.parameter)

(catch_clause
  name: (identifier) @variable.parameter)

(slot
  key: (identifier) @variable.member)

(shorthand_slot
  key: (identifier) @variable.member)

(function_declaration
  name: (identifier) @function)

(local_declaration
  "function"
  name: (identifier) @function)

(const_declaration
  name: (identifier) @constant)

(const_declaration
  "function"
  name: (identifier) @function)

(function_expression
  name: (identifier) @function)

(lambda_expression
  name: (identifier) @function)

(method
  name: (identifier) @function.method)

(call_expression
  callee: (identifier) @function.call)

(call_expression
  callee: (root_table_access
    name: (identifier) @function.call))

(call_expression
  callee: (field_access_expression
    field: (field_identifier) @function.method.call))

((identifier) @constructor
  (#eq? @constructor "constructor"))

((field_identifier) @constructor
  (#eq? @constructor "constructor"))

(class_declaration
  name: (identifier) @type)

(class_declaration
  base: (identifier) @type)

(class_expression
  base: (identifier) @type)

(local_declaration
  "class"
  name: (identifier) @type)

(enum_declaration
  name: (identifier) @type)

(catch_clause
  class: (identifier) @type)

(enum_member
  name: (identifier) @constant)

(type) @type.builtin

(import_statement
  alias: (identifier) @module)

(wildcard_import) @character.special

(integer) @number

(float) @number.float

[
  (true)
  (false)
] @boolean

[
  (null)
  (line_macro)
  (file_macro)
] @constant.builtin

[
  (string)
  (verbatim_string)
  (interpolated_string)
] @string

(escape_sequence) @string.escape

(character) @character

(docstring) @string.documentation

(comment) @comment

[
  (directive)
  (position_directive)
] @keyword.directive

[
  "local"
  "let"
] @keyword

[
  "const"
  "global"
  "static"
] @keyword.modifier

[
  "function"
  "@"
] @keyword.function

[
  "class"
  "enum"
] @keyword.type

[
  "return"
  "yield"
] @keyword.return

[
  "if"
  "else"
  "switch"
  "case"
  "default"
] @keyword.conditional

[
  "while"
  "do"
  "for"
  "foreach"
  (break_statement)
  (continue_statement)
] @keyword.repeat

[
  "try"
  "catch"
  "throw"
] @keyword.exception

[
  "import"
  "from"
  "as"
] @keyword.import

[
  "async"
  "await"
  "resume"
] @keyword.coroutine

[
  "typeof"
  "instanceof"
  "in"
  "not"
  "delete"
  "clone"
] @keyword.operator

[
  "pure"
  "nodiscard"
] @attribute

[
  "="
  "<-"
  "+="
  "-="
  "*="
  "/="
  "%="
  "+"
  "-"
  "*"
  "/"
  "%"
  "++"
  "--"
  "=="
  "!="
  "<=>"
  "<"
  ">"
  "<="
  ">="
  "<<"
  ">>"
  ">>>"
  "&"
  "|"
  "^"
  "~"
  "!"
  "&&"
  "||"
  "??"
  "..."
] @operator

(vararg_parameter
  "..." @variable.parameter)

[
  "("
  ")"
  "["
  "]"
  "{"
  "}"
  "?("
  "?["
] @punctuation.bracket

[
  ","
  ";"
  ":"
  "."
  "?."
  ".$"
  "?.$"
  "::"
] @punctuation.delimiter

(empty_statement) @punctuation.delimiter

(ternary_expression
  [
    "?"
    ":"
  ] @keyword.conditional.ternary)

(hole
  [
    "{"
    "}"
  ] @punctuation.special)

"$${" @punctuation.special
