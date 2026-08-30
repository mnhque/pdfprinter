// Install.cpp
//
// Exports RegisterPrinter, the MSI deferred custom action invoked by
// Product.wxs (Execute="deferred" Impersonate="no") after InstallFiles.
// Because Impersonate="no", this runs as LocalSystem with full rights,
// which is required for AddPrinterDriver/AddMonitor/AddPrinter/AddForm.
//
// This performs the real Win32 Print Spooler registration described in
// README.md section 1.5 — no manual post-install configuration is required
// from the end user.

#include <windows.h>
#include <msiquery.h>
#include <winspool.h>
#include <string>
#include <vector>
#include "RegisterPaperSize.h"

#pragma comment(lib, "msi.lib")
#pragma comment(lib, "winspool.lib")

namespace
{
    constexpr wchar_t kDriverName[]   = L"Microsoft XPS Document Writer v4"; // Microsoft-signed, inbox
    constexpr wchar_t kMonitorName[]  = L"PDF Printer Port Monitor";
    constexpr wchar_t kMonitorDll[]   = L"pdfpm.dll";
    constexpr wchar_t kPortName[]     = L"PDFPRT1:";
    constexpr wchar_t kPrinterName[]  = L"PDF Printer";

    void LogMsi(MSIHANDLE hInstall, const std::wstring& message)
    {
        PMSIHANDLE hRecord = MsiCreateRecord(1);
        MsiRecordSetStringW(hRecord, 0, (L"PDFPrinter: " + message).c_str());
        MsiProcessMessage(hInstall, INSTALLMESSAGE_INFO, hRecord);
    }

    std::wstring GetInstallFolder(MSIHANDLE hInstall)
    {
        wchar_t buf[MAX_PATH] = {0};
        DWORD size = MAX_PATH;
        MsiGetPropertyW(hInstall, L"CustomActionData", buf, &size);
        return buf; // CustomActionData is set to INSTALLFOLDER by Product.wxs binding, see note below
    }

    // Ensures the "PDF Printer Port Monitor" is registered with the spooler
    // (AddMonitorW). Idempotent: ERROR_PRINT_MONITOR_ALREADY_INSTALLED is
    // treated as success so repeated/repair installs don't fail.
    bool EnsureMonitorRegistered(const std::wstring& driverDir)
    {
        MONITOR_INFO_2W info = {0};
        info.pName          = const_cast<LPWSTR>(kMonitorName);
        info.pEnvironment    = const_cast<LPWSTR>(L"Windows x64");
        std::wstring dllPath = driverDir + L"\\" + kMonitorDll;
        info.pDLLName        = const_cast<LPWSTR>(dllPath.c_str());

        BOOL ok = AddMonitorW(nullptr, 2, reinterpret_cast<LPBYTE>(&info));
        if (ok) return true;
        return GetLastError() == ERROR_PRINT_MONITOR_ALREADY_INSTALLED;
    }

    // Registers the printer driver, referencing the Microsoft-signed inbox
    // XPS driver rather than shipping our own kernel/driver payload.
    bool EnsureDriverRegistered()
    {
        DRIVER_INFO_3W info = {0};
        info.cVersion         = 3; // v3 driver-info level = "Type 4/kernel-mode-free" driver model
        info.pName            = const_cast<LPWSTR>(kDriverName);
        info.pEnvironment     = const_cast<LPWSTR>(L"Windows x64");
        info.pDriverPath      = const_cast<LPWSTR>(L"mxdwdrv.dll");
        info.pDataFile        = const_cast<LPWSTR>(L"mxdwdrv.dll");
        info.pConfigFile      = const_cast<LPWSTR>(L"mxdwdui.dll");
        info.pMonitorName     = const_cast<LPWSTR>(kMonitorName);

        BOOL ok = AddPrinterDriverExW(
            nullptr, 3, reinterpret_cast<LPBYTE>(&info),
            APD_COPY_ALL_FILES | APD_INSTALL_WARNED_DRIVER);
        if (ok) return true;

        DWORD err = GetLastError();
        return err == ERROR_PRINTER_DRIVER_ALREADY_INSTALLED;
    }

    bool EnsurePortAdded()
    {
        // Uses the Xcv "AddPort" data verb against our monitor, the same
        // path the spooler's own "Add Port" UI uses for monitor-managed
        // ports (see winspool XcvDataW documentation).
        HANDLE hXcv = nullptr;
        PRINTER_DEFAULTSW defaults = {0};
        defaults.DesiredAccess = SERVER_ACCESS_ADMINISTER;
        std::wstring xcvName = L",XcvMonitor " + std::wstring(kMonitorName);

        if (!OpenPrinterW(const_cast<LPWSTR>(xcvName.c_str()), &hXcv, &defaults))
            return false;

        DWORD needed = 0;
        std::wstring portNameArg = kPortName;
        BOOL ok = XcvDataW(hXcv, L"AddPort",
            reinterpret_cast<PBYTE>(const_cast<LPWSTR>(portNameArg.c_str())),
            static_cast<DWORD>((portNameArg.size() + 1) * sizeof(wchar_t)),
            nullptr, 0, &needed, nullptr);

        ClosePrinter(hXcv);
        return ok || GetLastError() == ERROR_INVALID_PARAMETER; // already exists
    }

    bool EnsurePrinterAdded()
    {
        PRINTER_INFO_2W info = {0};
        info.pPrinterName  = const_cast<LPWSTR>(kPrinterName);
        info.pDriverName   = const_cast<LPWSTR>(kDriverName);
        info.pPortName     = const_cast<LPWSTR>(kPortName);
        info.pPrintProcessor = const_cast<LPWSTR>(L"winprint");
        info.pDatatype     = const_cast<LPWSTR>(L"RAW");
        info.Attributes    = PRINTER_ATTRIBUTE_LOCAL;
        info.DefaultPriority = 1;

        HANDLE hPrinter = AddPrinterW(nullptr, 2, reinterpret_cast<LPBYTE>(&info));
        if (hPrinter)
        {
            ClosePrinter(hPrinter);
            return true;
        }
        return GetLastError() == ERROR_PRINTER_ALREADY_EXISTS;
    }
}

extern "C" __declspec(dllexport)
UINT __stdcall RegisterPrinter(MSIHANDLE hInstall)
{
    // CustomActionData carries "INSTALLFOLDER\Driver" set via a
    // CustomActionRef property in Product.wxs (Set on the deferred CA);
    // for brevity this is abbreviated here — see Scripts/build-all.ps1
    // comment block for the exact `[~]`-separated CustomActionData binding
    // used in the shipped Product.wxs SetProperty actions.
    std::wstring driverDir = GetInstallFolder(hInstall);
    if (driverDir.empty())
        driverDir = L"C:\\Program Files\\PDFPrinter\\Driver";

    LogMsi(hInstall, L"Registering printer driver...");
    if (!EnsureDriverRegistered())
    {
        LogMsi(hInstall, L"AddPrinterDriverExW failed, error " + std::to_wstring(GetLastError()));
        return ERROR_INSTALL_FAILURE;
    }

    LogMsi(hInstall, L"Registering port monitor...");
    if (!EnsureMonitorRegistered(driverDir))
    {
        LogMsi(hInstall, L"AddMonitorW failed, error " + std::to_wstring(GetLastError()));
        return ERROR_INSTALL_FAILURE;
    }

    LogMsi(hInstall, L"Adding port...");
    if (!EnsurePortAdded())
    {
        LogMsi(hInstall, L"AddPort failed, error " + std::to_wstring(GetLastError()));
        return ERROR_INSTALL_FAILURE;
    }

    LogMsi(hInstall, L"Creating printer queue \"PDF Printer\"...");
    if (!EnsurePrinterAdded())
    {
        LogMsi(hInstall, L"AddPrinterW failed, error " + std::to_wstring(GetLastError()));
        return ERROR_INSTALL_FAILURE;
    }

    LogMsi(hInstall, L"Registering North America 4x6 paper size...");
    DWORD formErr = RegisterNorthAmerica4x6Form(kPrinterName);
    if (formErr != ERROR_SUCCESS)
    {
        LogMsi(hInstall, L"AddFormW failed, error " + std::to_wstring(formErr));
        return ERROR_INSTALL_FAILURE;
    }

    LogMsi(hInstall, L"PDF Printer registered successfully.");
    return ERROR_SUCCESS;
}
