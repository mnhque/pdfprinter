# PDF Printer — Testing Checklist

Run on a clean Windows 10 64-bit VM and a clean Windows 11 64-bit VM (snapshot
before install so "reinstallation" and "uninstallation" cases can be re-run
from a known state).

## Install
- [ ] `PDF Printer Setup.exe` runs, shows one UAC prompt, completes without error
- [ ] "PDF Printer" appears in **Settings → Bluetooth & devices → Printers & scanners**
- [ ] "PDF Printer" appears in every app's Print dialog (Print Provider list)
- [ ] `PDFPrinterService` shows as **Running** in `services.msc`
- [ ] Start Menu → "PDF Printer" folder has "PDF Printer Settings" and "Uninstall PDF Printer"

## Paper sizes
- [ ] Printer Properties → Advanced → Paper sizes lists Letter, Legal, A4, A3, A5,
      Executive, Tabloid, Ledger, B4, B5, common envelope sizes, **and North America 4x6**
- [ ] North America 4x6 reports exactly 101.6 mm × 152.4 mm (Properties → Advanced → Forms,
      or `RegisterPaperSize::VerifyNorthAmerica4x6Form`)
- [ ] North America 4x6 is selectable from a PrintTicket-aware app's paper size dropdown
      (Word, Edge) by its display name, not just "Custom"

## Print from real applications
- [ ] **Microsoft Word**: File → Print → PDF Printer → produces a valid, openable PDF
- [ ] **Notepad**: prints a plain-text file correctly, page breaks preserved
- [ ] **Chrome**: Ctrl+P → PDF Printer, multi-page web page prints completely
- [ ] **Edge**: same as Chrome
- [ ] Multi-page Word document (10+ pages): all pages present, in order, in the output PDF
- [ ] Letter, Legal, A4 each produce correctly-sized PDF pages (verify via PDF properties)
- [ ] North America 4x6 produces a PDF page sized exactly 4in × 6in
- [ ] Portrait orientation preserved end-to-end
- [ ] Landscape orientation preserved end-to-end
- [ ] Document containing embedded images: images present, not corrupted, reasonable quality
- [ ] Document containing only text: text is selectable/searchable in the output PDF (i.e.
      Ghostscript's gxps→pdfwrite path did not rasterize)
- [ ] Large document (100+ pages or large embedded images): completes without timeout,
      output file size reasonable, no OOM in `PDFPrinterService`

## Save behavior
- [ ] "Always show Save As dialog" enabled → native Save dialog appears in the
      correct user session, in the correct default folder
- [ ] Save dialog cancelled → job is discarded cleanly, no orphaned temp files remain
      in `%ProgramData%\PDFPrinter\Spool`
- [ ] "Always show Save As dialog" disabled → file is auto-named and saved silently to the
      configured default folder
- [ ] Saving to a filename that already exists triggers the configured overwrite behavior
      (confirmation prompt, or auto-numbered filename per settings)

## Concurrency / reliability
- [ ] Two print jobs submitted back-to-back from different apps both complete correctly,
      output files are not mixed up or corrupted
- [ ] A print job is cancelled mid-print (from the print queue) — spooler/service recover
      cleanly, no stuck job, no crash
- [ ] Two different Windows user sessions (if testing on a multi-user/RDS box) each get their
      Save dialog routed to their own session, not to another user's

## Reinstall / uninstall
- [ ] Uninstall via **Settings → Apps → Installed apps → PDF Printer → Uninstall** completes
- [ ] After uninstall: "PDF Printer" is gone from Printers & scanners
- [ ] After uninstall: `PDFPrinterService` is gone from `services.msc`
- [ ] After uninstall: North America 4x6 form is gone (check another printer's paper size
      list doesn't show a leftover global form)
- [ ] After uninstall: `%ProgramFiles%\PDFPrinter` and `%ProgramData%\PDFPrinter` are removed
- [ ] After uninstall: no entry remains in Settings → Apps → Installed apps
- [ ] Re-running `PDF Printer Setup.exe` after uninstall reinstalls cleanly (no
      "already exists" errors, no duplicate printer icons)
- [ ] Reinstalling **without** uninstalling first (repair/upgrade path) does not create a
      second "PDF Printer" queue or duplicate port

## Reboot resilience
- [ ] After a full Windows restart, "PDF Printer" is still present and prints successfully
      without any manual re-registration
- [ ] `PDFPrinterService` is set to Automatic start and is running after reboot without
      user login (verify by printing immediately post-boot before logging in interactively,
      via a scheduled test job or RDP from another machine)

## Error handling
- [ ] Printing while the Conversion Service is stopped: job stays queued/fails gracefully
      with a clear spooler error, no crash, and recovers once the service is restarted
- [ ] Invalid/reserved filename typed into Save dialog is rejected with a clear message
- [ ] Configured default output folder is deleted/unavailable: printing surfaces a clear
      error instead of silently failing
- [ ] Ghostscript executable missing/misconfigured: job fails with a logged, diagnosable
      error (Event Viewer → Application, source "PDF Printer") rather than hanging forever
- [ ] Print job with corrupt/unsupported print data does not crash `PDFPrinterService`
      or `spoolsv.exe`
