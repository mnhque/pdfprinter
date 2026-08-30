#pragma once

#include <windows.h>
#include <winspool.h>
#include <winsplp.h>
#include <sddl.h>
#include <shlobj.h>

#include <string>
#include <memory>
#include <mutex>
#include <vector>

#define PDFPRINTER_PORT_PREFIX  L"PDFPRT"
#define PDFPRINTER_MONITOR_NAME L"PDF Printer Port Monitor"
#define PDFPRINTER_SPOOL_DIR    L"\\ProgramData\\PDFPrinter\\Spool"
#define PDFPRINTER_PIPE_NAME    L"\\\\.\\pipe\\PDFPrinterJobReady"

struct PdfPrinterPort
{
    std::wstring portName;
    std::wstring jobFilePath;
    std::wstring jobId;
    HANDLE fileHandle = INVALID_HANDLE_VALUE;
    DWORD jobIdSpooler = 0;
    std::wstring printerName;
    std::wstring userSid;
    std::mutex writeLock;
};

BOOL WINAPI Pdf_OpenPort(
    HANDLE hMonitor,
    LPWSTR pName,
    PHANDLE pHandle);

BOOL WINAPI Pdf_StartDocPort(
    HANDLE hPort,
    LPWSTR pPrinterName,
    DWORD JobId,
    DWORD Level,
    LPBYTE pDocInfo);

BOOL WINAPI Pdf_WritePort(
    HANDLE hPort,
    LPBYTE pBuffer,
    DWORD cbBuf,
    LPDWORD pcbWritten);

BOOL WINAPI Pdf_ReadPort(
    HANDLE hPort,
    LPBYTE pBuffer,
    DWORD cbBuffer,
    LPDWORD pcbRead);

BOOL WINAPI Pdf_EndDocPort(
    HANDLE hPort);

BOOL WINAPI Pdf_ClosePort(
    HANDLE hPort);

BOOL WINAPI Pdf_XcvOpenPort(
    HANDLE hMonitor,
    LPCWSTR pszObject,
    ACCESS_MASK GrantedAccess,
    PHANDLE phXcv);

DWORD WINAPI Pdf_XcvDataPort(
    HANDLE hXcv,
    LPCWSTR pszDataName,
    PBYTE pInputData,
    DWORD cbInputData,
    PBYTE pOutputData,
    DWORD cbOutputData,
    PDWORD pcbOutputNeeded);

BOOL WINAPI Pdf_XcvClosePort(
    HANDLE hXcv);

bool EnsureSpoolDirectoryExists();

std::wstring GenerateJobGuid();

bool WriteJobManifest(
    const PdfPrinterPort& port,
    DWORD pageCountHint);

void SignalJobReady(
    const std::wstring& jobId);
