#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <string>
#include <optional>
#include <variant>
#include <CPPPdf/Objects/PdfObject.hpp>
#include <vector>
#include <memory>

namespace CPPPdf {

class PdfExponentialFunction final {
public:
    PdfExponentialFunction(std::vector<double> c0, std::vector<double> c1, double exponent);

    [[nodiscard]] std::vector<double> Evaluate(double input) const;
    [[nodiscard]] std::size_t OutputCount() const noexcept { return c0_.size(); }

private:
    std::vector<double> c0_;
    std::vector<double> c1_;
    double exponent_{};
};

class PdfSampledFunction final {
public:
    PdfSampledFunction(std::size_t inputSize, std::size_t outputSize,
                       std::vector<std::uint32_t> size,
                       std::vector<std::uint16_t> samples,
                       std::vector<double> encode,
                       std::vector<double> decode,
                       std::uint16_t bitsPerSample);
    [[nodiscard]] std::vector<double> Evaluate(std::span<const double> input) const;

private:
    std::size_t inputSize_{};
    std::size_t outputSize_{};
    std::vector<std::uint32_t> size_;
    std::vector<std::uint16_t> samples_;
    std::vector<double> encode_;
    std::vector<double> decode_;
    std::uint16_t bitsPerSample_{};
};

class PdfStitchedFunction final {
public:
    PdfStitchedFunction(std::vector<std::shared_ptr<const PdfExponentialFunction>> functions,
                        std::vector<double> bounds, std::vector<double> encode);
    [[nodiscard]] std::vector<double> Evaluate(double input) const;

private:
    std::vector<std::shared_ptr<const PdfExponentialFunction>> functions_;
    std::vector<double> bounds_;
    std::vector<double> encode_;
};

struct PdfCalculatorLimits final {
    std::size_t maxTokens{4096U};
    std::size_t maxStack{256U};
};

class PdfCalculatorFunction final {
public:
    explicit PdfCalculatorFunction(std::string_view program, PdfCalculatorLimits limits = {});
    [[nodiscard]] std::vector<double> Evaluate(std::span<const double> input,
                                                std::size_t outputCount) const;

private:
    std::string program_;
    PdfCalculatorLimits limits_;
};

using PdfFunctionVariant = std::variant<PdfExponentialFunction, PdfCalculatorFunction>;
[[nodiscard]] std::optional<PdfFunctionVariant> ParsePdfFunction(const PdfDictionary& dictionary,
                                                                 std::string_view calculatorProgram = {});

} // namespace CPPPdf
