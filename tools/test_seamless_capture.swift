// SPDX-License-Identifier: GPL-2.0-or-later
import Foundation

@main struct CaptureFilterTest {
    static func main() {
        // Shapes from seamless windows, including the taskbar and the tooltip
        // that covers its Start button. A minimum-size filter loses both.
        let sizes: [(String, Int, Int)] = [
            ("application", 672, 480), ("taskbar", 1474, 30),
            ("Start menu", 177, 342), ("tooltip", 97, 17),
            ("context menu", 180, 90), ("menu bar popup", 160, 120),
            ("combo dropdown", 120, 20), ("small popup", 1, 1)
        ]
        var windows: [CaptureWindow] = []
        for (index, size) in sizes.enumerated() {
            windows.append(CaptureWindow(id: UInt32(index + 1), processID: 100,
                frame: CGRect(x: 28, y: 892, width: size.1, height: size.2)))
        }
        windows.append(CaptureWindow(id: 90, processID: 101, frame: .zero))
        windows.append(CaptureWindow(id: 91, processID: 999, frame: CGRect(x: 0, y: 0, width: 1470, height: 956)))
        windows.append(CaptureWindow(id: 92, processID: nil, frame: .zero))
        let selected = captureWindowIDs(windows, processes: [100, 101])
        var failed = false
        for (index, size) in sizes.enumerated() {
            let okay = selected.contains(UInt32(index + 1))
            print("\(okay ? "PASS" : "FAIL") includes \(size.0)")
            failed = failed || !okay
        }
        let isolated = selected == Set(Array(UInt32(1)...UInt32(sizes.count)) + [90])
        print("\(isolated ? "PASS" : "FAIL") includes requested child process, excludes foreign and unowned windows")
        exit(failed || !isolated ? 1 : 0)
    }
}
