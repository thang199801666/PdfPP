#include <CPPPdf/CPPPdf.h>
#include <CPPPdf/pdfpp_c.h>

#include <string_view>

int main() {
    return std::string_view(pdfpp_c_version()) == CPPPdf::VersionString ? 0 : 1;
}
