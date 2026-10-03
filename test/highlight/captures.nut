from "ui.nut" import button as btn, *
// <- keyword.import
//   ^ string
//            ^ keyword.import
//                   ^ variable
//                          ^ keyword.import
//                                  ^ character.special
import "math" as m
// <- keyword.import
//               ^ module
#allow-delete-operator
// <- keyword.directive
@@"Module docs"
// <- string.documentation
local MAX_COUNT = 10
// <- keyword
//    ^ constant
//              ^ operator
//                ^ number
let ratio: float|null = 1.5
// <- keyword
//  ^ variable
//         ^ type.builtin
//              ^ operator
//               ^ type.builtin
//                      ^ number.float
function compute(a, b: int, ...) {
// <- keyword.function
//       ^ function
//               ^ variable.parameter
//                  ^ variable.parameter
//                     ^ type.builtin
//                          ^ variable.parameter
  if (a not in b) return null
  // <- keyword.conditional
  //    ^ keyword.operator
  //        ^ keyword.operator
  //              ^ keyword.return
  //                     ^ constant.builtin
  foreach (k, v in a) {
  // <- keyword.repeat
  //       ^ variable
    if (v == null)
    //    ^ operator
      continue
      // <- keyword.repeat
  }
  return a?.b ?? @(x) x + 1
  //      ^ punctuation.delimiter
  //        ^ variable.member
  //          ^ operator
  //             ^ keyword.function
  //              ^ punctuation.bracket
  //               ^ variable.parameter
}
class Widget (Base) {
// <- keyword.type
//    ^ type
//            ^ type
  static count = 0
  // <- keyword.modifier
  //     ^ variable.member
  constructor(value) {
  // <- constructor
  //          ^ variable.parameter
    base.constructor(value)
    // <- variable.builtin
    //   ^ constructor
  }
  function render() { return this.count }
  //       ^ function.method
  //                         ^ variable.builtin
  //                              ^ variable.member
  async function load() { await fetch() }
  // <- keyword.coroutine
  //                      ^ keyword.coroutine
  //                            ^ function.call
}
enum Color { RED, GREEN = -1 }
// <- keyword.type
//   ^ type
//           ^ constant
//                ^ constant
//                        ^ operator
//                         ^ number
try { throw "x" } catch (Error e) { print(e) }
// <- keyword.exception
//    ^ keyword.exception
//                ^ keyword.exception
//                       ^ type
//                             ^ variable.parameter
//                                  ^ function.call
let text = $"value {MAX_COUNT}\n"
//         ^ string
//                 ^ punctuation.special
//                  ^ constant
//                           ^ punctuation.special
//                            ^ string.escape
let c = 'x'
//      ^ character
local t = { key = true, short, [k] = false }
//          ^ variable.member
//                ^ boolean
//                      ^ variable.member
//                             ^ punctuation.bracket
//                                   ^ boolean
x = typeof t == "table" ? clone t : delete t.key
//  ^ keyword.operator
//                      ^ keyword.conditional.ternary
//                        ^ keyword.operator
//                                ^ keyword.conditional.ternary
//                                  ^ keyword.operator
//                                           ^ variable.member
::root.call(__FILE__, __LINE__)
// <- punctuation.delimiter
  // <- variable
//     ^ function.method.call
//          ^ constant.builtin
//                    ^ constant.builtin
let f = function [pure] named() {}
//                ^ attribute
//                      ^ function
let g = a.b(c)?[0]
//        ^ function.method.call
//            ^ punctuation.bracket
local default = 1 // a comment
//    ^ variable
//                ^ comment
#allow-switch-statement
// <- keyword.directive
switch (x) { case 1: break; default: yield }
// <- keyword.conditional
//           ^ keyword.conditional
//                   ^ keyword.repeat
//                          ^ keyword.conditional
//                                   ^ keyword.return
