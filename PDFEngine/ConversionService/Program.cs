using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging.EventLog;
using PDFPrinter.ConversionService;

// Runs as a real Windows Service ("PDFPrinterService"), installed by the MSI
// via `sc create` / ServiceInstaller in the WiX Product.wxs. Also runs fine
// as a plain console app for local debugging (`dotnet run`), which is how
// Tests/TESTING_CHECKLIST.md item "reinstallation" exercises it repeatedly
// without waiting on SCM restarts.

var builder = Host.CreateApplicationBuilder(args);

builder.Services.AddWindowsService(options =>
{
    options.ServiceName = "PDFPrinterService";
});

builder.Logging.AddEventLog(settings =>
{
    settings.SourceName = "PDF Printer";
});

builder.Services.Configure<PdfPrinterOptions>(
    builder.Configuration.GetSection("PdfPrinter"));

builder.Services.AddSingleton<GhostscriptConverter>();
builder.Services.AddSingleton<SaveDialogClient>();
builder.Services.AddHostedService<JobWatcher>();

var host = builder.Build();
host.Run();
