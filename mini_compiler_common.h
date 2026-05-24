#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mc {

// ------------------------------
// constants
// ------------------------------
inline constexpr const char* kInputFile = "infile.txt";

inline constexpr const char* kOutTokens = "outfile_tokens.txt";
inline constexpr const char* kOutSymbolTable = "outfile_symboltable.txt";
inline constexpr const char* kOutParser = "outfile_parser.txt";
inline constexpr const char* kOutSemantic = "outfile_semantic.txt";
inline constexpr const char* kOutIr = "outfile_ir.txt";
inline constexpr const char* kOutIrUnoptimized = "outfile_ir_unoptimized.txt";
inline constexpr const char* kOutIrOptimized = "outfile_ir_optimized.txt";
inline constexpr const char* kOutOptimizationReport = "outfile_optimization_report.txt";
inline constexpr const char* kOutMachine = "outfile_machinecode.txt";
inline constexpr const char* kOutErrors = "outfile_errors.txt";

// ------------------------------
// source location
// ------------------------------
struct SourcePos {
    int line = 1;
    int col = 1;
};

inline std::string toString(const SourcePos& p) {
    std::ostringstream oss;
    oss << "line " << p.line << ", col " << p.col;
    return oss.str();
}

// ------------------------------
// error handling
// ------------------------------
enum class Phase { Lexical, Syntax, Semantic };

inline const char* phaseName(Phase p) {
    switch (p) {
        case Phase::Lexical: return "Lexical";
        case Phase::Syntax: return "Syntax";
        case Phase::Semantic: return "Semantic";
    }
    return "Unknown";
}

struct CompileError {
    Phase phase;
    SourcePos pos;
    std::string message;
    std::string context;
};

class ErrorReporter {
public:
    void add(Phase phase, SourcePos pos, std::string message, std::string context = {}) {
        errors_.push_back(CompileError{phase, pos, std::move(message), std::move(context)});
    }

    bool hasErrors() const { return !errors_.empty(); }

    const std::vector<CompileError>& all() const { return errors_; }

    void writeFile(const std::string& path) const {
        std::ofstream out(path);
        if (!out) return;
        for (const auto& e : errors_) {
            out << phaseName(e.phase) << " Error at " << toString(e.pos) << ":\n";
            out << e.message << "\n";
            if (!e.context.empty()) {
                out << "Context: " << e.context << "\n";
            }
            out << "\n";
        }
        if (errors_.empty()) {
            out << "No errors.\n";
        }
    }

private:
    std::vector<CompileError> errors_;
};

// ------------------------------
// utility functions
// ------------------------------
inline std::string readTextFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

inline void writeLines(const std::string& path, const std::vector<std::string>& lines) {
    std::ofstream out(path);
    for (const auto& l : lines) out << l << "\n";
}

inline std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

inline bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        auto ca = static_cast<unsigned char>(a[i]);
        auto cb = static_cast<unsigned char>(b[i]);
        if (std::toupper(ca) != std::toupper(cb)) return false;
    }
    return true;
}

inline std::string padRight(std::string_view s, size_t width) {
    std::string out(s);
    if (out.size() < width) out.append(width - out.size(), ' ');
    return out;
}

inline std::string trim(std::string_view sv) {
    size_t start = 0;
    while (start < sv.size() && std::isspace(static_cast<unsigned char>(sv[start]))) ++start;
    size_t end = sv.size();
    while (end > start && std::isspace(static_cast<unsigned char>(sv[end - 1]))) --end;
    return std::string(sv.substr(start, end - start));
}

} // namespace mc
