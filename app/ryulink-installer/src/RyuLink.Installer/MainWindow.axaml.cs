using Avalonia.Controls;
using Avalonia.Interactivity;
using RyuLink.Installer.Core;

namespace RyuLink.Installer;

public partial class MainWindow : Window
{
    private readonly SdCardScanner _scanner = new(new SystemDriveProvider());
    private readonly InstallationService _installer = new();
    public MainWindow() { InitializeComponent(); ScanForSdCards(); }
    private void RescanButton_Click(object sender, RoutedEventArgs e) => ScanForSdCards();

    private async void InstallButton_Click(object sender, RoutedEventArgs e)
    {
        if (SdCardComboBox.SelectedItem is not SdCardCandidate target) { StatusTextBlock.Text = "请选择一张 Switch SD 卡。"; return; }
        var payload = Path.Combine(AppContext.BaseDirectory, "payload");
        var backupRoot = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "RyuLink Installer", "Backups");
        var progress = new Progress<InstallationProgress>(update => { StatusTextBlock.Text = update.Message; InstallProgressBar.Value = (int)update.Step / (double)InstallationStep.Completed; });
        InstallButton.IsEnabled = false;
        try
        {
            var result = await _installer.InstallAsync(payload, target, backupRoot, progress);
            StatusTextBlock.Text = string.IsNullOrEmpty(result.BackupDirectory) ? "安装并校验完成。请安全弹出 SD 卡，然后完整重启 CFW。" : $"安装并校验完成。备份：{result.BackupDirectory}\n请安全弹出 SD 卡，然后完整重启 CFW。";
        }
        catch (Exception exception) { StatusTextBlock.Text = $"安装失败：{exception.Message}"; }
        finally { InstallButton.IsEnabled = true; }
    }

    private void ScanForSdCards()
    {
        var candidates = _scanner.FindCandidates();
        SdCardComboBox.ItemsSource = candidates;
        SdCardComboBox.SelectedIndex = candidates.Count == 1 ? 0 : -1;
        InstallButton.IsEnabled = candidates.Count > 0;
        StatusTextBlock.Text = candidates.Count switch { 0 => "未找到同时包含 atmosphere 和 switch 目录的 SD 卡。", 1 => $"已检测到 Switch SD 卡：{candidates[0].RootPath}", _ => $"检测到 {candidates.Count} 张 Switch SD 卡，请选择要安装的目标。" };
    }
}
