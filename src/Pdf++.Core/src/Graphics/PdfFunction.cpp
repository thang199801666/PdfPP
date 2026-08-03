#include <CPPPdf/Graphics/PdfFunction.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <numeric>
#include <cctype>
#include <stack>

namespace CPPPdf {

PdfExponentialFunction::PdfExponentialFunction(
    std::vector<double> c0, std::vector<double> c1, const double exponent)
    : c0_(std::move(c0)), c1_(std::move(c1)), exponent_(exponent) {
    if (c0_.empty() || c0_.size() != c1_.size() || !std::isfinite(exponent_) || exponent_ < 0.0) {
        throw std::invalid_argument("Invalid exponential PDF function parameters.");
    }
}

std::vector<double> PdfExponentialFunction::Evaluate(const double input) const {
    const double clamped = std::clamp(input, 0.0, 1.0);
    const double power = std::pow(clamped, exponent_);
    std::vector<double> result;
    result.reserve(c0_.size());
    for (std::size_t index = 0; index < c0_.size(); ++index) {
        result.push_back(c0_[index] + power * (c1_[index] - c0_[index]));
    }
    return result;
}

PdfSampledFunction::PdfSampledFunction(
    const std::size_t inputSize, const std::size_t outputSize,
    std::vector<std::uint32_t> size, std::vector<std::uint16_t> samples,
    std::vector<double> encode, std::vector<double> decode,
    const std::uint16_t bitsPerSample)
    : inputSize_(inputSize), outputSize_(outputSize), size_(std::move(size)),
      samples_(std::move(samples)), encode_(std::move(encode)), decode_(std::move(decode)),
      bitsPerSample_(bitsPerSample) {
    if (inputSize_ == 0U || outputSize_ == 0U || size_.size() != inputSize_ ||
        encode_.size() != inputSize_ * 2U || decode_.size() != outputSize_ * 2U ||
        bitsPerSample_ == 0U || bitsPerSample_ > 16U) {
        throw std::invalid_argument("Invalid sampled PDF function parameters.");
    }
}

std::vector<double> PdfSampledFunction::Evaluate(const std::span<const double> input) const {
    if (input.size() != inputSize_) throw std::invalid_argument("Sampled function input dimension mismatch.");
    std::vector<std::uint32_t> index(inputSize_);
    for (std::size_t i = 0; i < inputSize_; ++i) {
        const double encoded = std::clamp(input[i], 0.0, 1.0) * (encode_[i * 2U + 1U] - encode_[i * 2U]) + encode_[i * 2U];
        index[i] = std::min<std::uint32_t>(size_[i] - 1U, static_cast<std::uint32_t>(std::lround(std::clamp(encoded, 0.0, static_cast<double>(size_[i] - 1U)))));
    }
    std::size_t offset = 0U;
    for (std::size_t i = 0; i < inputSize_; ++i) offset = offset * size_[i] + index[i];
    std::vector<double> result(outputSize_);
    const double maxSample = static_cast<double>((1U << bitsPerSample_) - 1U);
    for (std::size_t output = 0; output < outputSize_; ++output) {
        const auto sample = samples_.at(offset * outputSize_ + output);
        const double normalized = static_cast<double>(sample) / maxSample;
        result[output] = decode_[output * 2U] + normalized * (decode_[output * 2U + 1U] - decode_[output * 2U]);
    }
    return result;
}

PdfStitchedFunction::PdfStitchedFunction(
    std::vector<std::shared_ptr<const PdfExponentialFunction>> functions,
    std::vector<double> bounds, std::vector<double> encode)
    : functions_(std::move(functions)), bounds_(std::move(bounds)), encode_(std::move(encode)) {
    if (functions_.empty() || bounds_.size() + 1U != functions_.size() || encode_.size() != functions_.size() * 2U)
        throw std::invalid_argument("Invalid stitched PDF function parameters.");
}

std::vector<double> PdfStitchedFunction::Evaluate(const double input) const {
    const double value = std::clamp(input, 0.0, 1.0);
    const auto position = std::lower_bound(bounds_.begin(), bounds_.end(), value);
    const std::size_t index = static_cast<std::size_t>(position - bounds_.begin());
    const double lower = index == 0U ? 0.0 : bounds_[index - 1U];
    const double upper = index == bounds_.size() ? 1.0 : bounds_[index];
    const double encoded = encode_[index * 2U] + (value - lower) / std::max(1.0e-12, upper - lower) *
        (encode_[index * 2U + 1U] - encode_[index * 2U]);
    return functions_[index]->Evaluate(encoded);
}

PdfCalculatorFunction::PdfCalculatorFunction(const std::string_view program, const PdfCalculatorLimits limits)
    : program_(program), limits_(limits) {
    if (limits_.maxTokens == 0U || limits_.maxStack == 0U) throw std::invalid_argument("Invalid calculator limits.");
}

std::optional<PdfFunctionVariant> ParsePdfFunction(const PdfDictionary& dictionary,
                                                   const std::string_view calculatorProgram) {
    const auto* type = dictionary.Find(PdfName("FunctionType"));
    if (type == nullptr) return std::nullopt;
    const auto functionType = type->AsInteger().value_or(-1);
    if (functionType == 2) {
        const auto* c0 = dictionary.GetAsArray(PdfName("C0"));
        const auto* c1 = dictionary.GetAsArray(PdfName("C1"));
        const auto* exponent = dictionary.Find(PdfName("N"));
        if (!c0 || !c1 || !exponent || c0->size() != c1->size() || c0->empty()) return std::nullopt;
        std::vector<double> first, second;
        for (std::size_t i = 0; i < c0->size(); ++i) {
            first.push_back(c0->at(i).AsReal().value_or(static_cast<double>(c0->at(i).AsInteger().value_or(0))));
            second.push_back(c1->at(i).AsReal().value_or(static_cast<double>(c1->at(i).AsInteger().value_or(1))));
        }
        return PdfFunctionVariant(std::in_place_type<PdfExponentialFunction>, std::move(first), std::move(second),
                                  exponent->AsReal().value_or(static_cast<double>(exponent->AsInteger().value_or(1))));
    }
    if (functionType == 4 && !calculatorProgram.empty()) {
        return PdfFunctionVariant(std::in_place_type<PdfCalculatorFunction>, calculatorProgram);
    }
    return std::nullopt;
}

std::vector<double> PdfCalculatorFunction::Evaluate(const std::span<const double> input,
                                                    const std::size_t outputCount) const {
    std::vector<double> stack(input.begin(), input.end());
    if (stack.size() > limits_.maxStack) throw std::runtime_error("Calculator stack limit exceeded.");
    std::size_t tokens{};
    std::size_t position{};
    const auto pop = [&]() {
        if (stack.empty()) throw std::runtime_error("Calculator stack underflow.");
        const double value = stack.back(); stack.pop_back(); return value;
    };
    while (position < program_.size()) {
        while (position < program_.size() && std::isspace(static_cast<unsigned char>(program_[position]))) ++position;
        if (position >= program_.size()) break;
        const std::size_t begin = position;
        while (position < program_.size() && !std::isspace(static_cast<unsigned char>(program_[position]))) ++position;
        const std::string_view token(program_.data() + begin, position - begin);
        if (++tokens > limits_.maxTokens) throw std::runtime_error("Calculator token limit exceeded.");
        const std::string tokenText(token);
        char* end{};
        const double number = std::strtod(tokenText.c_str(), &end);
        if (end != nullptr && *end == '\0') {
            if (stack.size() >= limits_.maxStack) throw std::runtime_error("Calculator stack limit exceeded.");
            stack.push_back(number);
            continue;
        }
        if (token == "add" || token == "sub" || token == "mul" || token == "div") {
            const double right = pop(); const double left = pop();
            if (token == "add") stack.push_back(left + right);
            else if (token == "sub") stack.push_back(left - right);
            else if (token == "mul") stack.push_back(left * right);
            else {
                if (std::abs(right) <= 1.0e-18) throw std::runtime_error("Calculator division by zero.");
                stack.push_back(left / right);
            }
        } else if (token == "neg") stack.push_back(-pop());
        else if (token == "abs") stack.push_back(std::abs(pop()));
        else if (token == "sqrt") stack.push_back(std::sqrt(std::max(0.0, pop())));
        else if (token == "max") { const double b = pop(); const double a = pop(); stack.push_back(std::max(a, b)); }
        else if (token == "min") { const double b = pop(); const double a = pop(); stack.push_back(std::min(a, b)); }
        else if (token == "dup") { if (stack.empty()) throw std::runtime_error("Calculator stack underflow."); stack.push_back(stack.back()); }
        else if (token == "pop") { (void)pop(); }
        else if (token == "exch") { const double b = pop(); const double a = pop(); stack.push_back(b); stack.push_back(a); }
        else throw std::runtime_error("Unsupported calculator operator.");
        if (stack.size() > limits_.maxStack) throw std::runtime_error("Calculator stack limit exceeded.");
    }
    if (stack.size() < outputCount) throw std::runtime_error("Calculator produced too few outputs.");
    return std::vector<double>(stack.end() - static_cast<std::ptrdiff_t>(outputCount), stack.end());
}

} // namespace CPPPdf
