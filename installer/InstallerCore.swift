import Foundation
import CryptoKit

enum InstallerError: LocalizedError {
    case invalidPayload(String)
    case noSwitchCard
    case verificationFailed(String)

    var errorDescription: String? {
        switch self {
        case .invalidPayload(let message), .verificationFailed(let message): return message
        case .noSwitchCard: return "没有检测到可用的 Switch SD 卡。"
        }
    }
}

struct InstalledFile: Equatable {
    let relativePath: String
    let sha256: String
}

struct InstallResult {
    let backupDirectory: URL
    let files: [InstalledFile]
}

enum SwitchInstaller {
    static let artifacts = [
        "switch/RyuLink/RyuLink.nro",
        "atmosphere/contents/4200000000000010/exefs.nsp",
        "atmosphere/contents/4200000000000010/flags/boot2.flag",
        "config/ldn_mitm/relay.cfg",
    ]

    static func findSwitchVolumes() -> [URL] {
        let keys: Set<URLResourceKey> = [.volumeIsRemovableKey, .volumeNameKey]
        return FileManager.default.mountedVolumeURLs(includingResourceValuesForKeys: Array(keys), options: [.skipHiddenVolumes])?.filter { volume in
            let values = try? volume.resourceValues(forKeys: keys)
            return values?.volumeIsRemovable == true && looksLikeSwitchCard(volume)
        } ?? []
    }

    static func looksLikeSwitchCard(_ root: URL) -> Bool {
        let fm = FileManager.default
        return fm.fileExists(atPath: root.appendingPathComponent("switch", isDirectory: true).path)
            && fm.fileExists(atPath: root.appendingPathComponent("atmosphere", isDirectory: true).path)
    }

    static func install(payload: URL, to card: URL, backupRoot: URL) throws -> InstallResult {
        try validatePayload(at: payload)
        guard looksLikeSwitchCard(card) else { throw InstallerError.noSwitchCard }

        let backup = try makeBackup(of: card, in: backupRoot)
        var installed: [InstalledFile] = []

        for relativePath in artifacts {
            let source = payload.appendingPathComponent(relativePath)
            let destination = card.appendingPathComponent(relativePath)
            try FileManager.default.createDirectory(at: destination.deletingLastPathComponent(), withIntermediateDirectories: true)
            try Data(contentsOf: source).write(to: destination, options: .atomic)

            let sourceHash = try sha256(of: source)
            guard sourceHash == (try sha256(of: destination)) else {
                throw InstallerError.verificationFailed("校验失败：\(relativePath)")
            }
            installed.append(InstalledFile(relativePath: relativePath, sha256: sourceHash))
        }

        try removeResourceForkSidecars(from: card)
        return InstallResult(backupDirectory: backup, files: installed)
    }

    static func validatePayload(at payload: URL) throws {
        let fm = FileManager.default
        for relativePath in artifacts where !fm.fileExists(atPath: payload.appendingPathComponent(relativePath).path) {
            throw InstallerError.invalidPayload("安装包缺少：\(relativePath)")
        }

        let bootFlag = payload.appendingPathComponent(artifacts[2])
        guard try fm.attributesOfItem(atPath: bootFlag.path)[.size] as? NSNumber == 0 else {
            throw InstallerError.invalidPayload("boot2.flag 必须是零字节文件。")
        }

        let config = try String(contentsOf: payload.appendingPathComponent(artifacts[3]), encoding: .utf8)
        for setting in ["(?im)^\\s*enabled\\s*=\\s*1\\s*$", "(?im)^\\s*broadcast\\s*=\\s*1\\s*$", "(?im)^\\s*selected\\s*=\\s*RyuLink\\s*$", "(?im)^\\s*RyuLink\\s+.+$"] {
            guard config.range(of: setting, options: .regularExpression) != nil else {
                throw InstallerError.invalidPayload("relay.cfg 不符合 RyuLink Relay 配置。")
            }
        }
        if config.range(of: "(?im)^\\s*(password|token|private[_-]?key)\\s*=", options: .regularExpression) != nil {
            throw InstallerError.invalidPayload("relay.cfg 不得包含密码、token 或私钥。")
        }
    }

    static func sha256(of file: URL) throws -> String {
        let digest = SHA256.hash(data: try Data(contentsOf: file))
        return digest.map { String(format: "%02x", $0) }.joined()
    }

    private static func makeBackup(of card: URL, in backupRoot: URL) throws -> URL {
        let fm = FileManager.default
        let stamp = ISO8601DateFormatter().string(from: Date()).replacingOccurrences(of: ":", with: "-")
        let volumeName = card.lastPathComponent.replacingOccurrences(of: "/", with: "-")
        let backup = backupRoot.appendingPathComponent("\(volumeName)-\(stamp)", isDirectory: true)
        try fm.createDirectory(at: backup, withIntermediateDirectories: true)

        for relativePath in ["atmosphere/contents/4200000000000010", "atmosphere/contents.disabled/420000000000000B", "config/ldn_mitm"] {
            let source = card.appendingPathComponent(relativePath)
            if fm.fileExists(atPath: source.path) {
                let destination = backup.appendingPathComponent(relativePath)
                try fm.createDirectory(at: destination.deletingLastPathComponent(), withIntermediateDirectories: true)
                try fm.copyItem(at: source, to: destination)
            }
        }
        return backup
    }

    private static func removeResourceForkSidecars(from card: URL) throws {
        let fm = FileManager.default
        let directories = Set(artifacts.map { card.appendingPathComponent($0).deletingLastPathComponent() })
        for directory in directories {
            for item in try fm.contentsOfDirectory(at: directory, includingPropertiesForKeys: nil) where item.lastPathComponent.hasPrefix("._") {
                try fm.removeItem(at: item)
            }
        }
    }
}
