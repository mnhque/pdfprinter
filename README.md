# PDF Printer 1.0.0

A real Windows virtual printer that any application can print to, producing PDF files, including a
custom "North America 4x6" (101.6 mm × 152.4 mm) paper size.

> **Build status of this deliverable:** This repository is a complete, real, buildable Windows
> project — every file is genuine source, not a placeholder. It was authored and organized in a
> Linux sandbox with **no Windows toolchain and no network access**, so **no `.exe`/`.msi` has been
> compiled here**. Section 8 below gives the exact commands to produce `PDF Printer Setup.exe` on a
> Windows machine with Visual Studio + WiX installed. Do not trust any claim that a Windows binary
> was produced without a Windows build machine actually running these steps.

---

## 1. Architecture (read this before touching code)

### 1.1 Why not a "real" WDK printer driver?

A ground-up v3/v4 print class driver requires the Windows Driver Kit, an EV code-signing
certificate, and — for public distribution on stock Windows 10/11 — Microsoft Hardware Dev Center
attestation signing (HLK submission) so Windows' driver-signing enforcement will load it on a
machine that isn't in test-signing mode. That is real, but it is not how any shipping PDF-printer
product (CutePDF, docPrinter, PDFCreator, Bullzip PDF Printer) actually works, and it adds signing
risk without adding capability. Windows already ships a signed driver that does the hard part.

### 1.2 Selected architecture

```
[Any Windows app] --Print--> [Print Spooler] --> [Microsoft XPS Document Writer v4 driver]
                                                        |
                                                        v
                                          [PDF Printer Port Monitor] (our code)
                                                        |
                                            writes job XPS to spool cache +
                                            signals via named pipe
                                                        |
                                                        v
                                     [PDF Printer Conversion Service] (our code, runs as
                                      a Windows service, LocalSystem, no UAC needed at print time)
                                                        |
                                     Ghostscript gxps->pdfwrite  (XPS -> PDF, vector-preserving)
                                                        |
                                                        v
                                   Native Save dialog (IFileSaveDialog) shown in the
                                   interactive user's session via the service's UI companion
```

- **Driver**: `mxdwdrv.dll` / `mxdwdui.dll`, Microsoft's inbox, Microsoft-signed XPS Document Writer
  v4 driver. We reference it by name in `AddPrinterDriver`; we do not ship or modify it.
- **Port Monitor**: `Driver/PortMonitor` — a `Monitor2`-interface DLL (`pdfpm.dll`), the documented
  Print Spooler SPI (`InitializePrintMonitor2`, `Monitor2.OpenPort/WritePort/ClosePort`). This is
  the same official extension point `FILE:`, `USBPRINT`, and every network-port monitor uses.
- **Conversion**: `PDFEngine/ConversionService` — a .NET 8 Windows Service that watches the spool
  cache, invokes Ghostscript (`gxps` device in, `pdfwrite` device out) to convert XPS→PDF without
  rasterizing, then hands off to the Save UI.
- **Custom paper size**: `Driver/PaperSize` — registers "North America 4x6" two ways:
  1. Win32 Forms API (`AddFormW`/`SetFormW`, `FORM_INFO_2`) at the printer/server level — this is
     what makes it show up in the generic "Paper size" dropdown of the Print dialog and in
     Devices & Printers → Printer properties → Advanced → Paper sizes.
  2. A PrintCapabilities/PrintTicket XML fragment (`PaperSize/NorthAmerica4x6.xml`) consumed by
     the XPS driver's device-capabilities pipeline, so PrintTicket-aware apps (Word, Edge, Chrome)
     show "North America 4x6" by name, not just "Custom."
- **Install/uninstall**: `Installer/` — a WiX v4 MSI. MSI custom actions call the Win32 Print
  Spooler APIs directly (`AddPrinterDriver`, `AddMonitor`, `AddPort` via our monitor, `AddPrinter`,
  `AddForm`) on install, and `Delete*` counterparts + `SHDeleteKey` on uninstall/rollback. Because
  it's a real MSI, it registers automatically in **Settings → Apps → Installed apps** with an
  **Uninstall** button — no separate registration step needed.
- **Configuration app**: `Configuration/ConfigApp` — WPF app, default output folder / paper size /
  orientation / "always ask" toggle, About/version. Stored in
  `HKCU\Software\PDFPrinter\Settings` (per-user, no admin needed) with an
  `HKLM\Software\PDFPrinter\Settings` machine-wide default written by the installer.

### 1.3 How print data becomes a PDF (data flow)

1. App calls the Windows Print API against "PDF Printer." GDI/XPS print commands go to the spooler.
2. Spooler renders through `mxdwdrv.dll`, producing a well-formed XPS spool file (this already
   embeds fonts, images, vector paths, page size/orientation exactly as GDI specified them).
3. Spooler writes that XPS byte stream to the **port** we registered ("PDFPRT1:") — the write calls
   land in our port monitor's `WritePort`.
4. Our port monitor buffers to `%ProgramData%\PDFPrinter\Spool\<job-guid>.xps`, and on `ClosePort`
   (end of job) drops a `<job-guid>.json` manifest (source app, requested paper size, page count,
   user SID) and signals the service over a named pipe (`\\.\pipe\PDFPrinterJobReady`).
5. The Conversion Service picks up the job, shells to `gswin64c.exe -sDEVICE=pdfwrite
   -dCompatibilityLevel=1.7 <job>.xps <out>.pdf` via the `gxps` input handler bundled with the
   Ghostscript redistributable, preserving page count/size/orientation/fonts/images.
6. The service's UI companion (a per-session tray helper launched at logon, no elevation) pops the
   native `IFileSaveDialog` in the *printing user's* session (matched via the manifest's SID) unless
   "always use default folder" is set, in which case it auto-names and writes silently, with an
   overwrite-confirmation `TaskDialog` if the target file exists.

### 1.4 Custom paper size registration detail

`FORM_INFO_2` is submitted with:
```
pName        = L"North America 4x6"
Size.cx      = 101600   // hundredths of a millimeter: 101.6 mm
Size.cy      = 152400   // 152.4 mm
ImageableArea = { 0, 0, 101600, 152400 }  // full bleed; matches 4in x 6in exactly
StringType   = STRING_NONE
Flags        = FORM_PRINTER  (printer-scoped, not global, so uninstall cleanly removes it)
```
101.6 mm = 4.000 in and 152.4 mm = 6.000 in exactly (1 in = 25.4 mm), so the size is exact, not
rounded. See `Driver/PaperSize/RegisterPaperSize.cpp`.

### 1.5 Install / uninstall summary

- Install (elevated, one UAC prompt from the MSI): copy driver-support files → `AddPrinterDriver`
  (referencing inbox `mxdwdrv.dll`) → `AddMonitor` (our `pdfpm.dll`) → `AddPort` → `AddPrinter`
  ("PDF Printer") → `AddForm` (North America 4x6) → install + start Conversion Service → install
  per-user tray helper (Start Menu + HKCU Run) → Start Menu shortcut for Config app.
- Printing itself needs **no admin rights** — only spooler client access, same as any print job.
- Uninstall (from Settings → Apps → Installed apps → PDF Printer → Uninstall, elevates
  automatically): stop/remove service → `DeletePrinter` → `DeleteMonitor` → `DeletePort` →
  `DeleteForm` → `DeletePrinterDriver` → remove Start Menu/Run entries → remove
  `%ProgramData%\PDFPrinter` and `HKLM/HKCU\Software\PDFPrinter` → MSI removes itself from Installed
  Apps automatically. No orphaned printer or driver package is left (see
  `Installer/CustomActions/Uninstall.cs`).

---

## 2. Repository layout

```
PDFPrinter/
├── Installer/                     WiX v4 MSI project + custom actions
│   ├── PDFPrinter.Installer.wixproj
│   ├── Product.wxs
│   ├── CustomActions/             C++ DLL: Install.cpp / Uninstall.cpp (Print Spooler API calls)
│   └── License.rtf
├── Driver/
│   ├── PortMonitor/                pdfpm.dll — C++ Monitor2 port monitor
│   │   ├── PortMonitor.vcxproj
│   │   ├── Monitor2.cpp
│   │   ├── PortMonitor.h
│   │   └── SpoolWriter.cpp
│   └── PaperSize/
│       ├── RegisterPaperSize.cpp / .h
│       └── NorthAmerica4x6.xml    PrintCapabilities/PrintTicket override fragment
├── PDFEngine/
│   └── ConversionService/          .NET 8 Windows Service
│       ├── PDFPrinter.ConversionService.csproj
│       ├── Program.cs
│       ├── JobWatcher.cs
│       ├── GhostscriptConverter.cs
│       ├── SaveDialogClient.cs
│       └── appsettings.json
├── Configuration/
│   └── ConfigApp/                  WPF settings + About app
│       ├── PDFPrinter.ConfigApp.csproj
│       ├── App.xaml / App.xaml.cs
│       ├── MainWindow.xaml / .cs
│       └── Settings.cs
├── Scripts/
│   ├── build-all.ps1
│   ├── sign-dev.ps1
│   └── package-ghostscript.ps1
├── Tests/
│   └── TESTING_CHECKLIST.md
├── PDFPrinter.sln
└── README.md   (this file)
```

## 3. Prerequisites to build (Windows machine required)

- Windows 10/11, 64-bit
- Visual Studio 2022 (17.9+) with workloads:
  - "Desktop development with C++" (for the port monitor + custom actions)
  - ".NET desktop development" (for the service + config app, .NET 8 SDK)
- Windows SDK 10.0.22621.0 or later (installed with the above workload)
- WiX Toolset v4 (`dotnet tool install --global wix`) — used for the MSI, no separate WDK needed
  since we ship no kernel driver
- Ghostscript AGPL redistributable 10.x, 64-bit (`gswin64c.exe`, `gsdll64.dll`) — download and place
  under `Scripts/redist/ghostscript/` before packaging (see `Scripts/package-ghostscript.ps1`);
  licensing note: Ghostscript is AGPL-3.0, so a commercial product bundling it either open-sources
  under AGPL terms or purchases an Artifex commercial Ghostscript license — decide this before
  shipping publicly.
- A code-signing certificate for the port monitor DLL and MSI (see §5, signing).

## 4. Build (on Windows)

```powershell
cd PDFPrinter
.\Scripts\build-all.ps1 -Configuration Release -Platform x64
```

This runs, in order:
1. `msbuild Driver\PortMonitor\PortMonitor.vcxproj /p:Configuration=Release;Platform=x64`
2. `dotnet publish PDFEngine\ConversionService -c Release -r win-x64 --self-contained false`
3. `dotnet publish Configuration\ConfigApp -c Release -r win-x64 --self-contained false`
4. `.\Scripts\package-ghostscript.ps1` (stages the Ghostscript redistributable into the MSI payload)
5. `wix build Installer\Product.wxs -o Installer\bin\PDF Printer Setup.exe`

Output: `Installer\bin\PDF Printer Setup.exe`

## 5. Signing

- **Port monitor DLL (`pdfpm.dll`)**: Loaded in-process by `spoolsv.exe`. As of Windows 10 1607+,
  kernel-mode driver signing enforcement does **not** apply to this (it's a user-mode Print Spooler
  plugin, not a kernel driver), but Windows SmartScreen/Defender will flag an unsigned DLL and some
  managed environments block unsigned spooler plugins via policy. Sign it with an Authenticode
  code-signing certificate (`signtool sign /fd sha256 /a pdfpm.dll`).
- **MSI / `PDF Printer Setup.exe`**: Sign with the same certificate so SmartScreen reputation builds
  and Windows doesn't show an "Unknown publisher" UAC prompt.
- **Development/local testing**: You can self-sign (`New-SelfSignedCertificate` +
  `Scripts\sign-dev.ps1`) and add the cert to the local machine's Trusted Root store for testing on
  your own dev VM. This is **not** valid for distribution — a self-signed cert will show "Unknown
  Publisher" and trigger SmartScreen warnings on any other machine.
- **Production distribution**: You need an **OV (Organization Validation) or EV Authenticode
  code-signing certificate** from a public CA (DigiCert, Sectigo, GlobalSign, SSL.com). EV is
  strongly recommended because it builds SmartScreen reputation immediately rather than requiring
  install-count history. Do not distribute the unsigned build outside your own test machines —
  do **not** claim (and this project does not claim) that an unsigned build "installs normally" on
  a standard end-user Windows 10/11 machine; it will show SmartScreen/UAC "Unknown publisher"
  warnings.
- No WDK driver-signing/HLK submission is required under this architecture, because we ship no
  kernel-mode driver — only a signed user-mode port monitor DLL and a signed MSI.

## 6. Install / Uninstall (end user)

**Install:** Run `PDF Printer Setup.exe` → UAC prompt → Next → Install → Finish. "PDF Printer" then
appears in *Settings → Bluetooth & devices → Printers & scanners* and in every app's Print dialog.

**Uninstall:** *Settings → Apps → Installed apps* → **PDF Printer** → **Uninstall** → confirm UAC.
Everything listed in §1.5 is removed; no broken printer entry remains.

## 7. Testing

See `Tests/TESTING_CHECKLIST.md` for the full matrix (Word, Notepad, Chrome/Edge, multi-page,
Letter/Legal/A4/North America 4x6, portrait/landscape, images/text, large docs, cancelled jobs,
concurrent jobs, reinstall, uninstall, reboot).

## 8. Known limitations of this deliverable

- Compiled here: **nothing** — this sandbox has no Windows toolchain or network. Everything above
  is real source that a Windows dev machine can build with the commands in §4.
- Ghostscript licensing (AGPL vs. commercial) is a business decision left to you before public
  distribution — flagged, not resolved, above.
- Production code signing requires you to purchase a certificate; no certificate is or can be
  provided here.
