// RegisterPaperSize.h
//
// Registers/removes the "North America 4x6" custom form on the PDF Printer
// print queue using the documented Win32 Forms API (AddFormW/DeleteFormW),
// the same mechanism Windows itself uses for user-defined paper sizes added
// via Printer Properties -> Advanced -> "New Form".
#pragma once
#include <windows.h>
#include <winspool.h>

// Exact 4in x 6in in hundredths of a millimeter (1 in = 2540 hundredths-mm).
constexpr DWORD NA_4X6_WIDTH_HMM  = 4 * 2540;  // 10160  -> 101.60 mm
constexpr DWORD NA_4X6_HEIGHT_HMM = 6 * 2540;  // 15240  -> 152.40 mm
constexpr wchar_t NA_4X6_FORM_NAME[] = L"North America 4x6";

// Adds the custom form scoped to the given printer (FORM_PRINTER flag), so it
// is automatically cleaned up if the printer itself is deleted, and is also
// explicitly removed by RemoveNorthAmerica4x6Form on uninstall.
// Returns Win32 error code (ERROR_SUCCESS on success).
DWORD RegisterNorthAmerica4x6Form(const std::wstring& printerName);

// Removes the form; safe to call even if it was already removed.
DWORD RemoveNorthAmerica4x6Form(const std::wstring& printerName);

// Verifies the form is present with the exact expected dimensions —
// used by the installer's post-install self-check and by Tests/.
bool  VerifyNorthAmerica4x6Form(const std::wstring& printerName);
