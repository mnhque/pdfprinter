using System.Diagnostics;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;

namespace PDFPrinter.ConversionService;

/// <summary>
/// Converts an XPS spool file to PDF using Ghostscript's built-in XPS
/// interpreter (gxps) feeding the pdfwrite device. This preserves vector
/// paths, embedded fonts, and images from the XPS package rather than
/// rasterizing pages to bitmaps, which is what keeps text selectable/
/// searchable in the resulting PDF and keeps output size reasonable for
/// large/multi-page documents.
///
/// Ghostscript AGPL redistributable must be staged at GhostscriptExePath by
/// the installer (see Scripts/package-ghostscript.ps1). See README.md section
/// 3 for the licensing note (AGPL vs. commercial Artifex license).
/// </summary>
public sealed class GhostscriptConverter
{
    private readonly ILogger<GhostscriptConverter> _logger;
    private readonly PdfPrinterOptions _options;

    public GhostscriptConverter(ILogger<GhostscriptConverter> logger, IOptions<PdfPrinterOptions> options)
    {
        _logger = logger;
        _options = options.Value;
    }

    public async Task<bool> ConvertXpsToPdfAsync(string xpsPath, string pdfPath, CancellationToken token)
    {
        string gsPath = Environment.ExpandEnvironmentVariables(_options.GhostscriptExePath);
        if (!File.Exists(gsPath))
        {
            _logger.LogError("Ghostscript executable not found at {Path}. " +
                "Was the redistributable staged during install? See Scripts/package-ghostscript.ps1.", gsPath);
            return false;
        }

        // -dSAFER: sandboxes Ghostscript's PostScript-level operators (defense
        //          in depth against a maliciously crafted spool input).
        // -dNOPAUSE -dBATCH: non-interactive single-shot conversion.
        // -sDEVICE=pdfwrite: vector PDF output, not a rasterized image device.
        // -dCompatibilityLevel: PDF spec version to target (configurable).
        // -dEmbedAllFonts / -dSubsetFonts: keep the embedded fonts XPS already
        //          carries rather than re-flattening.
        var args = new List<string>
        {
            "-dSAFER",
            "-dNOPAUSE",
            "-dBATCH",
            "-dQUIET",
            "-sDEVICE=pdfwrite",
            $"-dCompatibilityLevel={_options.PdfCompatibilityLevel}",
            "-dEmbedAllFonts=true",
            "-dSubsetFonts=true",
            "-dAutoRotatePages=/None",
            $"-sOutputFile={QuoteIfNeeded(pdfPath)}",
            QuoteIfNeeded(xpsPath),
        };

        var psi = new ProcessStartInfo
        {
            FileName = gsPath,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
        };
        foreach (var a in args) psi.ArgumentList.Add(a.Trim('"'));

        using var process = new Process { StartInfo = psi };
        var stdout = new System.Text.StringBuilder();
        var stderr = new System.Text.StringBuilder();
        process.OutputDataReceived += (_, e) => { if (e.Data != null) stdout.AppendLine(e.Data); };
        process.ErrorDataReceived += (_, e) => { if (e.Data != null) stderr.AppendLine(e.Data); };

        process.Start();
        process.BeginOutputReadLine();
        process.BeginErrorReadLine();

        try
        {
            await process.WaitForExitAsync(token);
        }
        catch (OperationCanceledException)
        {
            TryKill(process);
            throw;
        }

        if (process.ExitCode != 0 || !File.Exists(pdfPath))
        {
            _logger.LogError("Ghostscript exited with code {Code}. stderr: {Err}",
                process.ExitCode, stderr.ToString());
            return false;
        }

        return true;
    }

    private static string QuoteIfNeeded(string path) =>
        path.Contains(' ') ? $"\"{path}\"" : path;

    private static void TryKill(Process p)
    {
        try { if (!p.HasExited) p.Kill(entireProcessTree: true); }
        catch { /* best effort */ }
    }
}
