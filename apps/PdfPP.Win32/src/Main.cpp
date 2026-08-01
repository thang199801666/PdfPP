#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <PdfPP/Win32/ReaderApplication.hpp>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, const int showCommand) {
    return PdfPP::Win32::RunReaderApplication(instance, showCommand);
}
