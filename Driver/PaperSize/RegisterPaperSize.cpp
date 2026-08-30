// RegisterPaperSize.cpp
#include "RegisterPaperSize.h"
#include <string>
#include <vector>

namespace
{
    // Opens the printer handle with the access rights needed to manage forms
    // (PRINTER_ACCESS_ADMINISTER). Caller must ClosePrinter the result.
    bool OpenPrinterForFormAdmin(const std::wstring& printerName, HANDLE& hPrinter)
    {
        PRINTER_DEFAULTSW defaults = {0};
        defaults.DesiredAccess = PRINTER_ACCESS_ADMINISTER;
        return OpenPrinterW(const_cast<LPWSTR>(printerName.c_str()), &hPrinter, &defaults) != 0;
    }
}

DWORD RegisterNorthAmerica4x6Form(const std::wstring& printerName)
{
    HANDLE hPrinter = nullptr;
    if (!OpenPrinterForFormAdmin(printerName, hPrinter))
        return GetLastError();

    FORM_INFO_2W form = {0};
    form.pName          = const_cast<LPWSTR>(NA_4X6_FORM_NAME);
    form.Size.cx        = NA_4X6_WIDTH_HMM;
    form.Size.cy         = NA_4X6_HEIGHT_HMM;
    form.ImageableArea.left   = 0;
    form.ImageableArea.top    = 0;
    form.ImageableArea.right  = NA_4X6_WIDTH_HMM;
    form.ImageableArea.bottom = NA_4X6_HEIGHT_HMM;
    form.Flags        = FORM_PRINTER;   // scoped to this printer, not machine-global
    form.pKeyword      = const_cast<LPWSTR>(NA_4X6_FORM_NAME);
    form.StringType    = STRING_NONE;
    form.pMuiDll        = nullptr;
    form.dwResourceId   = 0;
    form.pDisplayName   = const_cast<LPWSTR>(NA_4X6_FORM_NAME);
    form.wLangId        = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);

    BOOL ok = AddFormW(hPrinter, 2, reinterpret_cast<LPBYTE>(&form));
    DWORD lastError = ok ? ERROR_SUCCESS : GetLastError();

    // ERROR_FILE_EXISTS just means a prior install already registered it
    // (e.g. repair/reinstall) — treat as success so installs are idempotent.
    if (!ok && lastError == ERROR_FILE_EXISTS)
        lastError = ERROR_SUCCESS;

    ClosePrinter(hPrinter);
    return lastError;
}

DWORD RemoveNorthAmerica4x6Form(const std::wstring& printerName)
{
    HANDLE hPrinter = nullptr;
    if (!OpenPrinterForFormAdmin(printerName, hPrinter))
        return GetLastError();

    BOOL ok = DeleteFormW(hPrinter, const_cast<LPWSTR>(NA_4X6_FORM_NAME));
    DWORD lastError = ok ? ERROR_SUCCESS : GetLastError();

    // Not present is fine on uninstall (idempotent).
    if (!ok && lastError == ERROR_UNKNOWN_PRINT_MONITOR)
        lastError = ERROR_SUCCESS;
    if (!ok && lastError == ERROR_INVALID_PARAMETER)
        lastError = ERROR_SUCCESS;

    ClosePrinter(hPrinter);
    return lastError;
}

bool VerifyNorthAmerica4x6Form(const std::wstring& printerName)
{
    HANDLE hPrinter = nullptr;
    if (!OpenPrinterForFormAdmin(printerName, hPrinter))
        return false;

    DWORD needed = 0, returned = 0;
    EnumFormsW(hPrinter, 2, nullptr, 0, &needed, &returned);
    if (needed == 0) { ClosePrinter(hPrinter); return false; }

    std::vector<BYTE> buffer(needed);
    bool found = false;
    if (EnumFormsW(hPrinter, 2, buffer.data(), needed, &needed, &returned))
    {
        auto* forms = reinterpret_cast<FORM_INFO_2W*>(buffer.data());
        for (DWORD i = 0; i < returned; ++i)
        {
            if (forms[i].pName && lstrcmpW(forms[i].pName, NA_4X6_FORM_NAME) == 0 &&
                forms[i].Size.cx == static_cast<LONG>(NA_4X6_WIDTH_HMM) &&
                forms[i].Size.cy == static_cast<LONG>(NA_4X6_HEIGHT_HMM))
            {
                found = true;
                break;
            }
        }
    }
    ClosePrinter(hPrinter);
    return found;
}
