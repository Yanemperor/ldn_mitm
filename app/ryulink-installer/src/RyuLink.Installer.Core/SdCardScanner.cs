namespace RyuLink.Installer.Core;

public sealed record SdCardCandidate(string RootPath, string VolumeLabel, long TotalSizeBytes, bool HasAtmosphere, bool HasSwitch)
{
    public bool IsLikelySwitchSdCard => HasAtmosphere && HasSwitch;
}

public interface IDriveProvider
{
    IEnumerable<DriveInfo> GetDrives();
}

public sealed class SystemDriveProvider : IDriveProvider
{
    public IEnumerable<DriveInfo> GetDrives() => DriveInfo.GetDrives();
}

public sealed class SdCardScanner(IDriveProvider driveProvider)
{
    public IReadOnlyList<SdCardCandidate> FindCandidates()
    {
        var candidates = new List<SdCardCandidate>();
        var systemRoot = Path.GetPathRoot(Environment.SystemDirectory);

        foreach (var drive in driveProvider.GetDrives())
        {
            if (!drive.IsReady || drive.DriveType is DriveType.CDRom or DriveType.Network)
            {
                continue;
            }
            AddCandidate(candidates, drive.RootDirectory.FullName, systemRoot);
        }

        if (OperatingSystem.IsMacOS() && Directory.Exists("/Volumes"))
        {
            foreach (var volumePath in Directory.EnumerateDirectories("/Volumes"))
            {
                AddCandidate(candidates, volumePath, systemRoot);
            }
        }

        return candidates
            .DistinctBy(candidate => candidate.RootPath, StringComparer.OrdinalIgnoreCase)
            .Where(candidate => candidate.IsLikelySwitchSdCard)
            .OrderBy(candidate => candidate.RootPath, StringComparer.OrdinalIgnoreCase)
            .ToArray();
    }

    private static void AddCandidate(ICollection<SdCardCandidate> candidates, string root, string? systemRoot)
    {
        if (string.Equals(root, systemRoot, StringComparison.OrdinalIgnoreCase))
        {
            return;
        }

        try
        {
            var drive = new DriveInfo(root);
            candidates.Add(new(
                root,
                drive.IsReady ? drive.VolumeLabel : Path.GetFileName(root),
                drive.IsReady ? drive.TotalSize : 0,
                Directory.Exists(Path.Combine(root, "atmosphere")),
                Directory.Exists(Path.Combine(root, "switch"))));
        }
        catch (IOException) { }
        catch (UnauthorizedAccessException) { }
    }
}
