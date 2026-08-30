using Microsoft.Win32;

namespace PDFPrinter.ConfigApp;

public enum DefaultOrientation { Portrait, Landscape }

/// <summary>
/// Reads/writes user-configurable PDF Printer settings. Per-user values live
/// under HKCU (no admin rights needed to change your own defaults); the
/// installer seeds HKLM with the initial machine-wide defaults, which act as
/// the fallback when a per-user value hasn't been set yet.
/// </summary>
public sealed class Settings
{
    private const string HkcuKeyPath = @"Software\PDFPrinter\Settings";
    private const string HklmKeyPath = @"Software\PDFPrinter\Settings";

    public string DefaultOutputFolder { get; set; } =
        Environment.ExpandEnvironmentVariables(@"%USERPROFILE%\Documents\PDF Printer");

    public string DefaultPaperSize { get; set; } = "Letter"; // or "North America 4x6", "A4", etc.

    public DefaultOrientation DefaultOrientation { get; set; } = DefaultOrientation.Portrait;

    public bool AlwaysShowSaveDialog { get; set; } = true;

    public bool AutoNumberFilenames { get; set; } = true;

    public static Settings Load()
    {
        var settings = new Settings();

        // Start from machine-wide defaults (written by the installer), then
        // let any per-user overrides win.
        ApplyFrom(settings, Registry.LocalMachine, HklmKeyPath);
        ApplyFrom(settings, Registry.CurrentUser, HkcuKeyPath);

        return settings;
    }

    public void Save()
    {
        using var key = Registry.CurrentUser.CreateSubKey(HkcuKeyPath, writable: true);
        key.SetValue("DefaultOutputFolder", DefaultOutputFolder, RegistryValueKind.String);
        key.SetValue("DefaultPaperSize", DefaultPaperSize, RegistryValueKind.String);
        key.SetValue("DefaultOrientation", DefaultOrientation.ToString(), RegistryValueKind.String);
        key.SetValue("AlwaysShowSaveDialog", AlwaysShowSaveDialog ? 1 : 0, RegistryValueKind.DWord);
        key.SetValue("AutoNumberFilenames", AutoNumberFilenames ? 1 : 0, RegistryValueKind.DWord);
    }

    private static void ApplyFrom(Settings settings, RegistryKey hive, string path)
    {
        using var key = hive.OpenSubKey(path);
        if (key is null) return;

        if (key.GetValue("DefaultOutputFolder") is string folder && !string.IsNullOrWhiteSpace(folder))
            settings.DefaultOutputFolder = Environment.ExpandEnvironmentVariables(folder);

        if (key.GetValue("DefaultPaperSize") is string paper && !string.IsNullOrWhiteSpace(paper))
            settings.DefaultPaperSize = paper;

        if (key.GetValue("DefaultOrientation") is string orientation &&
            Enum.TryParse<DefaultOrientation>(orientation, out var parsed))
            settings.DefaultOrientation = parsed;

        if (key.GetValue("AlwaysShowSaveDialog") is int showDialog)
            settings.AlwaysShowSaveDialog = showDialog != 0;

        if (key.GetValue("AutoNumberFilenames") is int autoNumber)
            settings.AutoNumberFilenames = autoNumber != 0;
    }
}
