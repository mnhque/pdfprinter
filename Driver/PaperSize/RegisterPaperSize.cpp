#include "RegisterPaperSize.h"

#include <windows.h>
#include <winspool.h>

#include <string>

#pragma comment(lib, "winspool.lib")

namespace
{
    constexpr wchar_t kFormName[] =
        L"North America 4x6";

    // 4 x 6 inches, expressed in thousandths of an inch.
    constexpr LONG kWidth =
        4000;

    constexpr LONG kHeight =
        6000;

    // Small margins so the form can be used by normal
    // Windows printer drivers.
    constexpr LONG kLeftMargin =
        0;

    constexpr LONG kTopMargin =
        0;

    constexpr LONG kRightMargin =
        0;

    constexpr LONG kBottomMargin =
        0;
}


// ------------------------------------------------------------
// Register 4x6 form
// ------------------------------------------------------------

DWORD RegisterNorthAmerica4x6Form(
    const std::wstring& printerName)
{
    HANDLE hPrinter = nullptr;

    PRINTER_DEFAULTSW defaults = {};

    defaults.DesiredAccess =
        PRINTER_ACCESS_ADMINISTER;


    //
    // Open printer with administration rights.
    //

    if (!OpenPrinterW(
            const_cast<LPWSTR>(
                printerName.c_str()),
            &hPrinter,
            &defaults))
    {
        return GetLastError();
    }


    //
    // Build the 4x6 form structure.
    //

    FORM_INFO_1W form = {};

    form.Flags = 0;

    form.pName =
        const_cast<LPWSTR>(
            kFormName);


    //
    // Form dimensions are expressed in
    // thousandths of an inch.
    //

    form.Size.Width =
        kWidth;

    form.Size.Height =
        kHeight;


    //
    // Printable area.
    //

    form.ImageableArea.left =
        kLeftMargin;

    form.ImageableArea.top =
        kTopMargin;

    form.ImageableArea.right =
        kRightMargin < kWidth
            ? kWidth - kRightMargin
            : kWidth;

    form.ImageableArea.bottom =
        kBottomMargin < kHeight
            ? kHeight - kBottomMargin
            : kHeight;


    //
    // Check whether the form already exists.
    //

    DWORD needed = 0;

    GetFormW(
        hPrinter,
        const_cast<LPWSTR>(
            kFormName),
        1,
        nullptr,
        0,
        &needed);


    if (GetLastError() ==
        ERROR_INSUFFICIENT_BUFFER)
    {
        //
        // The form already exists.
        // That is fine for repair/reinstall.
        //

        ClosePrinter(hPrinter);

        return ERROR_SUCCESS;
    }


    //
    // Add the form.
    //

    if (!AddFormW(
            hPrinter,
            1,
            reinterpret_cast<LPBYTE>(
                &form)))
    {
        DWORD error =
            GetLastError();

        ClosePrinter(hPrinter);

        //
        // Treat an existing form as success.
        //

        if (error ==
            ERROR_ALREADY_EXISTS)
        {
            return ERROR_SUCCESS;
        }

        return error;
    }


    ClosePrinter(hPrinter);

    return ERROR_SUCCESS;
}


// ------------------------------------------------------------
// Unregister 4x6 form
// ------------------------------------------------------------

DWORD UnregisterNorthAmerica4x6Form(
    const std::wstring& printerName)
{
    HANDLE hPrinter = nullptr;

    PRINTER_DEFAULTSW defaults = {};

    defaults.DesiredAccess =
        PRINTER_ACCESS_ADMINISTER;


    if (!OpenPrinterW(
            const_cast<LPWSTR>(
                printerName.c_str()),
            &hPrinter,
            &defaults))
    {
        return GetLastError();
    }


    //
    // Delete the form.
    //

    BOOL result =
        DeleteFormW(
            hPrinter,
            const_cast<LPWSTR>(
                kFormName));


    DWORD error =
        GetLastError();


    ClosePrinter(hPrinter);


    if (result)
        return ERROR_SUCCESS;


    //
    // If the form doesn't exist,
    // removal is already complete.
    //

    if (error ==
        ERROR_FILE_NOT_FOUND)
    {
        return ERROR_SUCCESS;
    }


    return error;
}
