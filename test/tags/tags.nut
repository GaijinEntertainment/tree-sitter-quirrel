// Builds the greeting.
function greet(name) {
//       ^ definition.function
  return $"hello, {name}"
}

local function helper() { return 1 }
//             ^ definition.function

const function [pure] twice(x) { return x * 2 }
//                    ^ definition.function

const LIMIT = 10
//    ^ definition.constant

let square = @(x) x * x
//  ^ definition.function

enum Color { RED, GREEN }
//   ^ definition.class

// A shape with an area.
class Shape {
//    ^ definition.class
  function area() { return 0 }
  //       ^ definition.method
}

class Square (Shape) {
//    ^ definition.class
//            ^ reference.class
  side = 1
}

local api = {
  load = function() { return null }
  // <- definition.function
}

greet("x")
// <- reference.call
api.load()
//  ^ reference.call
::print(square(LIMIT))
//^ reference.call
//      ^ reference.call
