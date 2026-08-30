import Foundation

@main
struct InstallerCoreTests {
    static func main() throws {
        let fm = FileManager.default
        let root = fm.temporaryDirectory.appendingPathComponent("ryulink-installer-test-\(UUID().uuidString)", isDirectory: true)
        defer { try? fm.removeItem(at: root) }
        let payload = root.appendingPathComponent("payload", isDirectory: true)
        let card = root.appendingPathComponent("switch-sd", isDirectory: true)
        let backups = root.appendingPathComponent("backups", isDirectory: true)

        try fm.createDirectory(at: card.appendingPathComponent("Nintendo"), withIntermediateDirectories: true)
        try fm.createDirectory(at: card.appendingPathComponent("switch"), withIntermediateDirectories: true)
        try fm.createDirectory(at: card.appendingPathComponent("atmosphere"), withIntermediateDirectories: true)
        try fm.createDirectory(at: card.appendingPathComponent("atmosphere/contents/4200000000000010"), withIntermediateDirectories: true)
        try fm.createDirectory(at: card.appendingPathComponent("atmosphere/contents.disabled/420000000000000B"), withIntermediateDirectories: true)
        try fm.createDirectory(at: card.appendingPathComponent("config/ldn_mitm"), withIntermediateDirectories: true)
        try Data("old-core".utf8).write(to: card.appendingPathComponent("atmosphere/contents/4200000000000010/exefs.nsp"))
        try Data("old-config".utf8).write(to: card.appendingPathComponent("config/ldn_mitm/relay.cfg"))
        try Data("legacy-core".utf8).write(to: card.appendingPathComponent("atmosphere/contents.disabled/420000000000000B/exefs.nsp"))

        for relativePath in SwitchInstaller.artifacts {
            let file = payload.appendingPathComponent(relativePath)
            try fm.createDirectory(at: file.deletingLastPathComponent(), withIntermediateDirectories: true)
            let data: Data
            switch relativePath {
            case "config/ldn_mitm/relay.cfg": data = Data("enabled=1\nbroadcast=1\nselected=RyuLink\nRyuLink relay.example:11451\n".utf8)
            case "atmosphere/contents/4200000000000010/flags/boot2.flag": data = Data()
            default: data = Data("new-\(relativePath)".utf8)
            }
            try data.write(to: file)
        }

        let result = try SwitchInstaller.install(payload: payload, to: card, backupRoot: backups)
        precondition(result.files.count == 4)
        for file in result.files {
            let sourceHash = try SwitchInstaller.sha256(of: payload.appendingPathComponent(file.relativePath))
            let installedHash = try SwitchInstaller.sha256(of: card.appendingPathComponent(file.relativePath))
            precondition(sourceHash == installedHash)
        }
        precondition(fm.fileExists(atPath: result.backupDirectory.appendingPathComponent("atmosphere/contents/4200000000000010/exefs.nsp").path))
        let backupConfig = try Data(contentsOf: result.backupDirectory.appendingPathComponent("config/ldn_mitm/relay.cfg"))
        precondition(backupConfig == Data("old-config".utf8))
        let backupLegacyCore = try Data(contentsOf: result.backupDirectory.appendingPathComponent("atmosphere/contents.disabled/420000000000000B/exefs.nsp"))
        precondition(backupLegacyCore == Data("legacy-core".utf8))
        print("Installer core test passed: copied, hashed, and backed up all four files.")
    }
}
