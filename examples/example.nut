from "dagor.math" import Point2, Point3
import "string.nut" as str

@@"Tracks scores of the players in a match."

const MAX_PLAYERS = 16

enum Team {
  RED = 1
  BLUE = 2
}

let defaults = freeze({
  name = "player"
  score = 0
  team = Team.RED
})

class Player {
  name = null
  score = 0

  constructor(params = {}) {
    let { name = defaults.name, score = defaults.score } = params
    this.name = name
    this.score = score
  }

  function title() {
    return $"{this.name} ({this.score})"
  }
}

function makeRoster(names: array, ...): table {
  let roster = {}
  foreach (i, name in names) {
    if (i >= MAX_PLAYERS)
      break
    roster[name] <- Player({ name })
  }
  return roster
}

let best = @(roster) roster
  .values()
  .reduce(@(a, b) a == null || b.score > a.score ? b : a, null)

local function report(roster) {
  let winner = best(roster)
  if (winner == null)
    return "no players"
  return str.format("winner: %s", winner?.title() ?? "unknown")
}

try {
  let roster = makeRoster(["a", "b", "c"])
  println(report(roster))
} catch (e) {
  println($"failed: {e}")
}

return { Player, makeRoster, report }
