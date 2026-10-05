# tree-sitter-quirrel

Tree-sitter grammar for Quirrel, the scripting language of the Dagor Engine.

## Reference

- The authority is the compiler in `prog/1stPartyLibs/quirrel/quirrel/squirrel/compiler/` of the Dagor tree:
  `lexer.cpp`, `parser.cpp`, `parser.h`, and `sqtypeparser.cpp`. Settle each grammar question in that code, not in the
  Quirrel docs.
- Two releases of the compiler are in use: Quirrel 4.41 of GaijinEntertainment/quirrel and GaijinEntertainment/DagorEngine
  on GitHub, and Quirrel 4.43 of the Dagor master branch. The grammar accepts the input of both. Only 4.41 has the
  `#forbid-auto-freeze` and `#allow-auto-freeze` directives, and only 4.43 accepts a spread right after a shorthand slot
  with no comma between them (`{ a ...b }`).
- The grammar is a clean-room implementation. No file, rule, or test comes from another Quirrel or Squirrel tree-sitter
  grammar. Do not read one when you change this grammar.
- An input that the compiler accepts in some language mode parses without ERROR or MISSING nodes. An input that every
  mode rejects produces ERROR wherever the grammar can see the defect.
- `has_error` of the root node is the verdict of a parse. A recovery that inserts a missing `_automatic_semicolon`
  leaves no ERROR or MISSING node for a tree walk, and `tree-sitter parse` then reports a successful parse.
- The grammar accepts the input of every language mode, because a directive in the file and the host application set
  the mode. `switch`, `case`, `default`, and `clone` therefore parse both as keywords and as names, and `delete`, `::`,
  and `$${ ... }` parse everywhere. A file that uses the forms of two modes also parses, although no single mode
  accepts it, as in `x = clone y` followed by `clone--`.
- The compiler rejects this input, and the grammar accepts it. Keep the list current:
  - An import after a `try` or `catch` body that is a declaration or a control statement without braces, as in
    `try local x = 1 catch (e) { import "m" }`. A marker for the end of such a body costs about 2,200 parser states.
  - A second docstring that is a statement of a `$${ ... }` code block. The compiler gives that docstring to the
    function, lambda, class, or table around the block, and the scanner has no scope for a lambda or a table.
- The grammar does not check what the compiler checks after it reads the syntax: the nesting depth of an expression
  and the checks of the code generator.

## Where things live

- `grammar.js` - statements, expressions, and names. `expressionSpine` and `postfixSpine` build copies of the
  expression rules: the `statement_` copy starts an expression statement, where `{`, `function`, `class`, `const`, and
  `async` start other statements, and the `clone_` copy reads `clone [` as the clone operator on an array.
- The compiler allows `=` only where an expression statement, an initializer, a `for` clause, a pattern default, or a
  lambda body starts, and keeps that permission through parentheses, the operand of a unary operator, the left operand
  of a binary operator, both operands of `??`, `||`, and `&&`, the condition of `? :`, and the receiver of a postfix
  operator. The `outer_` and `statement_` copies are the expressions that keep it; the rules without a prefix are the
  expressions that lost it, and their `parenthesized_expression` holds no `=`.
- The compiler ends a chain of relational operators after `not in`, and reads the right operand of each relational
  operator at the level of the shift operators. Precedence cannot express that: it settles a conflict between two
  actions, and after `a not in b` no action on `<` conflicts with the shift. The binary rules therefore have operand
  levels: `_operand` for the operators below the relational ones, `_relational_operand`, and `_shift_operand`. All
  of them give `binary_expression` nodes.
- The compiler reads a unary operator and its operand as one operand of the postfix operators. The operand of the
  unary operator takes every postfix operator itself, except after `++` or `--`, where it ends. A postfix operator
  after that applies to the unary expression: `~x--.y` is `(~(x--)).y`. The `update_unary_expression` and
  `update_increment_expression` rules are that form, and `_open_operand` is an operand that does not end with a
  postfix update.
- The compiler folds `-` or `~` and the number or character literal after it into one literal, so `-1[0]` is
  `(-1)[0]`, and no postfix operator follows `-1++`. `folded_literal_expression` is that literal, as a
  `unary_expression` node.
- After the word `clone` a `[` on a later line starts the array that the clone operator takes, so the scanner returns
  `_index_bracket` there and not `_unexpected_newline`. `_after_clone`, which the scanner never returns, marks that
  position.
- After the word `clone` at a line end the statement ends, because `clone` can be a name. The scanner does not end it
  when the next line starts with `function`, `async`, `class`, or `const` in a form that is an operand and no
  statement, as in `clone` and then `const [1]`.
- The grammar rejects one input that the compiler accepts where `clone` is an operator: `clone` at a line end and, on
  the next line, a table that is not also a valid block, as in `{a = 1, b = 2}`. A parser that follows both readings
  of that line end has about 45% more states.
- After the word `clone` the scanner returns `++` and `--` as postfix tokens, because `clone` can be a name. The
  `clone_update_expression` rules take those tokens as the prefix update that the clone operator applies to, and the
  conflict with `_contextual_name` lets the parser try both readings of `clone --x`.
- An `=` cannot follow a complete lambda body, because the body takes every token that continues an expression. The
  lambda rule ends with an optional `=` that `_never_returned` rejects.
- `src/scanner.c` - statement ends (the automatic `;`), the `}` and `;` that end a statement, the postfix tokens that a
  line end changes (`[`, `?[`, `++`, `--`), numbers, template string text, the `.name` adjacency marker, the import
  marker, the same-line `|` of a declaration type, and the ignored text after a NUL byte.
- `src/parser.c`, `src/grammar.json`, `src/node-types.json`, `src/tree_sitter/` - generated by `tree-sitter generate`
  with tree-sitter CLI 0.27.0. Never edit them by hand. Regenerate with that CLI version, and make a CLI upgrade its own
  change, which also moves the `tree-sitter-ref` of the CI workflow and the `TREE_SITTER_CLI_VERSION` and
  `TREE_SITTER_CLI_SHA256` of the publish workflow: CI regenerates the parser and fails on a difference.
- `queries/highlights.scm` - editor highlighting. `test/highlight/` holds its capture assertions.
- `queries/tags.scm` - the definitions and references for code navigation (`tree-sitter tags`). `test/tags/` holds
  its tag assertions.
- `bindings/`, the package manifests (`binding.gyp`, `Cargo.toml`, `CMakeLists.txt`, `go.mod`, `Makefile`,
  `package.json`, `Package.swift`, `pyproject.toml`, `setup.py`), `.editorconfig`, `.gitattributes`, and `.gitignore` -
  generated by `tree-sitter init` from `tree-sitter.json`. Run `tree-sitter init --update` after a change to
  `tree-sitter.json`, and set the version with `tree-sitter version <version>`.
- `package-lock.json`, `Cargo.lock`, `go.sum`, `Package.resolved` - lockfiles that npm, cargo, go, and swift write when
  they resolve the dependencies of the manifests. Commit them with the manifest change.
- `.github/` - the CI and publish workflows, dependabot, and issue templates.
  The publish workflow authenticates to crates.io, PyPI, and npm with trusted publishing and holds no registry token.
- `eslint.config.mjs` - the lint configuration for `grammar.js` (`npm run lint`).
- `examples/` - Quirrel files that the CI workflow parses.
- `test/corpus/` - corpus tests, one file per topic. `:error` marks an input that must produce ERROR.
- `test/corpus/compiler/` - inputs of the compiler tests, copied unchanged from `testData/` of the compiler. Each test
  names its source file, and its tree is the output of `tree-sitter parse` without the positions. When the grammar
  moves to a new Quirrel version, copy the inputs again, write the trees again, and review the difference.

## Rules

- A node kind and a field take the name that the Quirrel language reference
  (`prog/1stPartyLibs/quirrel/quirrel/doc/content/pages/language/`) gives the construct: `interpolated_string` and its
  `hole`, `enum_member`, `vararg_parameter`, `root_table_access`, `spread`, the `container` of a `foreach`, the `step`
  of a `for`. A construct that the reference does not name takes the name of the compiler AST in
  `squirrel/compiler/ast.h`, with each abbreviation spelled out: `TerExpr` is `ternary_expression`, `GetFieldExpr` is
  `field_access_expression` with a `receiver` and a `field`, `GetSlotExpr` is `slot_access_expression` with a `key`,
  `CallExpr` has a `callee`, and `IfStatement` has a `then_branch` and an `else_branch`. The two sides of an assignment
  and a binary expression are `left` and `right`.
- The docs cite a Dagor file only when GaijinEntertainment/DagorEngine on GitHub has it.
- Model the repository setup (workflows, lint, lockfiles, README) on the official grammars of the tree-sitter
  organization, such as tree-sitter/tree-sitter-cpp. `README.md` holds only the badges, a short description of the
  grammar, and references. Editor setup, binding use, and maintainer procedures stay out of it.
- `tree-sitter init` writes only the first author of `tree-sitter.json` into a package manifest. Keep the full author
  list in `package.json`, `Cargo.toml`, and `pyproject.toml` by hand; `tree-sitter init --update` keeps it.
- CLI 0.27.0 `tree-sitter init --update` writes a second `let dir` line into `Package.swift`. Remove that line.
- Keep the `externals` array in `grammar.js` and `enum TokenType` in `src/scanner.c` in the same order.
- The generator copies the fields of an aliased hidden rule into the parent node, and `child_by_field_name` on the
  parent then returns the wrong child. A rule that has fields and appears only under an alias gets a visible name, as
  the spine copies, the parameter forms, the catch clause forms, and the declarations of `if` and `for` do.
- An alias of an inline `seq` applies to each element. Alias a named rule.
- A contextual keyword used as a name, and a type name, is an aliased token, so that its node is a leaf. A keyword
  capture in `queries/highlights.scm` then does not color a name.
- The compiler rejects `async` on a method whose name is a metamethod. `METAMETHODS` in `grammar.js` holds the names
  of `METAMETHODS_LIST` in `sqobject.h`; they are keywords only after `async function` in a table or a class, where
  `_never_returned` rejects them.
- `constructor` is a keyword token of the compiler and a reserved word here. The compiler takes it as a name where it
  calls `Expect(TK_IDENTIFIER)`; those places use `_name`. Where the compiler compares the token with `TK_IDENTIFIER`
  itself, `constructor` is not a name; those places use `_plain_name` or `identifier`: an import name, a slot key, the
  variable of a typed catch, and the name of a function expression or a lambda.
- The runtime restores the scanner state from the last external token. While `after_terminator` is set, each scan
  therefore returns a token, the zero-width `_terminator_reset` extra when no other token applies.
- A `}` or `;` that ends a statement sets `after_terminator`. One `}` or `;` ends each statement that it closes, as in
  `if (a) if (b) c; d`, so an automatic `;` that needs the flag keeps it for the statement around it.
- A `;` on the line of the `}` or `;` that ended a statement ends no statement. The compiler takes it as an empty
  statement, so `x = {};` is two statements, and `else` or the `while` of a `do` loop cannot follow that `;`.
- A `const` declaration with a value ends only at `;`, a line end, `}`, or the text end; a `}` before it does not end
  it, as in `const A = {} x = 1`. The zero-width `_const_declaration_end` holds that check, and the statement end
  follows it.
- The compiler takes no postfix operator after a postfix update and reads `--` as one token there. The update rule
  therefore ends with an optional `--` that `_never_returned` rejects; where the next operand can start, as in
  `[a++ --b]`, the left associativity of the rule ends the update first.
- The compiler reads an import statement without regard to line ends: the word `as` after an import name or a module
  name is the alias keyword, also on a later line. `_before_import_alias` is a token that the scanner never returns;
  where it is valid, a line end before `as` does not end the statement.
- `_foreach_index_marker` and `_enum_members_marker` are zero-width tokens whose scan reads ahead and compares names:
  the index and the value of a `foreach`, and the members of an enum. The scan returns no token when two names are
  equal, and the statement is then an error. The enum scan follows the lexer of the compiler for the member values
  and compares the first 4096 members.
- `_catch_marker` is a zero-width token before each `catch`. Its scan reads ahead and compares the type names of the
  catch clauses that follow outside brackets, up to the next `try` outside brackets, because a `catch` joins the
  nearest `try`. The scan returns no token when two types are equal or a clause follows the catch-all clause. It steps
  over strings, template strings, and comments, and it stops at 32 nested template strings.
- The compiler takes one docstring in a file, in a function body, and in a class body, and none in a table. The
  scanner keeps one entry for each open scope: `_docstring_scope_start` after the `{` of a function body or a class
  body opens an entry, and `_docstring_scope_end` before the `}` closes it. `_docstring_start` is a zero-width token
  before each docstring, and the scan returns no token for the second docstring of a scope. A code block opens an
  entry that takes each docstring. The scanner keeps 512 entries, and a deeper scope takes each docstring.
- The scanner never returns `_after_postfix_update` either. Its presence in `valid_symbols` marks the position after a
  postfix update, where `_index_bracket` is not valid but a binary minus on the next line continues the expression.
- The scanner never returns `_never_returned`, so a rule that ends with it is an error. The rule gives the lexer a
  longer token for input that a shorter valid token matches in part: `_invalid_unicode_escape` takes all of `\uD800`,
  of which `\uD80` is a valid escape.
- `scan_number` follows `SQLexer::ReadNumber`: a decimal number takes every following letter, digit, and `.`, and an
  invalid run returns `_malformed_number`, which no rule accepts.
- A number outside the limits of the engine build returns `_malformed_number` too: an integer above 2^63 - 1, also after
  a minus sign; more than 16 hex digits; a float that a 32-bit float rounds to its maximum or to zero.
- The compiler rounds a float with `std::from_chars` where the library declares it, and through `strtod` and a cast
  elsewhere. The two paths differ within one double-precision step of each float limit. The scanner accepts a literal
  that one of the paths accepts.
- The compiler takes `import` and `from` as keywords at the start of a statement until a statement has ended that is
  not an import, a directive, `;`, an expression statement, `return`, `yield`, `break`, `continue`, or `throw`. The
  end of a function body counts as such a statement. After that, the two words are names. The scanner keeps this in
  `imports_closed`, which never goes back to false: `_after_opening_item` is valid after the terminator of such a
  statement and `_after_block` after a function body, and the scanner never returns either. While imports are open,
  the scanner returns `_import_marker` before `import` or `from` at the start of a statement, and each import
  statement needs that token.
- A scan that changes the scanner state returns a token, the `_terminator_reset` extra when no other token applies,
  because the runtime keeps the state only with a token.
- `TYPE_NAMES` follows `sq_type_string_to_mask` in `sqtypeparser.cpp`, and `DIRECTIVES` holds the directive tables in
  `parser.cpp` of both compiler releases.
- Every corpus input without `:error` compiles in some language mode, and every `:error` input fails in every mode.
  Check a new case with the compiler before you add it.
- In `queries/highlights.scm`, a later pattern overrides an earlier one in both tree-sitter-highlight and Neovim. Put a
  specific pattern after the general pattern, and give each pattern one capture: tree-sitter-highlight drops every
  capture of an earlier match that shares a node with a later match.
- A `#match?` regex must mean the same in Rust regex syntax and in Vim very-magic syntax (Neovim). Write a literal `@`
  as `[@]` and a literal `~` as `[~]`.
- A workflow pins each action to the commit SHA of a release and names the release in a comment
  (`actions/checkout@<sha> # v7.0.1`), which Dependabot reads to update both. A checkout sets
  `persist-credentials: false`.
- A change reaches `main` through a pull request, merged by squash or rebase after the `ci-ok` job of
  `.github/workflows/ci.yml` passes; the `protect-main` ruleset rejects a direct push. Add each new CI job to the
  `needs` list of `ci-ok`.
- In `queries/tags.scm`, tree-sitter-tags keeps one tag per name node, from the earliest pattern that matches it. Put
  a specific pattern before the general pattern for the same node.

## Releasing

- The version is `X.Y.P`. `X.Y` is always the major and minor number of the newest Quirrel version that the grammar
  covers. `P` is the grammar's own number: it counts the grammar releases for that Quirrel version and does not follow
  the patch number of the compiler. Only a new Quirrel major version can mark a breaking change: under one major
  version, a release adds node kinds and fields, and never renames or removes one.
- To release, set the version with `tree-sitter version X.Y.P`, then run `tree-sitter generate`, because
  `src/parser.c` holds the version too, and update the lockfiles with `cargo update --workspace --offline` and
  `npm install --package-lock-only --ignore-scripts`. Commit, and push the tag `vX.Y.P`.
- The Go module path ends in `/vX`, as Go requires from major version 2. A new Quirrel major version changes the path
  in `go.mod` and in the import of `bindings/go/binding_test.go`.
- The tag starts `.github/workflows/publish.yml`. It checks that the tag matches `tree-sitter.json` and the Go module
  path, creates the GitHub release with attested artifacts, and publishes to crates.io, PyPI, and npm. A registry that
  already has the version is skipped. The Go module needs no publish step: the tag on the public repository is the
  release.
- A manual run of `publish.yml` rehearses a release: it runs the checks and builds every artifact, and publishes
  nothing. Run it after a change to the workflow.
- The `protect-release-tags` ruleset forbids moving or deleting a `v*` tag. When a publish job fails for a reason
  outside the repository, run the failed jobs again; when the fix is a commit, release the next version.
- crates.io, PyPI, and npm each hold a trusted publisher for the repository `GaijinEntertainment/tree-sitter-quirrel`,
  the workflow `publish.yml`, and the environment `crates`, `pypi`, or `npm`. The npm publisher allows `npm publish`.
  The three environments accept deployments only from `v*` tags. A new name for the repository, the workflow file, or
  an environment needs a new trusted publisher on each registry.
- The GitHub release attests its artifacts, and npm records provenance; both need a public repository.
- A job that can mint an OIDC token (`id-token: write`) runs only GitHub's own actions and the registry's own publishing
  action. The publish workflow downloads the tree-sitter CLI and checks its SHA-256 for that reason.

## Verification

- After a change to `grammar.js` or `src/scanner.c`, run `tree-sitter generate` and `tree-sitter test` in this
  directory, and commit the regenerated `src/` files with the change. After a change to `grammar.js`, also run
  `npm run lint`.
- After a change to `grammar.js` or `src/scanner.c`, also parse every Quirrel file of a Dagor checkout. Run these
  commands from the root of the checkout, with `GRAMMAR` set to the directory of this grammar:

  ```sh
  git ls-files '*.nut' > /tmp/nut-files.txt
  tree-sitter parse --grammar-path "$GRAMMAR" --paths /tmp/nut-files.txt --quiet --stat
  ```

  Only test inputs of the compiler in `prog/1stPartyLibs/quirrel/quirrel/testData/` may fail, and only those that the
  compiler rejects too. The `sq` tool of the Quirrel source tree shows the error of the compiler:
  `sq -parse-only file.nut`.
- Write the expected tree of a new corpus test with its field names. `tree-sitter test --update` writes a new tree
  without them, and the test then ignores the fields.
- After a change to `queries/highlights.scm` or `queries/tags.scm`, run `tree-sitter test`, which runs the
  assertions in `test/highlight/` and `test/tags/`.
- The CLI caches one compiled parser per grammar name. After you parse with another grammar named `quirrel`, such as a
  copy at another revision, pass `--rebuild` to `tree-sitter test`.
- After a change to `tree-sitter.json`, `bindings/`, or a package manifest, build and test each binding in a copy of
  this directory, so that no build output lands in the tree: `cargo test`, `make && make test`, `go test ./...`,
  `npm install && npm test`, `swift test`, and `python -m unittest discover -s bindings/python/tests` after
  `pip install -e ".[core]"`.
