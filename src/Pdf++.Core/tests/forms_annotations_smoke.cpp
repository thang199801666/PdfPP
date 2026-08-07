#include <CPPPdf/CPPPdf.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void require(const bool condition, const std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

std::string streamObject(const std::string& data, const std::string& extraDictionary = {}) {
    return "<< /Length " + std::to_string(data.size()) +
           (extraDictionary.empty() ? std::string{} : " " + extraDictionary) +
           " >>\nstream\n" + data + "\nendstream";
}

void writeClassicPdf(const std::filesystem::path& path, const std::vector<std::string>& objects) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot create form smoke PDF");
    output << "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
    std::vector<std::uint64_t> offsets(objects.size(), 0U);
    for (std::size_t index = 1U; index < objects.size(); ++index) {
        offsets[index] = static_cast<std::uint64_t>(output.tellp());
        output << index << " 0 obj\n" << objects[index] << "\nendobj\n";
    }
    const auto xref = static_cast<std::uint64_t>(output.tellp());
    output << "xref\n0 " << objects.size() << "\n"
           << "0000000000 65535 f \n";
    for (std::size_t index = 1U; index < objects.size(); ++index) {
        output << std::setw(10) << std::setfill('0') << offsets[index]
               << " 00000 n \n";
    }
    output << "trailer\n<< /Size " << objects.size() << " /Root 1 0 R >>\n"
           << "startxref\n" << xref << "\n%%EOF\n";
}

void createFormFixture(const std::filesystem::path& path) {
    std::vector<std::string> objects(18U);
    objects[1] = "<< /Type /Catalog /Pages 2 0 R /AcroForm 5 0 R >>";
    objects[2] = "<< /Type /Pages /Kids [3 0 R] /Count 1 >>";
    objects[3] = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 300] "
                 "/Contents 4 0 R /Resources << >> "
                 "/Annots [8 0 R 9 0 R 10 0 R 11 0 R 14 0 R 16 0 R 17 0 R] >>";
    objects[4] = streamObject("");
    objects[5] = "<< /Fields [6 0 R 9 0 R 10 0 R 11 0 R 14 0 R 15 0 R] "
                 "/DR << /Font << /Helv << /Type /Font /Subtype /Type1 /BaseFont /Helvetica >> >> >> "
                 "/DA (/Helv 10 Tf 0 g) >>";
    objects[6] = "<< /T (person) /Kids [7 0 R] >>";
    objects[7] = "<< /FT /Tx /T (name) /Parent 6 0 R /Kids [8 0 R] /Ff 4096 "
                 "/V (Line one\\nLine two) >>";
    objects[8] = "<< /Type /Annot /Subtype /Widget /Parent 7 0 R /P 3 0 R "
                 "/Rect [20 235 280 285] >>";
    objects[9] = "<< /Type /Annot /Subtype /Widget /FT /Tx /T (secret) /P 3 0 R "
                 "/Rect [20 195 280 225] /Ff 8192 /MaxLen 8 /V (abc) >>";
    objects[10] = "<< /Type /Annot /Subtype /Widget /FT /Ch /T (country) /P 3 0 R "
                  "/Rect [20 155 280 185] /Ff 393216 /Opt [(VN) (US)] /V (VN) >>";
    objects[11] = "<< /Type /Annot /Subtype /Widget /FT /Btn /T (agree) /P 3 0 R "
                  "/Rect [20 115 50 145] /V /Off /AS /Off "
                  "/AP << /N << /Off 12 0 R /Yes 13 0 R >> >> >>";
    objects[12] = streamObject("q Q", "/Type /XObject /Subtype /Form /BBox [0 0 30 30]");
    objects[13] = streamObject("q 0 0 m 30 30 l S Q", "/Type /XObject /Subtype /Form /BBox [0 0 30 30]");
    objects[14] = "<< /Type /Annot /Subtype /Widget /FT /Tx /T (code) /P 3 0 R "
                  "/Rect [60 115 180 145] /Ff 16777216 /MaxLen 4 /V (ABCD) >>";
    objects[15] = "<< /FT /Btn /T (choice) /Ff 49152 /V /A /Kids [16 0 R 17 0 R] >>";
    objects[16] = "<< /Type /Annot /Subtype /Widget /Parent 15 0 R /P 3 0 R "
                  "/Rect [20 70 50 100] /AS /A /AP << /N << /Off 12 0 R /A 13 0 R >> >> >>";
    objects[17] = "<< /Type /Annot /Subtype /Widget /Parent 15 0 R /P 3 0 R "
                  "/Rect [60 70 90 100] /AS /Off /AP << /N << /Off 12 0 R /B 13 0 R >> >> >>";
    writeClassicPdf(path, objects);
}

std::string readAll(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

const CPPPdf::PdfFormFieldInfo& fieldByName(
    const std::vector<CPPPdf::PdfFormFieldInfo>& fields, const std::string& name) {
    const auto found = std::find_if(fields.begin(), fields.end(), [&](const auto& field) {
        return field.name == name;
    });
    if (found == fields.end()) throw std::runtime_error("form field not found: " + name);
    return *found;
}

void testForms(const std::filesystem::path& directory) {
    using namespace CPPPdf;
    const auto source = directory / "pdfpp_forms_source.pdf";
    const auto updated = directory / "pdfpp_forms_updated.pdf";
    const auto appearances = directory / "pdfpp_forms_appearances.pdf";
    const auto flattened = directory / "pdfpp_forms_flattened.pdf";
    createFormFixture(source);

    const auto fields = PdfAcroForm::GetFields(source);
    require(fields.size() == 6U, "unexpected terminal field count");
    const auto& name = fieldByName(fields, "person.name");
    require(name.multiline && name.hierarchyDepth == 1U, "field hierarchy or multiline flag missing");
    require(name.parentReference && name.parentReference->objectNumber == 6U,
            "parent field reference was not retained");
    const auto& secret = fieldByName(fields, "secret");
    require(secret.password && secret.maxLength == 8U, "password /MaxLen metadata missing");
    const auto& country = fieldByName(fields, "country");
    require(country.combo && country.editableCombo, "editable combo flags missing");
    require(fieldByName(fields, "code").comb, "comb text flag missing");
    const auto& choice = fieldByName(fields, "choice");
    require(choice.radio && choice.noToggleToOff, "radio semantics flags missing");

    bool maxLengthRejected = false;
    try {
        (void)PdfAcroForm::SetFieldValues(source, directory / "invalid-length.pdf",
            {{"secret", "123456789", {}}});
    } catch (const PdfException&) {
        maxLengthRejected = true;
    }
    require(maxLengthRejected, "text exceeding /MaxLen was accepted");

    bool radioOffRejected = false;
    try {
        (void)PdfAcroForm::SetFieldValues(source, directory / "invalid-radio.pdf",
            {{"choice", "Off", {}}});
    } catch (const PdfException&) {
        radioOffRejected = true;
    }
    require(radioOffRejected, "NoToggleToOff radio field was toggled off");

    PdfFormUpdateOptions updateOptions;
    updateOptions.truncateTextToMaxLength = true;
    const auto updateResult = PdfAcroForm::SetFieldValues(source, updated,
        {
            {"person.name", "First line\nSecond line", {}},
            {"secret", "123456789", {}},
            {"country", "CA", {}},
            {"agree", "true", {}},
            {"code", "WXYZ", {}},
            {"choice", "B", {}}
        }, updateOptions);
    require(updateResult.updatedFieldCount == 6U, "not all form fields were updated");

    const auto updatedFields = PdfAcroForm::GetFields(updated);
    require(fieldByName(updatedFields, "secret").value == "12345678", "/MaxLen truncation failed");
    require(fieldByName(updatedFields, "country").value == "CA", "editable combo custom value failed");
    require(fieldByName(updatedFields, "agree").checked, "checkbox state update failed");
    require(fieldByName(updatedFields, "choice").value == "B", "radio export value update failed");

    const auto appearanceResult = PdfAcroForm::GenerateAppearances(updated, appearances);
    require(appearanceResult.generatedAppearanceCount >= 9U,
            "button state and text appearance streams were not generated");
    require(PdfDocument::Open(appearances).GetPageCount() == 1U,
            "form appearance output is unreadable");
    const std::string raw = readAll(appearances);
    require(raw.find("(********)") != std::string::npos, "password appearance was not masked");
    require(raw.find("(First line)") != std::string::npos &&
            raw.find("(Second line)") != std::string::npos,
            "multiline appearance was not split into lines");
    require(raw.find("/Off") != std::string::npos && raw.find("/Yes") != std::string::npos,
            "checkbox appearance-state dictionary is missing");

    const auto flattenResult = PdfAcroForm::FlattenFields(updated, flattened);
    require(flattenResult.flattenedFieldCount == 6U && flattenResult.removedWidgetCount == 7U,
            "field hierarchy was not fully flattened");
    const auto flattenedDocument = PdfDocument::Open(flattened);
    require(PdfAcroForm::GetFields(flattenedDocument).empty(),
            "flattening left orphaned parent fields in the AcroForm tree");
    require(flattenedDocument.GetAnnotations(0U).empty(),
            "flattening left widget annotations on the page");

    std::filesystem::remove(source);
    std::filesystem::remove(updated);
    std::filesystem::remove(appearances);
    std::filesystem::remove(flattened);
    std::filesystem::remove(directory / "invalid-length.pdf");
    std::filesystem::remove(directory / "invalid-radio.pdf");
}

void testAnnotations(const std::filesystem::path& directory) {
    using namespace CPPPdf;
    const auto source = directory / "pdfpp_annotations_source.pdf";
    const auto output = directory / "pdfpp_annotations_output.pdf";

    PdfWriter writer;
    (void)writer.AddPage(PdfRectangle{0.0, 0.0, 300.0, 300.0});
    (void)writer.AddPage(PdfRectangle{0.0, 0.0, 300.0, 300.0});
    writer.Save(source);

    const auto sourceDocument = PdfDocument::Open(source);
    std::optional<PdfReference> streamReference;
    for (const auto number : sourceDocument.objectNumbers()) {
        const PdfReference reference{number, 0U};
        if (sourceDocument.GetObject(reference).AsStream()) {
            streamReference = reference;
            break;
        }
    }
    require(streamReference.has_value(), "source PDF has no stream for Sound annotation smoke test");

    std::vector<PdfAnnotation> annotations;

    PdfAnnotation goTo;
    goTo.pageIndex = 0U;
    goTo.type = PdfAnnotationType::Link;
    goTo.rectangle = {10.0, 250.0, 120.0, 275.0};
    goTo.action.type = PdfAnnotationActionType::GoTo;
    goTo.action.pageIndex = 1U;
    goTo.action.destinationType = PdfAnnotationDestinationType::Xyz;
    goTo.action.left = 20.0;
    goTo.action.top = 280.0;
    goTo.action.zoom = 1.25;
    PdfAnnotationAction nextUri;
    nextUri.type = PdfAnnotationActionType::Uri;
    nextUri.uri = "https://example.com";
    PdfAnnotationAction nextNamed;
    nextNamed.type = PdfAnnotationActionType::Named;
    nextNamed.namedAction = "NextPage";
    goTo.action.next = {nextUri, nextNamed};
    annotations.push_back(goTo);

    PdfAnnotation goToRemote;
    goToRemote.pageIndex = 0U;
    goToRemote.type = PdfAnnotationType::Link;
    goToRemote.rectangle = {10.0, 215.0, 120.0, 240.0};
    goToRemote.action.type = PdfAnnotationActionType::GoToR;
    goToRemote.action.fileName = "remote.pdf";
    goToRemote.action.destinationType = PdfAnnotationDestinationType::Named;
    goToRemote.action.destinationName = "chapter-2";
    goToRemote.action.newWindow = true;
    annotations.push_back(goToRemote);

    PdfAnnotation launch;
    launch.pageIndex = 0U;
    launch.type = PdfAnnotationType::Link;
    launch.rectangle = {10.0, 180.0, 120.0, 205.0};
    launch.action.type = PdfAnnotationActionType::Launch;
    launch.action.fileName = "viewer.exe";
    launch.action.launchParameters = "--safe";
    annotations.push_back(launch);

    PdfAnnotation caret;
    caret.pageIndex = 0U;
    caret.type = PdfAnnotationType::Caret;
    caret.rectangle = {140.0, 250.0, 170.0, 275.0};
    caret.caretSymbol = PdfCaretSymbol::Paragraph;
    caret.rectangleDifferences = {1.0, 2.0, 3.0, 4.0};
    caret.structParent = 7U;
    annotations.push_back(caret);

    PdfAnnotation screen;
    screen.pageIndex = 0U;
    screen.type = PdfAnnotationType::Screen;
    screen.rectangle = {140.0, 200.0, 260.0, 240.0};
    screen.action.type = PdfAnnotationActionType::Uri;
    screen.action.uri = "https://example.com/media";
    annotations.push_back(screen);

    PdfAnnotation movie;
    movie.pageIndex = 0U;
    movie.type = PdfAnnotationType::Movie;
    movie.rectangle = {140.0, 145.0, 260.0, 190.0};
    movie.mediaFileName = "clip.mp4";
    annotations.push_back(movie);

    PdfAnnotation sound;
    sound.pageIndex = 0U;
    sound.type = PdfAnnotationType::Sound;
    sound.rectangle = {140.0, 100.0, 170.0, 130.0};
    sound.mediaReference = streamReference;
    annotations.push_back(sound);

    const auto result = PdfAnnotationEditor::AddAnnotations(source, output, annotations);
    require(result.annotationCount == annotations.size(), "annotation count mismatch");
    const auto reopened = PdfDocument::Open(output);
    require(reopened.GetAnnotations(0U).size() == annotations.size(),
            "annotation output lost annotation objects");

    const std::string raw = readAll(output);
    require(raw.find("/S /GoTo") != std::string::npos, "GoTo action missing");
    require(raw.find("/Next [") != std::string::npos, "chained actions missing");
    require(raw.find("/S /GoToR") != std::string::npos, "GoToR action missing");
    require(raw.find("/S /Launch") != std::string::npos, "Launch action missing");
    require(raw.find("/Subtype /Caret") != std::string::npos &&
            raw.find("/Sy /P") != std::string::npos, "Caret annotation missing");
    require(raw.find("/Subtype /Screen") != std::string::npos, "Screen annotation missing");
    require(raw.find("/Subtype /Movie") != std::string::npos, "Movie annotation missing");
    require(raw.find("/Subtype /Sound") != std::string::npos, "Sound annotation missing");
    require(raw.find("/StructParent 7") != std::string::npos, "annotation StructParent missing");

    std::filesystem::remove(source);
    std::filesystem::remove(output);
}

} // namespace

int main() {
    try {
        const auto directory = std::filesystem::temp_directory_path();
        testForms(directory);
        testAnnotations(directory);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
