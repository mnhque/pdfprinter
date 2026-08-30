using System.IO.Pipes;
using System.Text;
using System.Text.Json;
using Microsoft.Extensions.Logging;

namespace PDFPrinter.ConversionService;

/// <summary>
/// The Conversion Service runs as LocalSystem and, per Windows session
/// isolation, cannot itself pop UI into a user's interactive desktop.
/// Instead it talks to a small per-user tray helper (installed to
/// HKCU...\Run, no elevation, launched at logon in each session) over a
/// per-session named pipe. The helper shows the native IFileSaveDialog (or,
/// for "always use default folder", just returns the computed path) and
/// relays the chosen path back.
///
/// This is the standard, documented pattern for services that need
/// session-0-isolated interactive UI (the same approach Windows Update and
/// many antivirus products use for showing prompts from a service).
/// </summary>
public sealed class SaveDialogClient
{
    private readonly ILogger<SaveDialogClient> _logger;

    public SaveDialogClient(ILogger<SaveDialogClient> logger)
    {
        _logger = logger;
    }

    public async Task<string?> PromptAndSaveAsync(
        JobManifest manifest, string tempPdfPath, PdfPrinterOptions options, CancellationToken token)
    {
        string defaultFolder = Environment.ExpandEnvironmentVariables(options.DefaultOutputFolder);
        Directory.CreateDirectory(defaultFolder);

        string suggestedName = $"{manifest.PrinterName}_{DateTime.Now:yyyyMMdd_HHmmss}.pdf";
        string suggestedPath = Path.Combine(defaultFolder, SanitizeFileName(suggestedName));

        string? finalPath = suggestedPath;

        if (options.AlwaysShowSaveDialog)
        {
            finalPath = await AskTrayHelperForPathAsync(manifest.UserSid, suggestedPath, token);
            if (finalPath is null)
                return null; // user cancelled the save dialog
        }
        else if (options.OverwritePrompt && File.Exists(suggestedPath))
        {
            finalPath = MakeUniquePath(suggestedPath);
        }

        Directory.CreateDirectory(Path.GetDirectoryName(finalPath)!);
        File.Copy(tempPdfPath, finalPath, overwrite: true);
        return finalPath;
    }

    public async Task NotifyFailureAsync(JobManifest manifest, string message)
    {
        try
        {
            await SendToTrayHelperAsync(manifest.UserSid, new
            {
                Type = "Error",
                Message = message,
            }, expectResponse: false, CancellationToken.None);
        }
        catch (Exception ex)
        {
            _logger.LogWarning(ex, "Could not notify tray helper of failure for job {JobId}.", manifest.JobId);
        }
    }

    private async Task<string?> AskTrayHelperForPathAsync(string userSid, string suggestedPath, CancellationToken token)
    {
        try
        {
            var response = await SendToTrayHelperAsync(userSid, new
            {
                Type = "SaveAs",
                SuggestedPath = suggestedPath,
            }, expectResponse: true, token);

            if (response is null) return suggestedPath; // no interactive helper (e.g. RDP/service context) -> silent default
            using var doc = JsonDocument.Parse(response);
            if (doc.RootElement.TryGetProperty("Cancelled", out var cancelled) && cancelled.GetBoolean())
                return null;
            return doc.RootElement.GetProperty("Path").GetString();
        }
        catch (TimeoutException)
        {
            _logger.LogWarning("Tray helper for session did not respond in time; using default path silently.");
            return suggestedPath;
        }
    }

    private async Task<string?> SendToTrayHelperAsync(string userSid, object payload, bool expectResponse, CancellationToken token)
    {
        string pipeName = $"PDFPrinterTray_{userSid}";
        using var client = new NamedPipeClientStream(".", pipeName, PipeDirection.InOut, PipeOptions.Asynchronous);

        using var connectCts = CancellationTokenSource.CreateLinkedTokenSource(token);
        connectCts.CancelAfter(TimeSpan.FromSeconds(2));
        try
        {
            await client.ConnectAsync(connectCts.Token);
        }
        catch (OperationCanceledException)
        {
            return null; // no tray helper running in that session — caller falls back to silent default
        }

        string json = JsonSerializer.Serialize(payload);
        byte[] bytes = Encoding.UTF8.GetBytes(json + "\n");
        await client.WriteAsync(bytes, token);

        if (!expectResponse) return null;

        using var timeoutCts = CancellationTokenSource.CreateLinkedTokenSource(token);
        timeoutCts.CancelAfter(TimeSpan.FromMinutes(5)); // user may take time picking a folder
        using var reader = new StreamReader(client, Encoding.UTF8, leaveOpen: true);
        var line = await reader.ReadLineAsync(timeoutCts.Token);
        return line;
    }

    private static string SanitizeFileName(string name)
    {
        foreach (char c in Path.GetInvalidFileNameChars())
            name = name.Replace(c, '_');
        return name;
    }

    private static string MakeUniquePath(string path)
    {
        string dir = Path.GetDirectoryName(path)!;
        string baseName = Path.GetFileNameWithoutExtension(path);
        string ext = Path.GetExtension(path);
        int i = 1;
        string candidate = path;
        while (File.Exists(candidate))
        {
            candidate = Path.Combine(dir, $"{baseName} ({i}){ext}");
            i++;
        }
        return candidate;
    }
}
