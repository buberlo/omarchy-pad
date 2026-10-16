import Foundation
import CoreGraphics
let windows = CGWindowListCopyWindowInfo([.optionOnScreenOnly, .excludeDesktopElements], kCGNullWindowID) as? [[String:Any]] ?? []
for w in windows {
 if let owner=w[kCGWindowOwnerName as String] as? String, owner.contains("Device") || owner.contains("Geräte") {
 print(w)
 }
}
