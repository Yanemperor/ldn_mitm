using System.Security.Cryptography;
using System.Text.RegularExpressions;

namespace RyuLink.Installer.Core;

public enum InstallationStep { ValidatingPayload, BackingUpExistingFiles, CopyingFiles, VerifyingFiles, Completed }
public sealed record InstallationProgress(InstallationStep Step, string Message);
public sealed record InstalledFile(string RelativePath, string Sha256);
public sealed record InstallationResult(string BackupDirectory, IReadOnlyList<InstalledFile> Files);
public sealed record HistoricalVersionRemovalResult(IReadOnlyList<string> RemovedPaths);

public sealed class InstallationService
{
    private const string BootFlag = "atmosphere/contents/4200000000000010/flags/boot2.flag";
    private const string RelayConfig = "config/ldn_mitm/relay.cfg";
    private static readonly string[] HistoricalVersionPaths =
    [
        "switch/RyuLink",
        "switch/.overlays/ldnmitm_config.ovl",
        "atmosphere/contents/4200000000000010",
        RelayConfig,
    ];
    public static readonly string[] RequiredFiles =
    [
        "switch/RyuLink/RyuLink.nro",
        "switch/.overlays/ldnmitm_config.ovl",
        "atmosphere/contents/4200000000000010/exefs.nsp",
        BootFlag,
        RelayConfig,
    ];

    public async Task<InstallationResult> InstallAsync(string payloadDirectory, SdCardCandidate target, string backupRoot, IProgress<InstallationProgress>? progress = null, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(target);
        ValidateTarget(target);
        return await InstallToRootAsync(payloadDirectory, target.RootPath, backupRoot, progress, cancellationToken);
    }

    public async Task<InstallationResult> InstallToRootAsync(string payloadDirectory, string sdRoot, string backupRoot, IProgress<InstallationProgress>? progress = null, CancellationToken cancellationToken = default)
    {
        progress?.Report(new(InstallationStep.ValidatingPayload, "正在验证安装包…"));
        ValidatePayload(payloadDirectory);

        progress?.Report(new(InstallationStep.BackingUpExistingFiles, "正在备份已有 Core 与 Relay 配置…"));
        var backupDirectory = await BackupExistingFilesAsync(sdRoot, backupRoot, cancellationToken);

        progress?.Report(new(InstallationStep.CopyingFiles, "正在复制 RyuLink 必需文件…"));
        foreach (var relativePath in RequiredFiles)
        {
            await CopyFileAsync(Path.Combine(payloadDirectory, relativePath), Path.Combine(sdRoot, relativePath), cancellationToken);
        }

        progress?.Report(new(InstallationStep.VerifyingFiles, "正在校验已写入的文件…"));
        var installed = new List<InstalledFile>();
        foreach (var relativePath in RequiredFiles)
        {
            var sourceHash = await Sha256Async(Path.Combine(payloadDirectory, relativePath), cancellationToken);
            var targetHash = await Sha256Async(Path.Combine(sdRoot, relativePath), cancellationToken);
            if (!string.Equals(sourceHash, targetHash, StringComparison.Ordinal))
            {
                throw new InvalidOperationException($"校验失败：{relativePath}");
            }
            installed.Add(new(relativePath, sourceHash));
        }
        RemoveResourceForkSidecars(sdRoot);

        progress?.Report(new(InstallationStep.Completed, "安装并校验完成。请安全弹出 SD 卡后完整重启 CFW。"));
        return new(backupDirectory, installed);
    }

    /// <summary>
    /// Removes only files and directories owned by RyuLink and its enabled ldn_mitm Core.
    /// It intentionally does not touch other Atmosphère content or the legacy disabled Core.
    /// </summary>
    public Task<HistoricalVersionRemovalResult> RemoveHistoricalVersionAsync(SdCardCandidate target, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(target);
        ValidateTarget(target);
        return RemoveHistoricalVersionFromRootAsync(target.RootPath, cancellationToken);
    }

    public Task<HistoricalVersionRemovalResult> RemoveHistoricalVersionFromRootAsync(string sdRoot, CancellationToken cancellationToken = default)
    {
        var removed = new List<string>();
        foreach (var relativePath in HistoricalVersionPaths)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var path = GetManagedPath(sdRoot, relativePath);
            if (File.Exists(path))
            {
                File.Delete(path);
                removed.Add(relativePath);
            }
            else if (Directory.Exists(path))
            {
                Directory.Delete(path, recursive: true);
                removed.Add(relativePath);
            }
        }
        return Task.FromResult<HistoricalVersionRemovalResult>(new(removed));
    }

    public static void ValidatePayload(string payloadDirectory)
    {
        foreach (var relativePath in RequiredFiles)
        {
            if (!File.Exists(Path.Combine(payloadDirectory, relativePath)))
            {
                throw new InvalidOperationException($"安装包缺少：{relativePath}");
            }
        }
        if (new FileInfo(Path.Combine(payloadDirectory, BootFlag)).Length != 0)
        {
            throw new InvalidOperationException("boot2.flag 必须是零字节文件。");
        }

        var relayConfig = File.ReadAllText(Path.Combine(payloadDirectory, RelayConfig));
        foreach (var setting in new[] { @"(?im)^\s*enabled\s*=\s*1\s*$", @"(?im)^\s*broadcast\s*=\s*1\s*$", @"(?im)^\s*selected\s*=\s*RyuLink\s*$", @"(?im)^\s*RyuLink\s+.+$" })
        {
            if (!Regex.IsMatch(relayConfig, setting))
            {
                throw new InvalidOperationException("relay.cfg 不符合 RyuLink Relay 配置。");
            }
        }
        if (Regex.IsMatch(relayConfig, @"(?im)^\s*(password|token|private[_-]?key)\s*="))
        {
            throw new InvalidOperationException("relay.cfg 不得包含密码、token 或私钥。");
        }
    }

    private static void ValidateTarget(SdCardCandidate target)
    {
        var rootPath = Path.GetFullPath(target.RootPath);
        if (!target.IsLikelySwitchSdCard || (OperatingSystem.IsWindows() && !string.Equals(rootPath, Path.GetPathRoot(rootPath), StringComparison.OrdinalIgnoreCase)) || (OperatingSystem.IsMacOS() && !string.Equals(Path.GetDirectoryName(rootPath.TrimEnd(Path.DirectorySeparatorChar)), "/Volumes", StringComparison.Ordinal)))
        {
            throw new InvalidOperationException("目标不是有效的 Switch SD 卡。");
        }
    }

    private static async Task<string> BackupExistingFilesAsync(string sdRoot, string backupRoot, CancellationToken cancellationToken)
    {
        var backupDirectory = Path.Combine(backupRoot, $"{Path.GetFileName(Path.TrimEndingDirectorySeparator(sdRoot))}-{DateTimeOffset.Now:yyyyMMdd_HHmmss}");
        var backedUp = false;
        foreach (var relativePath in new[] { "atmosphere/contents/4200000000000010", "atmosphere/contents.disabled/420000000000000B", "config/ldn_mitm", "switch/.overlays" })
        {
            var source = Path.Combine(sdRoot, relativePath);
            if (Directory.Exists(source))
            {
                backedUp = true;
                await CopyDirectoryAsync(source, Path.Combine(backupDirectory, relativePath), cancellationToken);
            }
        }
        return backedUp ? backupDirectory : string.Empty;
    }

    private static async Task CopyDirectoryAsync(string sourceDirectory, string destinationDirectory, CancellationToken cancellationToken)
    {
        foreach (var sourceFile in Directory.EnumerateFiles(sourceDirectory, "*", SearchOption.AllDirectories))
        {
            var destination = Path.Combine(destinationDirectory, Path.GetRelativePath(sourceDirectory, sourceFile));
            await CopyFileAsync(sourceFile, destination, cancellationToken);
        }
    }

    private static async Task CopyFileAsync(string sourceFile, string destinationFile, CancellationToken cancellationToken)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(destinationFile)!);
        var temporaryFile = destinationFile + ".ryulink-install-tmp";
        try
        {
            await using (var input = File.OpenRead(sourceFile))
            await using (var output = File.Create(temporaryFile))
            {
                await input.CopyToAsync(output, cancellationToken);
            }
            File.Move(temporaryFile, destinationFile, overwrite: true);
        }
        finally
        {
            if (File.Exists(temporaryFile)) File.Delete(temporaryFile);
        }
    }

    private static async Task<string> Sha256Async(string file, CancellationToken cancellationToken)
    {
        await using var stream = File.OpenRead(file);
        return Convert.ToHexString(await SHA256.HashDataAsync(stream, cancellationToken)).ToLowerInvariant();
    }

    private static void RemoveResourceForkSidecars(string sdRoot)
    {
        foreach (var directory in RequiredFiles.Select(path => Path.GetDirectoryName(Path.Combine(sdRoot, path))!).Distinct())
        {
            if (Directory.Exists(directory))
            {
                foreach (var sidecar in Directory.EnumerateFiles(directory, "._*")) File.Delete(sidecar);
            }
        }
    }

    private static string GetManagedPath(string sdRoot, string relativePath)
    {
        var root = Path.GetFullPath(sdRoot);
        var candidate = Path.GetFullPath(Path.Combine(root, relativePath));
        var prefix = Path.EndsInDirectorySeparator(root) ? root : root + Path.DirectorySeparatorChar;
        var comparison = OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal;
        if (!candidate.StartsWith(prefix, comparison))
        {
            throw new InvalidOperationException("拒绝删除 SD 卡根目录以外的文件。");
        }
        return candidate;
    }
}
