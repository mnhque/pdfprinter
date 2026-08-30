using System.Collections.Concurrent;
using System.IO.Pipes;
using System.Text;
using System.Text.Json;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;

namespace PDFPrinter.ConversionService;

public sealed class PdfPrinterOptions
{
    public string SpoolDirectory { get; set; } = @"%ProgramData%\PDFPrinter\Spool";
    public string GhostscriptExePath { get; set; } = @"%ProgramFiles%\PDFPrinter\redist\gs\gswin64c.exe";
    public string PdfCompatibilityLevel { get; set; } = "1.7";
    public string DefaultOutputFolder { get; set; } = @"%USERPROFILE%\Documents\PDF Printer";
    public bool AlwaysShowSaveDialog { get; set; } = true;
    public bool OverwritePrompt { get; set; } = true;
    public int MaxConcurrentConversions { get; set; } = 4;
    public int JobTimeoutSeconds { get; set; } = 120;
}

public sealed record JobManifest(
    string JobId,
    int SpoolerJobId,
    string PrinterName,
    string PortName,
    string UserSid,
    string XpsFile,
    int PageCountHint);

/// <summary>
/// Hosted service that discovers finished print jobs and drives them through
/// conversion + save. Two independent discovery paths, so a missed signal on
/// one never loses a job:
///   1. A named-pipe server the port monitor writes a job id to as soon as
///      EndDocPort fires (low latency).
///   2. A FileSystemWatcher + periodic directory sweep on the spool folder
///      (catches anything if the pipe wasn't connected, e.g. service was
///      mid-restart, and cleans up orphaned jobs after a crash).
/// </summary>
public sealed class JobWatcher : BackgroundService
{
    private readonly ILogger<JobWatcher> _logger;
    private readonly PdfPrinterOptions _options;
    private readonly GhostscriptConverter _converter;
    private readonly SaveDialogClient _saveDialog;
    private readonly ConcurrentDictionary<string, byte> _inFlight = new();
    private readonly SemaphoreSlim _concurrencyGate;

    public JobWatcher(
        ILogger<JobWatcher> logger,
        IOptions<PdfPrinterOptions> options,
        GhostscriptConverter converter,
        SaveDialogClient saveDialog)
    {
        _logger = logger;
        _options = options.Value;
        _converter = converter;
        _saveDialog = saveDialog;
        _concurrencyGate = new SemaphoreSlim(Math.Max(1, _options.MaxConcurrentConversions));
    }

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        string spoolDir = Environment.ExpandEnvironmentVariables(_options.SpoolDirectory);
        Directory.CreateDirectory(spoolDir);

        _logger.LogInformation("PDF Printer Conversion Service started. Watching {SpoolDir}", spoolDir);

        var pipeTask = RunPipeServerAsync(spoolDir, stoppingToken);
        var sweepTask = RunPeriodicSweepAsync(spoolDir, stoppingToken);

        await Task.WhenAll(pipeTask, sweepTask);
    }

    /// <summary>
    /// Named-pipe server: the port monitor connects, writes "<jobId>\n", and
    /// disconnects. We loop, always keeping one server instance listening.
    /// </summary>
    private async Task RunPipeServerAsync(string spoolDir, CancellationToken token)
    {
        while (!token.IsCancellationRequested)
        {
            try
            {
                await using var server = new NamedPipeServerStream(
                    "PDFPrinterJobReady", PipeDirection.In, 1,
                    PipeTransmissionMode.Byte, PipeOptions.Asynchronous);

                await server.WaitForConnectionAsync(token);

                using var reader = new StreamReader(server, Encoding.Unicode, leaveOpen: true);
                string? jobId = await reader.ReadLineAsync(token);
                if (!string.IsNullOrWhiteSpace(jobId))
                {
                    _ = ProcessJobAsync(spoolDir, jobId.Trim(), token);
                }
            }
            catch (OperationCanceledException)
            {
                break;
            }
            catch (Exception ex)
            {
                _logger.LogWarning(ex, "Pipe server iteration failed; retrying.");
                await Task.Delay(TimeSpan.FromSeconds(1), token).ContinueWith(_ => { });
            }
        }
    }

    /// <summary>
    /// Fallback sweep: every 3 seconds, look for *.json manifests in the
    /// spool directory that don't yet have a matching *.pdf and aren't
    /// already being processed.
    /// </summary>
    private async Task RunPeriodicSweepAsync(string spoolDir, CancellationToken token)
    {
        while (!token.IsCancellationRequested)
        {
            try
            {
                foreach (var manifestPath in Directory.EnumerateFiles(spoolDir, "*.json"))
                {
                    string jobId = Path.GetFileNameWithoutExtension(manifestPath);
                    string pdfMarker = Path.Combine(spoolDir, jobId + ".done");
                    if (!File.Exists(pdfMarker) && !_inFlight.ContainsKey(jobId))
                    {
                        _ = ProcessJobAsync(spoolDir, jobId, token);
                    }
                }
            }
            catch (Exception ex)
            {
                _logger.LogWarning(ex, "Spool directory sweep failed.");
            }

            await Task.Delay(TimeSpan.FromSeconds(3), token).ContinueWith(_ => { });
        }
    }

    private async Task ProcessJobAsync(string spoolDir, string jobId, CancellationToken token)
    {
        if (!_inFlight.TryAdd(jobId, 0))
            return; // already being processed by the other discovery path

        await _concurrencyGate.WaitAsync(token);
        try
        {
            string manifestPath = Path.Combine(spoolDir, jobId + ".json");
            if (!File.Exists(manifestPath))
            {
                _logger.LogWarning("Job {JobId} signaled but manifest missing; skipping.", jobId);
                return;
            }

            JobManifest? manifest = JsonSerializer.Deserialize<JobManifest>(
                await File.ReadAllTextAsync(manifestPath, token),
                new JsonSerializerOptions { PropertyNameCaseInsensitive = true });

            if (manifest is null || !File.Exists(manifest.XpsFile))
            {
                _logger.LogError("Job {JobId} manifest invalid or XPS file missing.", jobId);
                return;
            }

            using var cts = CancellationTokenSource.CreateLinkedTokenSource(token);
            cts.CancelAfter(TimeSpan.FromSeconds(_options.JobTimeoutSeconds));

            _logger.LogInformation("Converting job {JobId} from printer {Printer}...",
                jobId, manifest.PrinterName);

            string tempPdf = Path.Combine(spoolDir, jobId + ".pdf");
            bool converted = await _converter.ConvertXpsToPdfAsync(
                manifest.XpsFile, tempPdf, cts.Token);

            if (!converted)
            {
                _logger.LogError("Ghostscript conversion failed for job {JobId}.", jobId);
                await _saveDialog.NotifyFailureAsync(manifest, "PDF generation failed. See PDF Printer logs for details.");
                return;
            }

            string? finalPath = await _saveDialog.PromptAndSaveAsync(manifest, tempPdf, _options, token);
            if (finalPath is null)
            {
                _logger.LogInformation("Job {JobId} cancelled by user at save step.", jobId);
            }
            else
            {
                _logger.LogInformation("Job {JobId} saved to {Path}.", jobId, finalPath);
            }

            File.Create(Path.Combine(spoolDir, jobId + ".done")).Dispose();
            TryCleanupJobFiles(spoolDir, jobId);
        }
        catch (OperationCanceledException)
        {
            _logger.LogError("Job {JobId} timed out during conversion/save.", jobId);
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Unhandled error processing job {JobId}.", jobId);
        }
        finally
        {
            _concurrencyGate.Release();
            _inFlight.TryRemove(jobId, out _);
        }
    }

    private void TryCleanupJobFiles(string spoolDir, string jobId)
    {
        foreach (var ext in new[] { ".xps", ".json", ".pdf", ".done" })
        {
            try { File.Delete(Path.Combine(spoolDir, jobId + ext)); }
            catch (IOException) { /* best-effort cleanup, non-fatal */ }
        }
    }
}
