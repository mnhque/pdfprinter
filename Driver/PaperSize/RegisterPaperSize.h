#pragma once

#include <windows.h>
#include <string>

// Registers the North America 4x6 paper size with the specified printer.
// Returns ERROR_SUCCESS on success.
DWORD RegisterNorthAmerica4x6Form(
    const std::wstring& printerName);

// Removes the North America 4x6 paper size from the specified printer.
// Returns ERROR_SUCCESS on success.
DWORD UnregisterNorthAmerica4x6Form(
    const std::wstring& printerName);
