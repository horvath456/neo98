// SPDX-License-Identifier: GPL-2.0-or-later
// Capture only windows owned by the supplied host process IDs on the main display.
// Build: swiftc -parse-as-library tools/capture_host_windows.swift -o build/capture-host-windows
// Run: build/capture-host-windows output.png HOST_PID [CHILD_PID ...]
import AppKit
import ScreenCaptureKit
import ImageIO
import UniformTypeIdentifiers

struct CaptureWindow {
    let id: CGWindowID
    let processID: pid_t?
    let frame: CGRect
}

func captureWindowIDs(_ windows: [CaptureWindow], processes: Set<pid_t>) -> Set<CGWindowID> {
    // A tooltip or a taskbar can be shorter than an ordinary window. The
    // windows underneath already have holes for it, so it must be captured.
    Set(windows.filter { window in
        guard let owner = window.processID else { return false }
        return processes.contains(owner)
    }.map { $0.id })
}

#if !CAPTURE_FILTER_TEST
@main struct HostWindowCapture {
    static func main() async {
        do {
            let args = CommandLine.arguments
            let processes = args.dropFirst(2).compactMap { pid_t($0) }
            guard args.count >= 3, processes.count == args.count - 2,
                  processes.allSatisfy({ $0 > 0 }) else {
                throw NSError(domain: "Capture", code: 1, userInfo: [NSLocalizedDescriptionKey:
                    "Usage: capture-host-windows output.png HOST_PID [CHILD_PID ...]"])
            }
            let content = try await SCShareableContent.excludingDesktopWindows(true, onScreenWindowsOnly: true)
            let ids = captureWindowIDs(content.windows.map {
                CaptureWindow(id: $0.windowID, processID: $0.owningApplication?.processID, frame: $0.frame)
            }, processes: Set(processes))
            guard !ids.isEmpty, let display = content.displays.first(where: { $0.displayID == CGMainDisplayID() }) else {
                throw NSError(domain: "Capture", code: 2, userInfo: [NSLocalizedDescriptionKey:
                    "No host windows or main display available"])
            }
            let windows = content.windows.filter { ids.contains($0.windowID) }
            let filter = SCContentFilter(display: display, including: windows)
            let configuration = SCStreamConfiguration()
            configuration.width = Int(filter.contentRect.width)
            configuration.height = Int(filter.contentRect.height)
            configuration.showsCursor = false
            let image = try await SCScreenshotManager.captureImage(contentFilter: filter, configuration: configuration)
            guard let destination = CGImageDestinationCreateWithURL(URL(fileURLWithPath: args[1]) as CFURL,
                    UTType.png.identifier as CFString, 1, nil) else {
                throw NSError(domain: "Capture", code: 3, userInfo: [NSLocalizedDescriptionKey: "Cannot create PNG"])
            }
            CGImageDestinationAddImage(destination, image, nil)
            guard CGImageDestinationFinalize(destination) else {
                throw NSError(domain: "Capture", code: 4, userInfo: [NSLocalizedDescriptionKey: "Cannot write PNG"])
            }
            for window in windows {
                print("Captured window \(window.windowID) pid=\(window.owningApplication!.processID) frame=\(window.frame)")
            }
        } catch {
            FileHandle.standardError.write(Data("\(error.localizedDescription)\n".utf8))
            exit(1)
        }
    }
}
#endif
