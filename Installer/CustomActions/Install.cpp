// Install.cpp
//
// PDF Printer MSI deferred custom action.
// Registers the printer driver, port monitor, port, printer queue,
// and North America 4x6 paper form.

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
    constexpr wchar_t kDriverName[] =
        L"Microsoft XPS Document Writer v4";

    constexpr wchar_t kMonitorName[] =
        L"PDF Printer Port Monitor";

    constexpr wchar_t kMonitorDll[] =
        L"pdfpm.dll";

    constexpr wchar_t kPortName[] =
        L"PDFPRT1:";

    constexpr wchar_t kPrinterName[] =
        L"PDF Printer";


    // ---------------------------------------------------------
    // MSI logging
    // ---------------------------------------------------------

    void LogMsi(
        MSIHANDLE hInstall,
        const std::wstring& message)
    {
        PMSIHANDLE hRecord =
            MsiCreateRecord(1);

        if (!hRecord)
            return;

        std::wstring text =
            L"PDFPrinter: " + message;

        MsiRecordSetStringW(
            hRecord,
            0,
            text.c_str());

        MsiProcessMessage(
            hInstall,
            INSTALLMESSAGE_INFO,
            hRecord);
    }


    // ---------------------------------------------------------
    // Get installation folder
    // ---------------------------------------------------------

    std::wstring GetInstallFolder(
        MSIHANDLE hInstall)
    {
        wchar_t buffer[MAX_PATH] = {};

        DWORD size = MAX_PATH;

        UINT result =
            MsiGetPropertyW(
                hInstall,
                L"CustomActionData",
                buffer,
                &size);

        if (result != ERROR_SUCCESS)
            return L"";

        return std::wstring(buffer);
    }


    // ---------------------------------------------------------
    // Register port monitor
    // ---------------------------------------------------------

    bool EnsureMonitorRegistered(
        const std::wstring& driverDir)
    {
        MONITOR_INFO_2W info = {};

        info.pName =
            const_cast<LPWSTR>(
                kMonitorName);

        info.pEnvironment =
            const_cast<LPWSTR>(
                L"Windows x64");

        std::wstring dllPath =
            driverDir +
            L"\\" +
            kMonitorDll;

        info.pDLLName =
            const_cast<LPWSTR>(
                dllPath.c_str());

        BOOL result =
            AddMonitorW(
                nullptr,
                2,
                reinterpret_cast<LPBYTE>(
                    &info));

        if (result)
            return true;

        DWORD error =
            GetLastError();

        return error ==
            ERROR_PRINT_MONITOR_ALREADY_INSTALLED;
    }


    // ---------------------------------------------------------
    // Register printer driver
    // ---------------------------------------------------------

    bool EnsureDriverRegistered()
    {
        DRIVER_INFO_3W info = {};

        info.cVersion = 3;

        info.pName =
            const_cast<LPWSTR>(
                kDriverName);

        info.pEnvironment =
            const_cast<LPWSTR>(
                L"Windows x64");

        info.pDriverPath =
            const_cast<LPWSTR>(
                L"mxdwdrv.dll");

        info.pDataFile =
            const_cast<LPWSTR>(
                L"mxdwdrv.dll");

        info.pConfigFile =
            const_cast<LPWSTR>(
                L"mxdwdui.dll");

        info.pMonitorName =
            const_cast<LPWSTR>(
                kMonitorName);


        //
        // Windows SDK compatible call.
        //
        // AddPrinterDriverExW takes FOUR arguments:
        //
        //   1. printer server
        //   2. driver information level
        //   3. driver information structure
        //   4. copy flags
        //
        // We intentionally do NOT use
        // APD_INSTALL_WARNED_DRIVER because it is not
        // available in the Windows SDK used by the
        // GitHub Actions runner.
        //

        BOOL result =
            AddPrinterDriverExW(
                nullptr,
                3,
                reinterpret_cast<LPBYTE>(
                    &info),
                APD_COPY_ALL_FILES);

        if (result)
            return true;

        DWORD error =
            GetLastError();

        //
        // Driver already installed is not a failure.
        //

        if (error ==
            ERROR_PRINTER_DRIVER_ALREADY_INSTALLED)
        {
            return true;
        }

        return false;
    }


    // ---------------------------------------------------------
    // Add PDF printer port
    // ---------------------------------------------------------

    bool EnsurePortAdded()
    {
        HANDLE hXcv = nullptr;

        PRINTER_DEFAULTSW defaults = {};

        defaults.DesiredAccess =
            SERVER_ACCESS_ADMINISTER;

        std::wstring xcvName =
            L",XcvMonitor " +
            std::wstring(kMonitorName);


        if (!OpenPrinterW(
                const_cast<LPWSTR>(
                    xcvName.c_str()),
                &hXcv,
                &defaults))
        {
            return false;
        }


        std::wstring portName =
            kPortName;

        DWORD needed = 0;


        BOOL result =
            XcvDataW(
                hXcv,
                L"AddPort",

                reinterpret_cast<PBYTE>(
                    const_cast<LPWSTR>(
                        portName.c_str())),

                static_cast<DWORD>(
                    (portName.size() + 1) *
                    sizeof(wchar_t)),

                nullptr,
                0,
                &needed,
                nullptr);


        DWORD error =
            GetLastError();


        ClosePrinter(hXcv);


        //
        // Successful creation.
        //

        if (result)
            return true;


        //
        // Some spooler versions return
        // ERROR_INVALID_PARAMETER when the
        // port already exists.
        //

        if (error ==
            ERROR_INVALID_PARAMETER)
        {
            return true;
        }


        //
        // Also accept "already exists" if
        // returned by the spooler.
        //

        if (error ==
            ERROR_FILE_EXISTS ||
            error ==
            ERROR_ALREADY_EXISTS)
        {
            return true;
        }


        return false;
    }


    // ---------------------------------------------------------
    // Create printer queue
    // ---------------------------------------------------------

    bool EnsurePrinterAdded()
    {
        PRINTER_INFO_2W info = {};

        info.pPrinterName =
            const_cast<LPWSTR>(
                kPrinterName);

        info.pDriverName =
            const_cast<LPWSTR>(
                kDriverName);

        info.pPortName =
            const_cast<LPWSTR>(
                kPortName);

        info.pPrintProcessor =
            const_cast<LPWSTR>(
                L"winprint");

        info.pDatatype =
            const_cast<LPWSTR>(
                L"RAW");

        info.Attributes =
            PRINTER_ATTRIBUTE_LOCAL;

        info.DefaultPriority = 1;


        HANDLE hPrinter =
            AddPrinterW(
                nullptr,
                2,
                reinterpret_cast<LPBYTE>(
                    &info));


        if (hPrinter)
        {
            ClosePrinter(hPrinter);
            return true;
        }


        DWORD error =
            GetLastError();


        return error ==
            ERROR_PRINTER_ALREADY_EXISTS;
    }
}


// =============================================================
// MSI Custom Action
// =============================================================

extern "C"
__declspec(dllexport)
UINT __stdcall RegisterPrinter(
    MSIHANDLE hInstall)
{
    //
    // CustomActionData contains the driver directory.
    //

    std::wstring driverDir =
        GetInstallFolder(hInstall);


    if (driverDir.empty())
    {
        driverDir =
            L"C:\\Program Files\\PDFPrinter\\Driver";
    }


    // ---------------------------------------------------------
    // Driver
    // ---------------------------------------------------------

    LogMsi(
        hInstall,
        L"Registering printer driver...");


    if (!EnsureDriverRegistered())
    {
        DWORD error =
            GetLastError();

        LogMsi(
            hInstall,
            L"AddPrinterDriverExW failed, error " +
            std::to_wstring(error));

        return ERROR_INSTALL_FAILURE;
    }


    // ---------------------------------------------------------
    // Port monitor
    // ---------------------------------------------------------

    LogMsi(
        hInstall,
        L"Registering port monitor...");


    if (!EnsureMonitorRegistered(
            driverDir))
    {
        DWORD error =
            GetLastError();

        LogMsi(
            hInstall,
            L"AddMonitorW failed, error " +
            std::to_wstring(error));

        return ERROR_INSTALL_FAILURE;
    }


    // ---------------------------------------------------------
    // Port
    // ---------------------------------------------------------

    LogMsi(
        hInstall,
        L"Adding port...");


    if (!EnsurePortAdded())
    {
        DWORD error =
            GetLastError();

        LogMsi(
            hInstall,
            L"AddPort failed, error " +
            std::to_wstring(error));

        return ERROR_INSTALL_FAILURE;
    }


    // ---------------------------------------------------------
    // Printer queue
    // ---------------------------------------------------------

    LogMsi(
        hInstall,
        L"Creating printer queue \"PDF Printer\"...");


    if (!EnsurePrinterAdded())
    {
        DWORD error =
            GetLastError();

        LogMsi(
            hInstall,
            L"AddPrinterW failed, error " +
            std::to_wstring(error));

        return ERROR_INSTALL_FAILURE;
    }


    // ---------------------------------------------------------
    // 4x6 paper size
    // ---------------------------------------------------------

    LogMsi(
        hInstall,
        L"Registering North America 4x6 paper size...");


    DWORD formError =
        RegisterNorthAmerica4x6Form(
            kPrinterName);


    if (formError != ERROR_SUCCESS)
    {
        LogMsi(
            hInstall,
            L"AddFormW failed, error " +
            std::to_wstring(formError));

        return ERROR_INSTALL_FAILURE;
    }


    // ---------------------------------------------------------
    // Success
    // ---------------------------------------------------------

    LogMsi(
        hInstall,
        L"PDF Printer registered successfully.");


    return ERROR_SUCCESS;
}
