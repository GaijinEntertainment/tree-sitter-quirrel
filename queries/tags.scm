(
  (comment)* @doc
  .
  (function_declaration
    name: (identifier) @name) @definition.function
  (#strip! @doc "^//\\s?|^/\\*\\s*|\\s*\\*/$")
  (#select-adjacent! @doc @definition.function)
)

(
  (comment)* @doc
  .
  [
    (local_declaration
      "function"
      name: (identifier) @name)
    (const_declaration
      "function"
      name: (identifier) @name)
  ] @definition.function
  (#strip! @doc "^//\\s?|^/\\*\\s*|\\s*\\*/$")
  (#select-adjacent! @doc @definition.function)
)

(
  (comment)* @doc
  .
  [
    (class_declaration
      name: (identifier) @name)
    (local_declaration
      "class"
      name: (identifier) @name)
  ] @definition.class
  (#strip! @doc "^//\\s?|^/\\*\\s*|\\s*\\*/$")
  (#select-adjacent! @doc @definition.class)
)

(variable_declaration
  name: (identifier) @name
  value: [
    (function_expression)
    (lambda_expression)
  ]) @definition.function

(slot
  key: (identifier) @name
  value: [
    (function_expression)
    (lambda_expression)
  ]) @definition.function

(method
  name: (identifier) @name) @definition.method

(enum_declaration
  name: (identifier) @name) @definition.class

(const_declaration
  name: (identifier) @name
  value: (_)) @definition.constant

(class_declaration
  base: (identifier) @name) @reference.class

(class_expression
  base: (identifier) @name) @reference.class

(call_expression
  callee: [
    (identifier) @name
    (field_access_expression
      field: (field_identifier) @name)
    (root_table_access
      name: (identifier) @name)
  ]) @reference.call
