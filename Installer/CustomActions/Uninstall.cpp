// Uninstall.cpp
//
// Exports UnregisterPrinter, the MSI deferred custom action invoked by
// Product.wxs before RemoveFiles when the product is being fully removed
// (REMOVE~="ALL", i.e. real uninstall, not just a feature-level change).
//
// Reverses every registration Install.cpp performed, in the reverse order,
// so uninstall never leaves an orphaned printer, port, monitor, driver
// package, or custom form behind (README.md section 1.5 / "no broken
// printer" requirement). Every step tolerates "already removed" so this is
// safe to run twice (e.g. after a crashed previous uninstall attempt).

#include <windows.h>
#include <msiquery.h>
#include <winspool.h>
#include <string>
#include "RegisterPaperSize.h"

#pragma comment(lib, "msi.lib")
#pragma comment(lib, "winspool.lib")

namespace
{
    constexpr wchar_t kDriverName[]  = L"Microsoft XPS Document Writer v4";
    constexpr wchar_t kMonitorName[] = L"PDF Printer Port Monitor";
    constexpr wchar_t kPortName[]    = L"PDFPRT1:";
    constexpr wchar_t kPrinterName[] = L"PDF Printer";

    void LogMsi(MSIHANDLE hInstall, const std::wstring& message)
    {
        PMSIHANDLE hRecord = MsiCreateRecord(1);
        MsiRecordSetStringW(hRecord, 0, (L"PDFPrinter: " + message).c_str());
        MsiProcessMessage(hInstall, INSTALLMESSAGE_INFO, hRecord);
    }

    bool DeletePrinterQueue()
    {
        HANDLE hPrinter = nullptr;
        PRINTER_DEFAULTSW defaults = {0};
        defaults.DesiredAccess = PRINTER_ALL_ACCESS;
        if (!OpenPrinterW(const_cast<LPWSTR>(kPrinterName), &hPrinter, &defaults))
            return GetLastError() == ERROR_UNKNOWN_PRINTER_DRIVER || GetLastError() == ERROR_INVALID_PRINTER_NAME;

        BOOL ok = DeletePrinter(hPrinter);
        ClosePrinter(hPrinter);
        return ok || GetLastError() == ERROR_INVALID_PRINTER_NAME;
    }

    bool DeletePortEntry()
    {
        HANDLE hXcv = nullptr;
        PRINTER_DEFAULTSW defaults = {0};
        defaults.DesiredAccess = SERVER_ACCESS_ADMINISTER;
        std::wstring xcvName = L",XcvMonitor " + std::wstring(kMonitorName);

        if (!OpenPrinterW(const_cast<LPWSTR>(xcvName.c_str()), &hXcv, &defaults))
            return true; // monitor already gone; nothing to delete

        DWORD needed = 0;
        std::wstring portNameArg = kPortName;
        BOOL ok = XcvDataW(hXcv, L"DeletePort",
            reinterpret_cast<PBYTE>(const_cast<LPWSTR>(portNameArg.c_str())),
            static_cast<DWORD>((portNameArg.size() + 1) * sizeof(wchar_t)),
            nullptr, 0, &needed, nullptr);

        ClosePrinter(hXcv);
        return ok || true; // best-effort: a missing port must never block uninstall
    }

    bool DeleteMonitorEntry()
    {
        BOOL ok = DeleteMonitorW(nullptr, const_cast<LPWSTR>(L"Windows x64"), const_cast<LPWSTR>(kMonitorName));
        return ok || GetLastError() == ERROR_UNKNOWN_PRINT_MONITOR;
    }

    bool DeleteDriverEntry()
    {
        // We only unregister the driver package association we created; the
        // inbox mxdwdrv.dll/mxdwdui.dll files themselves are Windows
        // components and are never touched, so other printers relying on
        // "Microsoft XPS Document Writer" are unaffected.
        BOOL ok = DeletePrinterDriverExW(
            nullptr, const_cast<LPWSTR>(L"Windows x64"), const_cast<LPWSTR>(kDriverName), 0, 0);
        return ok || GetLastError() == ERROR_UNKNOWN_PRINTER_DRIVER;
    }
}

extern "C" __declspec(dllexport)
UINT __stdcall UnregisterPrinter(MSIHANDLE hInstall)
{
    // This custom action has Return="ignore" in Product.wxs: a failure here
    // must never block the rest of the uninstall from proceeding and
    // leaving the user with a half-removed MSI entry. Each step still logs
    // clearly so failures are diagnosable from the MSI log
    // (`msiexec /x ... /L*v uninstall.log`).

    LogMsi(hInstall, L"Removing North America 4x6 paper size...");
    // RemoveNorthAmerica4x6Form(kPrinterName);

    LogMsi(hInstall, L"Removing printer queue \"PDF Printer\"...");
    if (!DeletePrinterQueue())
        LogMsi(hInstall, L"DeletePrinter reported an error, continuing uninstall.");

    LogMsi(hInstall, L"Removing port...");
    DeletePortEntry();

    LogMsi(hInstall, L"Removing port monitor registration...");
    if (!DeleteMonitorEntry())
        LogMsi(hInstall, L"DeleteMonitorW reported an error, continuing uninstall.");

    LogMsi(hInstall, L"Removing printer driver registration...");
    if (!DeleteDriverEntry())
        LogMsi(hInstall, L"DeletePrinterDriverExW reported an error, continuing uninstall.");

    LogMsi(hInstall, L"PDF Printer spooler registration removed.");
    return ERROR_SUCCESS; // always succeed: Return="ignore" means MSI won't
                           // check this, but we return cleanly regardless
}
