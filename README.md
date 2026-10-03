# tree-sitter-quirrel

[![CI][ci]](https://github.com/GaijinEntertainment/tree-sitter-quirrel/actions/workflows/ci.yml)
[![crates][crates]](https://crates.io/crates/tree-sitter-quirrel)
[![npm][npm]](https://www.npmjs.com/package/tree-sitter-quirrel)
[![pypi][pypi]](https://pypi.org/project/tree-sitter-quirrel)

A [tree-sitter](https://tree-sitter.github.io/) grammar for Quirrel, the scripting language of the
[Dagor Engine](https://github.com/GaijinEntertainment/DagorEngine). Editors with tree-sitter support use it for syntax
highlighting, and tools use it to read the structure of a `.nut` file.

The grammar follows the Quirrel compiler of the Dagor Engine, in
[`prog/1stPartyLibs/quirrel/quirrel/squirrel/compiler/`](https://github.com/GaijinEntertainment/DagorEngine/tree/main/prog/1stPartyLibs/quirrel/quirrel/squirrel/compiler):
the lexer in `lexer.cpp`, the parser in `parser.cpp`, and the type names in `sqtypeparser.cpp`. It parses every `.nut`
file of the Dagor source tree without errors, except the test inputs of the compiler that the compiler rejects too.

## What the grammar reads

- Statements: `local` and `let` declarations with type annotations and destructuring, `function`, `class`, `enum`,
  `const`, `global const` and `global enum` declarations, `if` (also with a `local` or `let` declaration as the
  condition), `while`, `do`-`while`, `for`, `foreach`, `switch`, `try` with typed catch clauses, `return`, `yield`,
  `break`, `continue`, `throw`, blocks, empty statements, docstrings (`@@"..."`), and directives (`#strict`,
  `#allow-switch-statement`, ...).
- Imports: `import "module" as name` and `from "module" import name as alias, *`, before the first other statement.
- Expressions with the operator precedence of the compiler: assignment (`=`, `<-`, `+=`, ...), the conditional
  operator, `??`, `||`, `&&`, the bitwise, relational (`in`, `not in`, `instanceof`), shift and arithmetic operators,
  the unary operators (`typeof`, `clone`, `delete`, `resume`, `await`, `static`, `const`), and the postfix chains `.`,
  `?.`, `.$`, `?.$`, `[]`, `?[]`, `()`, `?()`, `++` and `--`.
- Function expressions, lambdas (`@(x) x + 1`), `async` functions and lambdas, class expressions, tables (with
  shorthand slots, JSON keys, computed keys, spread and methods), arrays, `$${ ... }` code blocks, and `::name` root
  access.
- Literals: integers and floats with `_` separators, hexadecimal integers, strings with escapes, verbatim strings
  (`@"..."`), character literals, and template strings (`$"value {x}"`).
- The statement ends that the compiler applies. A statement ends at a line end, at `;`, before `}`, and after a
  statement that ends with `}`. An expression continues on the next line when the next token continues it, but `[` on
  the next line is an error and `++` on the next line starts a new statement. A line end inside a block comment does not
  end a statement.
- The `#pos:line:column` markers that the compiler accepts at any position.

The grammar accepts the input of every language mode. A file can change the mode with a directive, and the host
application sets the default mode. So `switch`, `case`, `default` and `clone` parse both as keywords and as names, and
`delete`, `::` and `$${ ... }` parse everywhere.

The grammar also accepts some input that the compiler rejects, because the compiler decides it with state that a
grammar does not keep:

- `=` inside parentheses in any expression. The compiler accepts it only where a plain assignment is allowed.
- A relational operator after `not in`, as in `a not in b < c`. The compiler ends the relational chain after `not in`.
- An import in a nested statement list. The compiler accepts it only while each statement that has ended in the file is
  an import, a directive, `;`, an expression statement, `return`, `yield`, `break`, `continue` or `throw`.

The grammar does not check what the compiler checks after it reads the syntax: the range of number literals, the names
in a declaration (duplicate catch types, the same name for the key and the value of a `foreach`, `async` on a
metamethod), the number of docstrings in a function, the nesting depth of an expression, and the checks of the code
generator.

This file:

```quirrel
from "math.nut" import max
let greet = @(name) $"hello, {name}"
```

gives this tree:

```
(source_file
  (import_statement
    module: (string (string_content))
    (import_specifier name: (identifier)))
  (local_declaration
    (variable_declarator
      name: (identifier)
      value: (lambda_expression
        parameters: (parameters (parameter name: (identifier)))
        body: (template_string
          (string_content)
          (template_substitution (identifier)))))))
```

## Use it in Neovim

[nvim-treesitter](https://github.com/nvim-treesitter/nvim-treesitter) (the `main` branch) builds the parser and
installs the highlight query. Its README lists what it needs.

1. Add this to `init.lua`:

   ```lua
   vim.api.nvim_create_autocmd('User', {
     pattern = 'TSUpdate',
     callback = function()
       require('nvim-treesitter.parsers').quirrel = {
         install_info = {
           url = 'https://github.com/GaijinEntertainment/tree-sitter-quirrel',
           queries = 'queries',
         },
       }
     end,
   })
   vim.filetype.add({ extension = { nut = 'quirrel' } })
   vim.api.nvim_create_autocmd('FileType', {
     pattern = 'quirrel',
     callback = function() vim.treesitter.start() end,
   })
   ```

   Neovim gives `.nut` files the `squirrel` filetype. The `vim.filetype.add` line gives them the `quirrel` filetype.

2. Run `:TSInstall quirrel`, then restart Neovim.

To build from a local copy of the grammar, replace `url` with `path` and the directory of that copy. In a Dagor
checkout, the directory is `prog/1stPartyLibs/quirrel/tree-sitter-quirrel`. nvim-treesitter then links the highlight
query to that directory, and `:TSInstall! quirrel` builds the parser again after a change to the grammar.

Other tree-sitter editors (Helix, Zed, Emacs) use the same grammar directory and `queries/highlights.scm`. Their own
documentation tells how to register a grammar.

## Use it from code

The `bindings/` directory holds bindings for C, Go, Node.js, Python, Rust and Swift. Their package manifests are at the
top of the repository: `CMakeLists.txt` and `Makefile`, `go.mod`, `package.json` and `binding.gyp`, `pyproject.toml`
and `setup.py`, `Cargo.toml`, and `Package.swift`.

## Change the grammar

The tree-sitter CLI 0.27.0 generated the files in `src/` from `grammar.js`, except `src/scanner.c`, which is written by
hand. After you change `grammar.js`, run these commands in this directory:

```sh
tree-sitter generate
tree-sitter test
```

Commit the regenerated `src/` files together with `grammar.js`. The corpus tests are in `test/corpus/`, and the
highlight assertions are in `test/highlight/`.

Then parse every Quirrel file of a Dagor checkout. Run these commands from the root of the checkout, with `GRAMMAR` set
to the directory of this grammar:

```sh
git ls-files '*.nut' > /tmp/nut-files.txt
tree-sitter parse --grammar-path "$GRAMMAR" --paths /tmp/nut-files.txt --quiet --stat
```

Only test inputs of the compiler in `prog/1stPartyLibs/quirrel/quirrel/testData/` may fail, and only those that the
compiler rejects too. The `sq` tool of the Quirrel source tree shows the error of the compiler:
`sq -parse-only file.nut`.

`tree-sitter init` generated the bindings and the package manifests from `tree-sitter.json`. After a change to
`tree-sitter.json`, run `tree-sitter init --update`. To release a new version, run `tree-sitter version <version>`,
which writes the version into `tree-sitter.json` and into every package manifest.

## License

MIT. See `LICENSE`.

[ci]: https://img.shields.io/github/actions/workflow/status/GaijinEntertainment/tree-sitter-quirrel/ci.yml?logo=github&label=CI
[crates]: https://img.shields.io/crates/v/tree-sitter-quirrel?logo=rust
[npm]: https://img.shields.io/npm/v/tree-sitter-quirrel?logo=npm
[pypi]: https://img.shields.io/pypi/v/tree-sitter-quirrel?logo=pypi&logoColor=ffd242
