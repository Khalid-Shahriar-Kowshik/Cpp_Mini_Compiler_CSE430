#include "iroptimizer.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_set>

std::string IROptimizer::trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(start, end - start);
}

std::vector<std::string> IROptimizer::splitWhitespace(const std::string& s) {
    std::istringstream iss(s);
    std::vector<std::string> out;
    std::string token;
    while (iss >> token) {
        out.push_back(token);
    }
    return out;
}

bool IROptimizer::isInteger(const std::string& s) {
    if (s.empty()) {
        return false;
    }
    size_t i = 0;
    if (s[0] == '-') {
        if (s.size() == 1) {
            return false;
        }
        i = 1;
    }
    for (; i < s.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) {
            return false;
        }
    }
    return true;
}

bool IROptimizer::isTempName(const std::string& s) {
    if (s.size() < 2 || s[0] != 't') {
        return false;
    }
    for (size_t i = 1; i < s.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) {
            return false;
        }
    }
    return true;
}

bool IROptimizer::isIdentifier(const std::string& s) {
    if (s.empty()) {
        return false;
    }
    if (std::isdigit(static_cast<unsigned char>(s[0]))) {
        return false;
    }
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
            return false;
        }
    }
    return true;
}

bool IROptimizer::isSimpleToken(const std::string& s) {
    if (s.empty()) {
        return false;
    }
    return s.find_first_of(" \t\r\n") == std::string::npos;
}

bool IROptimizer::isLiteralToken(const std::string& s) {
    if (isInteger(s)) {
        return true;
    }
    if (s.size() >= 2 && ((s.front() == '\'' && s.back() == '\'') ||
                          (s.front() == '"' && s.back() == '"'))) {
        return true;
    }
    return false;
}

bool IROptimizer::isCopySourceToken(const std::string& s) {
    return isIdentifier(s) || isLiteralToken(s);
}

bool IROptimizer::isCommutativeOp(const std::string& op) {
    static const std::unordered_set<std::string> k{
        "+", "*", "==", "!=", "&&", "||"
    };
    return k.find(op) != k.end();
}

bool IROptimizer::isPureBinaryOp(const std::string& op) {
    static const std::unordered_set<std::string> k{
        "+", "-", "*", "/", "%",
        "<", ">", "<=", ">=", "==", "!=",
        "&&", "||"
    };
    return k.find(op) != k.end();
}

bool IROptimizer::isMemoryLValue(const std::string& s) {
    return s.find('[') != std::string::npos;
}

std::string IROptimizer::stripTrailingPunctuation(const std::string& s) {
    if (s.empty()) {
        return s;
    }
    size_t end = s.size();
    while (end > 0 && (s[end - 1] == ',' || s[end - 1] == ';')) {
        --end;
    }
    return s.substr(0, end);
}

bool IROptimizer::isZero(const std::string& s) {
    return isInteger(s) && std::stoll(s) == 0;
}

bool IROptimizer::isOne(const std::string& s) {
    return isInteger(s) && std::stoll(s) == 1;
}

IROptimizer::IRLine IROptimizer::parseLine(const std::string& line) const {
    IRLine out;
    out.raw = line;

    std::string trimmed = trim(line);
    if (trimmed.empty()) {
        out.isControlFlow = true;
        return out;
    }

    if (!trimmed.empty() && trimmed.back() == ':') {
        out.isLabel = true;
        out.isControlFlow = true;
        return out;
    }

    std::vector<std::string> tokens = splitWhitespace(trimmed);
    if (!tokens.empty()) {
        const std::string& first = tokens[0];
        if (first == "IF_FALSE" || first == "GOTO" || first == "RETURN" ||
            first == "PRINT" || first == "PARAM" || first == "READ") {
            out.isControlFlow = true;
            return out;
        }
    }

    size_t eqPos = trimmed.find('=');
    if (eqPos == std::string::npos) {
        out.isControlFlow = true;
        return out;
    }

    std::string lhs = trim(trimmed.substr(0, eqPos));
    std::string rhs = trim(trimmed.substr(eqPos + 1));
    if (lhs.empty() || rhs.empty()) {
        out.isControlFlow = true;
        return out;
    }

    out.isAssignment = true;
    out.result = lhs;

    std::vector<std::string> rhsTokens = splitWhitespace(rhs);
    if (!rhsTokens.empty() && rhsTokens[0] == "CALL") {
        out.isCall = true;
        out.isControlFlow = true;
        return out;
    }

    if (rhsTokens.size() == 1) {
        out.arg1 = rhsTokens[0];
        out.isSimple = true;
    } else if (rhsTokens.size() == 3 && isPureBinaryOp(rhsTokens[1])) {
        out.arg1 = rhsTokens[0];
        out.op = rhsTokens[1];
        out.arg2 = rhsTokens[2];
        out.isSimple = true;
    }

    return out;
}

std::string IROptimizer::formatAssignment(const IRLine& line) const {
    if (line.op.empty()) {
        return line.result + " = " + line.arg1;
    }
    return line.result + " = " + line.arg1 + " " + line.op + " " + line.arg2;
}

std::string IROptimizer::renderLine(const IRLine& line) const {
    if (!line.isAssignment || !line.isSimple || !line.modified) {
        return line.raw;
    }
    return formatAssignment(line);
}

bool IROptimizer::applyConstantFolding(IRLine& line, std::string& before, std::string& after) {
    if (!line.isAssignment || !line.isSimple || line.isControlFlow || line.isCall) {
        return false;
    }
    if (line.op.empty()) {
        return false;
    }
    if (!isInteger(line.arg1) || !isInteger(line.arg2)) {
        return false;
    }

    long long a = std::stoll(line.arg1);
    long long b = std::stoll(line.arg2);
    long long result = 0;

    if (line.op == "+") {
        result = a + b;
    } else if (line.op == "-") {
        result = a - b;
    } else if (line.op == "*") {
        result = a * b;
    } else if (line.op == "/") {
        if (b == 0) {
            return false;
        }
        result = a / b;
    } else {
        return false;
    }

    before = formatAssignment(line);
    line.arg1 = std::to_string(result);
    line.arg2.clear();
    line.op.clear();
    line.modified = true;
    after = formatAssignment(line);
    return true;
}

bool IROptimizer::applyAlgebraicSimplification(IRLine& line, std::string& before, std::string& after) {
    if (!line.isAssignment || !line.isSimple || line.isControlFlow || line.isCall) {
        return false;
    }
    if (line.op.empty()) {
        return false;
    }

    bool changed = false;
    before = formatAssignment(line);

    if (line.op == "+") {
        if (isZero(line.arg2)) {
            line.op.clear();
            line.arg2.clear();
            changed = true;
        } else if (isZero(line.arg1)) {
            line.arg1 = line.arg2;
            line.op.clear();
            line.arg2.clear();
            changed = true;
        }
    } else if (line.op == "-") {
        if (isZero(line.arg2)) {
            line.op.clear();
            line.arg2.clear();
            changed = true;
        }
    } else if (line.op == "*") {
        if (isZero(line.arg1) || isZero(line.arg2)) {
            line.arg1 = "0";
            line.op.clear();
            line.arg2.clear();
            changed = true;
        } else if (isOne(line.arg2)) {
            line.op.clear();
            line.arg2.clear();
            changed = true;
        } else if (isOne(line.arg1)) {
            line.arg1 = line.arg2;
            line.op.clear();
            line.arg2.clear();
            changed = true;
        }
    } else if (line.op == "/") {
        if (isOne(line.arg2)) {
            line.op.clear();
            line.arg2.clear();
            changed = true;
        }
    }

    if (!changed) {
        return false;
    }

    line.modified = true;
    after = formatAssignment(line);
    return true;
}

bool IROptimizer::tryCopyPropagation(const IRLine& from, IRLine& to, std::string& before,
                                    std::string& after) {
    if (!from.isAssignment || !from.isSimple || from.isControlFlow || from.isCall) {
        return false;
    }
    if (!from.op.empty()) {
        return false;
    }
    if (!isTempName(from.result)) {
        return false;
    }
    if (!isCopySourceToken(from.arg1)) {
        return false;
    }
    if (!to.isAssignment || !to.isSimple || to.isControlFlow || to.isCall) {
        return false;
    }

    bool changed = false;
    before = formatAssignment(to);

    if (to.arg1 == from.result && to.arg1 != from.arg1) {
        to.arg1 = from.arg1;
        changed = true;
    }
    if (!to.op.empty() && to.arg2 == from.result && to.arg2 != from.arg1) {
        to.arg2 = from.arg1;
        changed = true;
    }

    if (!changed) {
        return false;
    }

    to.modified = true;
    after = formatAssignment(to);
    return true;
}

void IROptimizer::applyLocalValueNumbering(std::vector<IRLine>& lines,
                                           std::vector<std::string>& report,
                                           int& cseCount) {
    struct LVNState {
        std::unordered_map<std::string, int> varValue;
        std::unordered_map<std::string, int> constValue;
        std::unordered_map<std::string, int> exprValue;
        std::unordered_map<int, std::string> leader;
        int nextValue = 1;
    } state;

    auto reset = [&]() {
        state = LVNState{};
    };

    auto getValueNumber = [&](const std::string& token) -> int {
        if (isLiteralToken(token)) {
            auto it = state.constValue.find(token);
            if (it != state.constValue.end()) {
                return it->second;
            }
            int v = state.nextValue++;
            state.constValue[token] = v;
            return v;
        }

        auto it = state.varValue.find(token);
        if (it != state.varValue.end()) {
            return it->second;
        }
        int v = state.nextValue++;
        state.varValue[token] = v;
        state.leader[v] = token;
        return v;
    };

    auto setVarValue = [&](const std::string& name, int value) {
        auto it = state.varValue.find(name);
        if (it != state.varValue.end()) {
            int oldValue = it->second;
            auto leaderIt = state.leader.find(oldValue);
            if (leaderIt != state.leader.end() && leaderIt->second == name) {
                state.leader.erase(leaderIt);
            }
        }
        state.varValue[name] = value;
        state.leader[value] = name;
    };

    for (auto& line : lines) {
        if (line.isLabel || line.isControlFlow || line.isCall || !line.isAssignment || !line.isSimple) {
            reset();
            continue;
        }
        if (isMemoryLValue(line.result)) {
            reset();
            continue;
        }

        int value = 0;
        if (line.op.empty()) {
            if (isCopySourceToken(line.arg1)) {
                value = getValueNumber(line.arg1);
            } else {
                value = state.nextValue++;
            }
        } else if (isPureBinaryOp(line.op)) {
            int v1 = getValueNumber(line.arg1);
            int v2 = getValueNumber(line.arg2);
            if (isCommutativeOp(line.op) && v2 < v1) {
                std::swap(v1, v2);
            }
            std::string key = line.op + ":" + std::to_string(v1) + ":" + std::to_string(v2);
            auto it = state.exprValue.find(key);
            if (it != state.exprValue.end()) {
                value = it->second;
                auto leaderIt = state.leader.find(value);
                if (leaderIt != state.leader.end() && leaderIt->second != line.result) {
                    auto leaderValueIt = state.varValue.find(leaderIt->second);
                    if (leaderValueIt != state.varValue.end() && leaderValueIt->second == value) {
                        std::string before = formatAssignment(line);
                        line.op.clear();
                        line.arg2.clear();
                        line.arg1 = leaderIt->second;
                        line.modified = true;
                        std::string after = formatAssignment(line);
                        report.push_back("Local value numbering: " + before + " -> " + after);
                        ++cseCount;
                    }
                }
            } else {
                value = state.nextValue++;
                state.exprValue[key] = value;
            }
        } else {
            value = state.nextValue++;
        }

        setVarValue(line.result, value);
    }
}

void IROptimizer::applyLocalCopyPropagation(std::vector<IRLine>& lines,
                                            std::vector<std::string>& report,
                                            int& copyPropagationCount) {
    std::unordered_map<std::string, std::string> subst;

    auto reset = [&]() {
        subst.clear();
    };

    auto resolveSubst = [&](std::string& token) -> bool {
        auto it = subst.find(token);
        if (it == subst.end()) {
            return false;
        }
        std::string replacement = it->second;
        int guard = 0;
        while (guard < 8) {
            auto nextIt = subst.find(replacement);
            if (nextIt == subst.end()) {
                break;
            }
            replacement = nextIt->second;
            ++guard;
        }
        if (replacement != token) {
            token = replacement;
            return true;
        }
        return false;
    };

    for (auto& line : lines) {
        if (line.isLabel || line.isControlFlow || line.isCall || !line.isAssignment || !line.isSimple) {
            reset();
            continue;
        }

        bool changed = false;
        std::string before = formatAssignment(line);
        if (resolveSubst(line.arg1)) {
            changed = true;
        }
        if (!line.op.empty() && resolveSubst(line.arg2)) {
            changed = true;
        }
        if (changed) {
            line.modified = true;
            std::string after = formatAssignment(line);
            report.push_back("Copy propagation: " + before + " -> " + after);
            ++copyPropagationCount;
        }

        if (!line.result.empty()) {
            subst.erase(line.result);
            for (auto it = subst.begin(); it != subst.end();) {
                if (it->second == line.result) {
                    it = subst.erase(it);
                } else {
                    ++it;
                }
            }
        }

        if (line.op.empty() && isTempName(line.result) && isCopySourceToken(line.arg1)) {
            std::string source = line.arg1;
            auto it = subst.find(source);
            if (it != subst.end()) {
                source = it->second;
            }
            subst[line.result] = source;
        }

        if (isMemoryLValue(line.result)) {
            reset();
        }
    }
}

void IROptimizer::addUseIfVar(const std::string& token,
                             std::unordered_map<std::string, int>& uses) const {
    std::string cleaned = stripTrailingPunctuation(token);
    if (!isIdentifier(cleaned)) {
        return;
    }
    if (isInteger(cleaned)) {
        return;
    }
    uses[cleaned] += 1;
}

void IROptimizer::collectUseCounts(const std::vector<IRLine>& lines,
                                  std::unordered_map<std::string, int>& uses) const {
    for (size_t i = 0; i < lines.size(); ++i) {
        const IRLine& line = lines[i];

        if (line.isAssignment && line.isSimple) {
            addUseIfVar(line.arg1, uses);
            if (!line.op.empty()) {
                addUseIfVar(line.arg2, uses);
            }
        }

        if (!line.isControlFlow) {
            continue;
        }

        std::string trimmed = trim(line.raw);
        std::vector<std::string> tokens = splitWhitespace(trimmed);
        if (tokens.empty()) {
            continue;
        }

        const std::string& first = tokens[0];
        if (first == "IF_FALSE" && tokens.size() >= 2) {
            addUseIfVar(tokens[1], uses);
        } else if (first == "PRINT" && tokens.size() >= 2) {
            addUseIfVar(tokens[1], uses);
        } else if (first == "RETURN" && tokens.size() >= 2) {
            addUseIfVar(tokens[1], uses);
        } else if (first == "PARAM" && tokens.size() >= 2) {
            addUseIfVar(tokens[1], uses);
        }
    }
}

std::vector<std::string> IROptimizer::optimize(const std::vector<std::string>& ir,
                                              std::vector<std::string>& report) {
    report.clear();

    std::vector<IRLine> lines;
    lines.reserve(ir.size());
    for (size_t i = 0; i < ir.size(); ++i) {
        lines.push_back(parseLine(ir[i]));
    }

    int constantFoldingCount = 0;
    int algebraicCount = 0;
    int cseCount = 0;
    int copyPropagationCount = 0;
    int deadTempCount = 0;

    for (size_t i = 0; i < lines.size(); ++i) {
        std::string before;
        std::string after;

        if (applyConstantFolding(lines[i], before, after)) {
            report.push_back("Constant folding: " + before + " -> " + after);
            ++constantFoldingCount;
            continue;
        }

        if (applyAlgebraicSimplification(lines[i], before, after)) {
            report.push_back("Algebraic simplification: " + before + " -> " + after);
            ++algebraicCount;
        }
    }

    applyLocalValueNumbering(lines, report, cseCount);
    applyLocalCopyPropagation(lines, report, copyPropagationCount);

    std::unordered_map<std::string, int> uses;
    collectUseCounts(lines, uses);

    std::vector<std::string> optimized;
    optimized.reserve(lines.size());

    for (size_t i = 0; i < lines.size(); ++i) {
        const IRLine& line = lines[i];
        if (line.isAssignment && line.isSimple && !line.isCall && !line.isControlFlow &&
            isTempName(line.result)) {
            if (uses.find(line.result) == uses.end()) {
                report.push_back("Dead temporary removed: " + formatAssignment(line));
                ++deadTempCount;
                continue;
            }
        }
        optimized.push_back(renderLine(line));
    }

    int totalOptimizations = constantFoldingCount + algebraicCount + cseCount +
                             copyPropagationCount + deadTempCount;

    report.push_back("Summary:");
    report.push_back("Total IR instructions before optimization: " +
                     std::to_string(static_cast<int>(ir.size())));
    report.push_back("Total IR instructions after optimization: " +
                     std::to_string(static_cast<int>(optimized.size())));
    report.push_back("Total optimizations applied: " + std::to_string(totalOptimizations));
    report.push_back("Constant folding count: " + std::to_string(constantFoldingCount));
    report.push_back("Algebraic simplification count: " + std::to_string(algebraicCount));
    report.push_back("Local value numbering count: " + std::to_string(cseCount));
    report.push_back("Copy propagation count: " + std::to_string(copyPropagationCount));
    report.push_back("Dead temporary removal count: " + std::to_string(deadTempCount));

    return optimized;
}
