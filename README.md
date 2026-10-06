# tree-sitter-quirrel

[![CI][ci]](https://github.com/GaijinEntertainment/tree-sitter-quirrel/actions/workflows/ci.yml)
[![crates][crates]](https://crates.io/crates/tree-sitter-quirrel)
[![npm][npm]](https://www.npmjs.com/package/tree-sitter-quirrel)
[![pypi][pypi]](https://pypi.org/project/tree-sitter-quirrel)

Quirrel grammar for [tree-sitter](https://github.com/tree-sitter/tree-sitter). Quirrel is the scripting language of the
[Dagor Engine](https://github.com/GaijinEntertainment/DagorEngine).

The grammar follows the Quirrel compiler of the Dagor Engine and accepts the input of every language mode. The compiler
rejects some input because of state that a grammar does not keep, and the grammar accepts that input. The grammar does
not do the checks that the compiler does after it reads the syntax, such as the limit on the nesting depth of an
expression.

## Versioning

The version of the grammar is `X.Y.P`. It is not a semantic version.

```text
4.43.1
|    |
|    +-- P: the number of the grammar release for that Quirrel version; the first release is 0
+------- X.Y: the newest Quirrel version that the grammar covers
```

- `X.Y` is the major and minor number of the newest Quirrel version that the grammar covers. Each `4.43.P` covers
  Quirrel 4.43, and the first release for Quirrel 4.44 is `4.44.0`.
- `P` is the grammar's own number. It counts the releases of the grammar for one Quirrel version, and it does not
  follow the patch number of the compiler. `4.43.1` is the second release of the grammar for Quirrel 4.43. It is not a
  grammar for the compiler release 4.43.1.

A new `P` is a new release of the grammar for the same Quirrel version: a correction of the trees or of the queries. A
new `X.Y` adds the syntax of a newer Quirrel version.

## References

- [The Quirrel compiler](https://github.com/GaijinEntertainment/DagorEngine/tree/main/prog/1stPartyLibs/quirrel/quirrel/squirrel/compiler)

[ci]: https://img.shields.io/github/actions/workflow/status/GaijinEntertainment/tree-sitter-quirrel/ci.yml?logo=github&label=CI
[crates]: https://img.shields.io/crates/v/tree-sitter-quirrel?logo=rust
[npm]: https://img.shields.io/npm/v/tree-sitter-quirrel?logo=npm
[pypi]: https://img.shields.io/pypi/v/tree-sitter-quirrel?logo=pypi&logoColor=ffd242
