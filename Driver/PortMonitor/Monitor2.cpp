#include "PortMonitor.h"

#include <shlwapi.h>
#include <unordered_map>
#include <sstream>
#include <vector>

#pragma comment(lib, "shlwapi.lib")

static std::unordered_map<
    HANDLE,
    std::unique_ptr<PdfPrinterPort>
> g_openPorts;

static std::mutex g_openPortsLock;


// ============================================================
// OpenPort
// ============================================================

BOOL WINAPI Pdf_OpenPort(
    HANDLE /*hMonitor*/,
    LPWSTR pName,
    PHANDLE pHandle)
{
    if (!pName || !pHandle)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    auto port = std::make_unique<PdfPrinterPort>();

    port->portName = pName;

    HANDLE handle =
        reinterpret_cast<HANDLE>(port.get());

    {
        std::lock_guard<std::mutex> lock(g_openPortsLock);

        g_openPorts[handle] = std::move(port);
    }

    *pHandle = handle;

    return TRUE;
}


// ============================================================
// StartDocPort
// ============================================================

BOOL WINAPI Pdf_StartDocPort(
    HANDLE hPort,
    LPWSTR pPrinterName,
    DWORD JobId,
    DWORD /*Level*/,
    LPBYTE /*pDocInfo*/)
{
    std::lock_guard<std::mutex> lock(g_openPortsLock);

    auto it = g_openPorts.find(hPort);

    if (it == g_openPorts.end())
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    PdfPrinterPort& p = *it->second;

    p.printerName =
        pPrinterName ? pPrinterName : L"";

    p.jobIdSpooler = JobId;

    p.jobId = GenerateJobGuid();

    if (!EnsureSpoolDirectoryExists())
    {
        SetLastError(ERROR_PATH_NOT_FOUND);
        return FALSE;
    }

    std::wstringstream path;

    path << PDFPRINTER_SPOOL_DIR
         << L"\\"
         << p.jobId
         << L".xps";

    p.jobFilePath = path.str();

    HANDLE hToken = nullptr;

    if (OpenProcessToken(
        GetCurrentProcess(),
        TOKEN_QUERY,
        &hToken))
    {
        DWORD needed = 0;

        GetTokenInformation(
            hToken,
            TokenUser,
            nullptr,
            0,
            &needed);

        if (needed > 0)
        {
            std::vector<BYTE> buffer(needed);

            if (GetTokenInformation(
                hToken,
                TokenUser,
                buffer.data(),
                needed,
                &needed))
            {
                TOKEN_USER* tokenUser =
                    reinterpret_cast<TOKEN_USER*>(
                        buffer.data());

                LPWSTR sidString = nullptr;

                if (ConvertSidToStringSidW(
                    tokenUser->User.Sid,
                    &sidString))
                {
                    p.userSid = sidString;

                    LocalFree(sidString);
                }
            }
        }

        CloseHandle(hToken);
    }

    p.fileHandle = CreateFileW(
        p.jobFilePath.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (p.fileHandle == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }

    return TRUE;
}


// ============================================================
// WritePort
// ============================================================

BOOL WINAPI Pdf_WritePort(
    HANDLE hPort,
    LPBYTE pBuffer,
    DWORD cbBuf,
    LPDWORD pcbWritten)
{
    PdfPrinterPort* p = nullptr;

    {
        std::lock_guard<std::mutex> lock(g_openPortsLock);

        auto it = g_openPorts.find(hPort);

        if (it == g_openPorts.end())
        {
            SetLastError(ERROR_INVALID_HANDLE);
            return FALSE;
        }

        p = it->second.get();
    }

    std::lock_guard<std::mutex> writeLock(
        p->writeLock);

    if (p->fileHandle == INVALID_HANDLE_VALUE)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    return WriteFile(
        p->fileHandle,
        pBuffer,
        cbBuf,
        pcbWritten,
        nullptr);
}


// ============================================================
// ReadPort
// ============================================================

BOOL WINAPI Pdf_ReadPort(
    HANDLE /*hPort*/,
    LPBYTE /*pBuffer*/,
    DWORD /*cbBuffer*/,
    LPDWORD pcbRead)
{
    if (pcbRead)
    {
        *pcbRead = 0;
    }

    return TRUE;
}


// ============================================================
// EndDocPort
// ============================================================

BOOL WINAPI Pdf_EndDocPort(
    HANDLE hPort)
{
    PdfPrinterPort* p = nullptr;

    {
        std::lock_guard<std::mutex> lock(g_openPortsLock);

        auto it = g_openPorts.find(hPort);

        if (it == g_openPorts.end())
        {
            SetLastError(ERROR_INVALID_HANDLE);
            return FALSE;
        }

        p = it->second.get();
    }

    if (p->fileHandle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(p->fileHandle);

        p->fileHandle =
            INVALID_HANDLE_VALUE;
    }

    WriteJobManifest(
        *p,
        0);

    SignalJobReady(
        p->jobId);

    return TRUE;
}


// ============================================================
// ClosePort
// ============================================================

BOOL WINAPI Pdf_ClosePort(
    HANDLE hPort)
{
    std::lock_guard<std::mutex> lock(
        g_openPortsLock);

    auto it = g_openPorts.find(hPort);

    if (it == g_openPorts.end())
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    g_openPorts.erase(it);

    return TRUE;
}


// ============================================================
// XcvOpenPort
// ============================================================

BOOL WINAPI Pdf_XcvOpenPort(
    HANDLE /*hMonitor*/,
    LPCWSTR /*pszObject*/,
    ACCESS_MASK /*GrantedAccess*/,
    PHANDLE phXcv)
{
    if (!phXcv)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    *phXcv =
        reinterpret_cast<HANDLE>(1);

    return TRUE;
}


// ============================================================
// XcvDataPort
//
// IMPORTANT: DWORD return type.
// ============================================================

DWORD WINAPI Pdf_XcvDataPort(
    HANDLE /*hXcv*/,
    LPCWSTR pszDataName,
    PBYTE /*pInputData*/,
    DWORD /*cbInputData*/,
    PBYTE pOutputData,
    DWORD cbOutputData,
    PDWORD pcbOutputNeeded)
{
    if (!pcbOutputNeeded)
    {
        return ERROR_INVALID_PARAMETER;
    }

    if (pszDataName &&
        lstrcmpW(
            pszDataName,
            L"MonitorUI") == 0)
    {
        static const wchar_t uiDll[] =
            L"pdfpmui.dll";

        *pcbOutputNeeded =
            static_cast<DWORD>(
                sizeof(uiDll));

        if (!pOutputData ||
            cbOutputData < sizeof(uiDll))
        {
            return ERROR_INSUFFICIENT_BUFFER;
        }

        memcpy(
            pOutputData,
            uiDll,
            sizeof(uiDll));

        return ERROR_SUCCESS;
    }

    return ERROR_NOT_SUPPORTED;
}


// ============================================================
// XcvClosePort
// ============================================================

BOOL WINAPI Pdf_XcvClosePort(
    HANDLE /*hXcv*/)
{
    return TRUE;
}


// ============================================================
// MONITOR2
// ============================================================

static MONITOR2 g_monitor2 = {};


// ============================================================
// InitializePrintMonitor2
//
// This is intentionally NOT declared in PortMonitor.h.
// winsplp.h already declares it.
//
// The definition below matches Microsoft's SDK declaration:
//
// LPMONITOR2 WINAPI InitializePrintMonitor2(
//     PMONITORINIT pMonitorInit,
//     PHANDLE phMonitor);
// ============================================================

LPMONITOR2 WINAPI InitializePrintMonitor2(
    PMONITORINIT /*pMonitorInit*/,
    PHANDLE phMonitor)
{
    if (!phMonitor)
    {
        SetLastError(
            ERROR_INVALID_PARAMETER);

        return nullptr;
    }

    ZeroMemory(
        &g_monitor2,
        sizeof(g_monitor2));

    g_monitor2.cbSize =
        sizeof(MONITOR2);

    g_monitor2.pfnOpenPort =
        Pdf_OpenPort;

    g_monitor2.pfnStartDocPort =
        Pdf_StartDocPort;

    g_monitor2.pfnWritePort =
        Pdf_WritePort;

    g_monitor2.pfnReadPort =
        Pdf_ReadPort;

    g_monitor2.pfnEndDocPort =
        Pdf_EndDocPort;

    g_monitor2.pfnClosePort =
        Pdf_ClosePort;

    g_monitor2.pfnXcvOpenPort =
        Pdf_XcvOpenPort;

    g_monitor2.pfnXcvDataPort =
        Pdf_XcvDataPort;

    g_monitor2.pfnXcvClosePort =
        Pdf_XcvClosePort;

    *phMonitor =
        reinterpret_cast<HANDLE>(
            &g_monitor2);

    return &g_monitor2;
}


// ============================================================
// DLL entry point
// ============================================================

BOOL APIENTRY DllMain(
    HMODULE /*hModule*/,
    DWORD /*reason*/,
    LPVOID /*lpReserved*/)
{
    return TRUE;
}
