// Monitor2.cpp
//
// Implements InitializePrintMonitor2 — the single required export the Print
// Spooler looks up (via GetProcAddress on pdfpm.dll after AddMonitor()) to
// obtain a MONITOR2 vtable. This is documented spooler plumbing
// (see "Print Monitor Reference" / winsplp.h), not a private hook.
//
// Responsibility of this file: bind our functions into MONITOR2 and manage
// per-port state. The actual "capture the job to disk" logic lives in
// SpoolWriter.cpp; this file focuses on the SPI contract.

#include "PortMonitor.h"
#include <shlwapi.h>
#include <unordered_map>
#include <sstream>

#pragma comment(lib, "shlwapi.lib")

static std::unordered_map<HANDLE, std::unique_ptr<PdfPrinterPort>> g_openPorts;
static std::mutex g_openPortsLock;

// ---------------------------------------------------------------------------
// OpenPort: called once per print job when the spooler begins writing to our
// registered port ("PDFPRT1:", "PDFPRT2:", ... one logical port is enough
// since each StartDocPort/EndDocPort pair is a discrete job; we allocate a
// fresh job file per StartDocPort call so concurrent jobs on the same queue
// still land in separate files).
// ---------------------------------------------------------------------------
BOOL WINAPI Pdf_OpenPort(HANDLE /*hMonitor*/, LPWSTR pName, PHANDLE pHandle)
{
    if (!pName || !pHandle) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }

    auto port = std::make_unique<PdfPrinterPort>();
    port->portName = pName;

    HANDLE handle = reinterpret_cast<HANDLE>(port.get());
    {
        std::lock_guard<std::mutex> lock(g_openPortsLock);
        g_openPorts[handle] = std::move(port);
    }
    *pHandle = handle;
    return TRUE;
}

// ---------------------------------------------------------------------------
// StartDocPort: a job has begun. Create the per-job spool file under
// %ProgramData%\PDFPrinter\Spool\<guid>.xps and stash job metadata for the
// manifest we'll write on EndDocPort.
// ---------------------------------------------------------------------------
BOOL WINAPI Pdf_StartDocPort(HANDLE hPort, LPWSTR pPrinterName, DWORD JobId,
                              DWORD /*Level*/, LPBYTE /*pDocInfo*/)
{
    std::lock_guard<std::mutex> lock(g_openPortsLock);
    auto it = g_openPorts.find(hPort);
    if (it == g_openPorts.end()) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }

    PdfPrinterPort& p = *it->second;
    p.printerName   = pPrinterName ? pPrinterName : L"";
    p.jobIdSpooler  = JobId;
    p.jobId         = GenerateJobGuid();

    if (!EnsureSpoolDirectoryExists())
    {
        SetLastError(ERROR_PATH_NOT_FOUND);
        return FALSE;
    }

    std::wstringstream path;
    path << PDFPRINTER_SPOOL_DIR << L"\\" << p.jobId << L".xps";
    p.jobFilePath = path.str();

    // Resolve and record the submitting user's SID so the Conversion Service
    // can show the Save dialog in the correct interactive session rather than
    // guessing.
    HANDLE hToken = nullptr;
    
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
    {
        DWORD needed = 0;
        GetTokenInformation(hToken, TokenUser, nullptr, 0, &needed);
        if (needed > 0)
        {
            std::vector<BYTE> buf(needed);
            if (GetTokenInformation(hToken, TokenUser, buf.data(), needed, &needed))
            {
                auto* tokenUser = reinterpret_cast<TOKEN_USER*>(buf.data());
                LPWSTR sidStr = nullptr;
                if (ConvertSidToStringSidW(tokenUser->User.Sid, &sidStr))
                {
                    p.userSid = sidStr;
                    LocalFree(sidStr);
                }
            }
        }
        CloseHandle(hToken);
    }

    p.fileHandle = CreateFileW(
        p.jobFilePath.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    return p.fileHandle != INVALID_HANDLE_VALUE;
}

// ---------------------------------------------------------------------------
// WritePort: the spooler streams the driver-rendered XPS data here in order,
// exactly as it would for a physical port. We append it verbatim to the job
// file — no transformation happens in the port monitor itself, keeping this
// component small and auditable; conversion logic lives entirely in the
// separate Conversion Service process.
// ---------------------------------------------------------------------------
BOOL WINAPI Pdf_WritePort(HANDLE hPort, LPBYTE pBuffer, DWORD cbBuf, LPDWORD pcbWritten)
{
    PdfPrinterPort* p = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_openPortsLock);
        auto it = g_openPorts.find(hPort);
        if (it == g_openPorts.end()) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
        p = it->second.get();
    }

    std::lock_guard<std::mutex> writeLock(p->writeLock);
    if (p->fileHandle == INVALID_HANDLE_VALUE) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }

    return WriteFile(p->fileHandle, pBuffer, cbBuf, pcbWritten, nullptr);
}

BOOL WINAPI Pdf_ReadPort(HANDLE /*hPort*/, LPBYTE /*pBuffer*/, DWORD /*cbBuffer*/, LPDWORD pcbRead)
{
    // This port is write-only (PDF generation), matching how FILE: ports
    // behave for jobs that never query back-channel status.
    if (pcbRead) *pcbRead = 0;
    return TRUE;
}

// ---------------------------------------------------------------------------
// EndDocPort: job finished. Close the spool file, write the manifest
// (printer, paper size hint, requesting user SID), and signal the
// Conversion Service over the named pipe so it can pick the job up and start
// XPS -> PDF conversion immediately rather than polling.
// ---------------------------------------------------------------------------
BOOL WINAPI Pdf_EndDocPort(HANDLE hPort)
{
    PdfPrinterPort* p = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_openPortsLock);
        auto it = g_openPorts.find(hPort);
        if (it == g_openPorts.end()) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
        p = it->second.get();
    }

    if (p->fileHandle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(p->fileHandle);
        p->fileHandle = INVALID_HANDLE_VALUE;
    }

    if (!WriteJobManifest(*p, /*pageCountHint*/ 0))
    {
        // Non-fatal: the service also derives page count itself while
        // converting. Manifest is best-effort metadata for the Save UI.
        OutputDebugStringW(L"PDFPrinter: manifest write failed, continuing.");
    }

    SignalJobReady(p->jobId);
    return TRUE;
}

BOOL WINAPI Pdf_ClosePort(HANDLE hPort)
{
    std::lock_guard<std::mutex> lock(g_openPortsLock);
    auto it = g_openPorts.find(hPort);
    if (it == g_openPorts.end()) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    g_openPorts.erase(it);
    return TRUE;
}

// XcvOpenPort/XcvDataPort/XcvClosePort back the "Configure Port" /
// "port enumeration" UI (Ports tab, Add/Delete Port dialogs). We implement
// the minimal required verbs: "MonitorUI" (returns our config DLL name) and
// port enumeration used by the spooler's port-management UI.
BOOL WINAPI Pdf_XcvOpenPort(HANDLE /*hMonitor*/, LPCWSTR /*pszObject*/,
                             ACCESS_MASK /*GrantedAccess*/, PHANDLE phXcv)
{
    *phXcv = reinterpret_cast<HANDLE>(1); // stateless; no per-Xcv data required
    return TRUE;
}

BOOL WINAPI Pdf_XcvDataPort(HANDLE /*hXcv*/, LPCWSTR pszDataName, PBYTE /*pInputData*/,
                             DWORD /*cbInputData*/, PBYTE pOutputData, DWORD cbOutputData,
                             PDWORD pcbOutputNeeded)
{
    if (pszDataName && lstrcmpW(pszDataName, L"MonitorUI") == 0)
    {
        static const wchar_t uiDll[] = L"pdfpmui.dll";
        *pcbOutputNeeded = sizeof(uiDll);
        if (cbOutputData >= sizeof(uiDll) && pOutputData)
        {
            memcpy(pOutputData, uiDll, sizeof(uiDll));
            return TRUE;
        }
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL WINAPI Pdf_XcvClosePort(HANDLE /*hXcv*/) { return TRUE; }

// ---------------------------------------------------------------------------
// InitializePrintMonitor2 — the one export the spooler calls after AddMonitor
// registers pdfpm.dll. Returns the vtable of entry points above.
// ---------------------------------------------------------------------------
static MONITOR2 g_monitor2 = {0};

extern "C" __declspec(dllexport)
PMONITOR2 WINAPI InitializePrintMonitor2(PMONITORINIT /*pMonitorInit*/, PHANDLE phMonitor)
{
    ZeroMemory(&g_monitor2, sizeof(g_monitor2));
    g_monitor2.cbSize          = sizeof(MONITOR2);
    g_monitor2.pfnOpenPort     = Pdf_OpenPort;
    g_monitor2.pfnStartDocPort = Pdf_StartDocPort;
    g_monitor2.pfnWritePort    = Pdf_WritePort;
    g_monitor2.pfnReadPort     = Pdf_ReadPort;
    g_monitor2.pfnEndDocPort   = Pdf_EndDocPort;
    g_monitor2.pfnClosePort    = Pdf_ClosePort;
    g_monitor2.pfnXcvOpenPort  = Pdf_XcvOpenPort;
    g_monitor2.pfnXcvDataPort  = Pdf_XcvDataPort;
    g_monitor2.pfnXcvClosePort = Pdf_XcvClosePort;

    *phMonitor = reinterpret_cast<HANDLE>(&g_monitor2);
    return &g_monitor2;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID)
{
    switch (reason)
    {
        case DLL_PROCESS_ATTACH:
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}
