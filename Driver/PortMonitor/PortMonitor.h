// PortMonitor.h
// Declarations for the PDF Printer port monitor (pdfpm.dll).
//
// This implements the documented Print Spooler "Monitor2" SPI
// (winsplp.h / MONITOR2 struct). It is the same official extension point
// used by every non-local-port monitor shipped on Windows (e.g. standard
// TCP/IP port monitor, USBPRINT). No undocumented or hacked APIs are used.
#pragma once

#include <windows.h>
#include <winspool.h>
#include <winsplp.h>
#include <sddl.h>
#include <shlobj.h>

#include <string>
#include <memory>
#include <mutex>

#define PDFPRINTER_PORT_PREFIX  L"PDFPRT"
#define PDFPRINTER_MONITOR_NAME L"PDF Printer Port Monitor"
#define PDFPRINTER_SPOOL_DIR    L"\\ProgramData\\PDFPrinter\\Spool"
#define PDFPRINTER_PIPE_NAME    L"\\\\.\\pipe\\PDFPrinterJobReady"

// One open port instance == one in-flight print job.
struct PdfPrinterPort
{
    std::wstring    portName;       // e.g. "PDFPRT1:"
    std::wstring    jobFilePath;    // %ProgramData%\PDFPrinter\Spool\<guid>.xps
    std::wstring    jobId;          // <guid>
    HANDLE          fileHandle = INVALID_HANDLE_VALUE;
    DWORD           jobIdSpooler = 0;   // spooler job id, for manifest correlation
    std::wstring    printerName;
    std::wstring    userSid;
    std::mutex      writeLock;
};

// --- Monitor2 entry points (see winsplp.h for canonical signatures) ---

BOOL WINAPI Pdf_OpenPort(HANDLE hMonitor, LPWSTR pName, PHANDLE pHandle);
BOOL WINAPI Pdf_StartDocPort(HANDLE hPort, LPWSTR pPrinterName, DWORD JobId,
                              DWORD Level, LPBYTE pDocInfo);
BOOL WINAPI Pdf_WritePort(HANDLE hPort, LPBYTE pBuffer, DWORD cbBuf, LPDWORD pcbWritten);
BOOL WINAPI Pdf_ReadPort(HANDLE hPort, LPBYTE pBuffer, DWORD cbBuffer, LPDWORD pcbRead);
BOOL WINAPI Pdf_EndDocPort(HANDLE hPort);
BOOL WINAPI Pdf_ClosePort(HANDLE hPort);
BOOL WINAPI Pdf_XcvOpenPort(HANDLE hMonitor, LPCWSTR pszObject, ACCESS_MASK GrantedAccess, PHANDLE phXcv);
BOOL WINAPI Pdf_XcvDataPort(HANDLE hXcv, LPCWSTR pszDataName, PBYTE pInputData, DWORD cbInputData,
                             PBYTE pOutputData, DWORD cbOutputData, PDWORD pcbOutputNeeded);
BOOL WINAPI Pdf_XcvClosePort(HANDLE hXcv);

// Job lifecycle helpers implemented in SpoolWriter.cpp
bool  EnsureSpoolDirectoryExists();
std::wstring GenerateJobGuid();
bool  WriteJobManifest(const PdfPrinterPort& port, DWORD pageCountHint);
void  SignalJobReady(const std::wstring& jobId);
