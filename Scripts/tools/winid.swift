// Prints the window id of the largest on-screen window owned by a process whose name contains the argument
// (for `screencapture -l <id>`, which captures a window even when other windows cover it). With a second
// argument "bounds" it prints "id x y width height" in points.
import CoreGraphics
import Foundation
let needle = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "UnrealEditor"
let info = CGWindowListCopyWindowInfo([.optionOnScreenOnly], kCGNullWindowID) as? [[String: Any]] ?? []
var best: (Int, Double) = (0, 0)
var bestBounds: [String: Double] = [:]
for w in info {
    guard let owner = w[kCGWindowOwnerName as String] as? String, owner.contains(needle),
          let id = w[kCGWindowNumber as String] as? Int,
          let b = w[kCGWindowBounds as String] as? [String: Double] else { continue }
    let area = (b["Width"] ?? 0) * (b["Height"] ?? 0)
    if area > best.1 { best = (id, area); bestBounds = b }
}
if CommandLine.arguments.count > 2 && CommandLine.arguments[2] == "bounds" {
    print(best.0, Int(bestBounds["X"] ?? 0), Int(bestBounds["Y"] ?? 0), Int(bestBounds["Width"] ?? 0), Int(bestBounds["Height"] ?? 0))
} else {
    print(best.0)
}
