// SpoolWriter.cpp
//
// Small helpers used by Monitor2.cpp: spool directory management, job GUID
// generation, JSON manifest emission, and named-pipe signaling to the
// Conversion Service. Kept separate from the SPI plumbing so the two
// responsibilities (spooler contract vs. our own job hand-off protocol)
// don't get tangled.

#include "PortMonitor.h"
#include <objbase.h>
#include <fstream>
#include <sstream>
#include <sddl.h>

#pragma comment(lib, "ole32.lib")

bool EnsureSpoolDirectoryExists()
{
    wchar_t programData[MAX_PATH];
    if (!SHGetSpecialFolderPathW(nullptr, programData, CSIDL_COMMON_APPDATA, TRUE))
        return false;

    std::wstring dir = std::wstring(programData) + L"\\PDFPrinter\\Spool";

    // Create the tree; ERROR_ALREADY_EXISTS is fine.
    std::wstring accum;
    std::wstringstream ss(dir);
    std::wstring seg;
    std::wstring path;
    size_t pos = 0, next;
    while ((next = dir.find(L'\\', pos)) != std::wstring::npos)
    {
        path = dir.substr(0, next);
        if (path.size() > 2) // skip drive root
            CreateDirectoryW(path.c_str(), nullptr);
        pos = next + 1;
    }
    BOOL created = CreateDirectoryW(dir.c_str(), nullptr);
    return created || GetLastError() == ERROR_ALREADY_EXISTS;
}

std::wstring GenerateJobGuid()
{
    GUID guid;
    if (CoCreateGuid(&guid) != S_OK)
    {
        // Extremely unlikely; fall back to a tick-count based id rather than
        // failing the print job outright.
        wchar_t buf[64];
        swprintf_s(buf, L"job-%08x-%08x", GetTickCount(), GetCurrentThreadId());
        return buf;
    }
    wchar_t buf[64];
    swprintf_s(buf, L"%08lx-%04x-%04x-%02x%02x%02x%02x%02x%02x%02x%02x",
        guid.Data1, guid.Data2, guid.Data3,
        guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
        guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    return buf;
}

// Minimal, dependency-free JSON emission (no external JSON library needed
// for this small, fixed-shape manifest).
bool WriteJobManifest(const PdfPrinterPort& port, DWORD pageCountHint)
{
    std::wstring manifestPath = port.jobFilePath;
    size_t dot = manifestPath.rfind(L".xps");
    if (dot != std::wstring::npos)
        manifestPath = manifestPath.substr(0, dot) + L".json";

    std::wofstream out(manifestPath, std::ios::out | std::ios::trunc);
    if (!out.is_open())
        return false;

    auto jsonEscape = [](const std::wstring& s)
    {
        std::wstring result;
        for (wchar_t c : s)
        {
            if (c == L'\\' || c == L'"') result += L'\\';
            result += c;
        }
        return result;
    };

    out << L"{\n"
        << L"  \"jobId\": \"" << jsonEscape(port.jobId) << L"\",\n"
        << L"  \"spoolerJobId\": " << port.jobIdSpooler << L",\n"
        << L"  \"printerName\": \"" << jsonEscape(port.printerName) << L"\",\n"
        << L"  \"portName\": \"" << jsonEscape(port.portName) << L"\",\n"
        << L"  \"userSid\": \"" << jsonEscape(port.userSid) << L"\",\n"
        << L"  \"xpsFile\": \"" << jsonEscape(port.jobFilePath) << L"\",\n"
        << L"  \"pageCountHint\": " << pageCountHint << L"\n"
        << L"}\n";
    out.close();
    return true;
}

// Best-effort push notification to the Conversion Service so it reacts
// immediately. The service ALSO runs a FileSystemWatcher on the spool
// directory as a fallback in case the pipe isn't connected (e.g. service
// mid-restart), so a missed signal here is not a correctness problem, only
// a latency one.
void SignalJobReady(const std::wstring& jobId)
{
    HANDLE hPipe = CreateFileW(
        PDFPRINTER_PIPE_NAME, GENERIC_WRITE, 0, nullptr,
        OPEN_EXISTING, 0, nullptr);

    if (hPipe == INVALID_HANDLE_VALUE)
        return; // service will pick it up via FileSystemWatcher fallback

    DWORD written = 0;
    std::wstring msg = jobId + L"\n";
    WriteFile(hPipe, msg.c_str(), static_cast<DWORD>(msg.size() * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(hPipe);
}
