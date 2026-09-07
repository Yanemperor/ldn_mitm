import AppKit
import SwiftUI

@MainActor
final class InstallerViewModel: ObservableObject {
    @Published private(set) var card: URL?
    @Published private(set) var status = ""
    @Published private(set) var statusIsError = false
    @Published var showRemovalConfirmation = false

    init() { detectCard(reportFailure: false) }

    func detectCard(reportFailure: Bool = true) {
        let cards = SwitchInstaller.findSwitchVolumes()
        card = cards.count == 1 ? cards[0] : nil
        guard reportFailure, card == nil else {
            statusIsError = false
            status = ""
            return
        }
        statusIsError = true
        status = cards.count > 1 ? "检测到多张 Switch SD 卡" : "未检测到 Switch SD 卡"
    }

    func install() {
        detectCard(reportFailure: true)
        guard let card else { return }
        guard let payload = Bundle.main.resourceURL?.appendingPathComponent("payload", isDirectory: true) else {
            status = "无法读取内置安装包。"
            return
        }

        do {
            let library = try FileManager.default.url(for: .applicationSupportDirectory, in: .userDomainMask, appropriateFor: nil, create: true)
            let backups = library.appendingPathComponent("RyuLink Installer/Backups", isDirectory: true)
            _ = try SwitchInstaller.install(payload: payload, to: card, backupRoot: backups)
            do {
                try NSWorkspace.shared.unmountAndEjectDevice(at: card)
                statusIsError = false
                status = "安装成功"
            } catch {
                statusIsError = true
                status = "请手动安全推出 SD 卡"
            }
        } catch {
            statusIsError = true
            status = error.localizedDescription
        }
    }

    func requestHistoricalVersionRemoval() {
        detectCard(reportFailure: true)
        guard card != nil else { return }
        showRemovalConfirmation = true
    }

    func removeHistoricalVersion() {
        guard let card else { return }
        do {
            let removed = try SwitchInstaller.removeHistoricalVersion(from: card)
            statusIsError = false
            status = removed.isEmpty
                ? "未找到可删除的 RyuLink 历史版本文件。"
                : "已删除 RyuLink 历史版本。现在可安装新版本。"
        } catch {
            statusIsError = true
            status = error.localizedDescription
        }
    }
}

@main
struct RyuLinkInstallerApp: App {
    @StateObject private var model = InstallerViewModel()

    var body: some Scene {
        WindowGroup("RyuLink 一键安装器") {
            VStack(alignment: .leading, spacing: 18) {
                Text("RyuLink 一键安装器").font(.title2).bold()
                Text(model.status)
                    .font(.title3).bold()
                    .foregroundStyle(model.statusIsError ? .red : .green)
                    .frame(minHeight: 48, alignment: .leading)
                HStack {
                    Button("重新检测") { model.detectCard() }
                    Button("删除历史版本", action: model.requestHistoricalVersionRemoval)
                    Button("安装、校验并推出", action: model.install)
                        .keyboardShortcut(.defaultAction)
                }
            }
            .padding(24)
            .frame(width: 520)
            .alert("删除 RyuLink 历史版本？", isPresented: $model.showRemovalConfirmation) {
                Button("取消", role: .cancel) {}
                Button("删除", role: .destructive, action: model.removeHistoricalVersion)
            } message: {
                Text("仅删除 RyuLink App、已启用的 ldn_mitm Core、其 overlay 和 RyuLink relay 配置。不会删除其他 Atmosphère 文件或已禁用的旧 Core。")
            }
        }
    }
}
