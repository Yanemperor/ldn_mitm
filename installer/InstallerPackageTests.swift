import Foundation

@main
struct InstallerPackageTests {
    static func main() throws {
        guard CommandLine.arguments.count == 2 else { fatalError("usage: InstallerPackageTests payload-directory") }
        let fm = FileManager.default
        let payload = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
        let root = fm.temporaryDirectory.appendingPathComponent("ryulink-installer-package-test-\(UUID().uuidString)", isDirectory: true)
        defer { try? fm.removeItem(at: root) }
        let card = root.appendingPathComponent("switch-sd", isDirectory: true)

        try fm.createDirectory(at: card.appendingPathComponent("Nintendo"), withIntermediateDirectories: true)
        try fm.createDirectory(at: card.appendingPathComponent("switch"), withIntermediateDirectories: true)
        try fm.createDirectory(at: card.appendingPathComponent("atmosphere"), withIntermediateDirectories: true)
        let result = try SwitchInstaller.install(payload: payload, to: card, backupRoot: root.appendingPathComponent("backups"))
        precondition(result.files.count == SwitchInstaller.artifacts.count)
        let bootFlagSize = try fm.attributesOfItem(atPath: card.appendingPathComponent(SwitchInstaller.artifacts[2]).path)[.size] as? NSNumber
        precondition(bootFlagSize == 0)
        for file in result.files {
            let sourceHash = try SwitchInstaller.sha256(of: payload.appendingPathComponent(file.relativePath))
            let installedHash = try SwitchInstaller.sha256(of: card.appendingPathComponent(file.relativePath))
            precondition(sourceHash == installedHash)
        }
        print("Packaged installer test passed: embedded payload copied and verified on a simulated Switch SD card.")
    }
}
