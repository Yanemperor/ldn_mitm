using RyuLink.Installer.Core;
using System.Security.Cryptography;

var root = Path.Combine(Path.GetTempPath(), $"ryulink-installer-{Guid.NewGuid()}");
try
{
    var payload = Path.Combine(root, "payload");
    var sd = Path.Combine(root, "sd");
    var backups = Path.Combine(root, "backups");
    Directory.CreateDirectory(Path.Combine(sd, "switch"));
    Directory.CreateDirectory(Path.Combine(sd, "switch", ".overlays"));
    Directory.CreateDirectory(Path.Combine(sd, "atmosphere", "contents", "4200000000000010"));
    Directory.CreateDirectory(Path.Combine(sd, "atmosphere", "contents.disabled", "420000000000000B"));
    Directory.CreateDirectory(Path.Combine(sd, "config", "ldn_mitm"));
    await File.WriteAllTextAsync(Path.Combine(sd, "config", "ldn_mitm", "relay.cfg"), "old-config");
    await File.WriteAllTextAsync(Path.Combine(sd, "switch", ".overlays", "ldnmitm_config.ovl"), "old-overlay");
    await File.WriteAllTextAsync(Path.Combine(sd, "atmosphere", "contents.disabled", "420000000000000B", "exefs.nsp"), "old-core");
    foreach (var relative in InstallationService.RequiredFiles)
    {
        var file = Path.Combine(payload, relative);
        Directory.CreateDirectory(Path.GetDirectoryName(file)!);
        await File.WriteAllTextAsync(file, relative == "config/ldn_mitm/relay.cfg" ? "enabled=1\nbroadcast=1\nselected=RyuLink\nRyuLink relay.example:11451\n" : relative.EndsWith("boot2.flag") ? "" : relative);
    }
    var result = await new InstallationService().InstallToRootAsync(payload, sd, backups);
    if (result.Files.Count != 5 || !File.Exists(Path.Combine(result.BackupDirectory, "config", "ldn_mitm", "relay.cfg")) || !File.Exists(Path.Combine(result.BackupDirectory, "switch", ".overlays", "ldnmitm_config.ovl")) || !File.Exists(Path.Combine(result.BackupDirectory, "atmosphere", "contents.disabled", "420000000000000B", "exefs.nsp"))) throw new Exception("backup or required file validation failed");
    foreach (var file in result.Files)
    {
        var source = SHA256.HashData(await File.ReadAllBytesAsync(Path.Combine(payload, file.RelativePath)));
        var target = SHA256.HashData(await File.ReadAllBytesAsync(Path.Combine(sd, file.RelativePath)));
        if (!source.SequenceEqual(target)) throw new Exception($"hash mismatch: {file.RelativePath}");
    }
    Console.WriteLine("Installer core test passed: backup, exact five-file copy, and SHA-256 verification.");
}
finally { if (Directory.Exists(root)) Directory.Delete(root, true); }
