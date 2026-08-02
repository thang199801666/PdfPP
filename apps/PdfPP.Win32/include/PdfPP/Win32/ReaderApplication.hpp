#pragma once

#include <windows.h>

namespace PdfPP::Win32 {

int RunReaderApplication(HINSTANCE instance, int showCommand, PWSTR commandLine);
int RunReaderApplication(HINSTANCE instance, int showCommand);

} // namespace PdfPP::Win32
