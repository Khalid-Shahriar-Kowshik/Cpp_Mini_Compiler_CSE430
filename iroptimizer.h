#ifndef IROPTIMIZER_H
#define IROPTIMIZER_H

#include <string>
#include <unordered_map>
#include <vector>

class IROptimizer {
public:

    std::vector<std::string> optimize(const std::vector<std::string>& ir,
                                      std::vector<std::string>& report);

private:
    struct IRLine {
        std::string raw;
        std::string result;
        std::string op;
        std::string arg1;
        std::string arg2;
        bool isAssignment = false;
        bool isControlFlow = false;
        bool isLabel = false;
        bool isCall = false;
        bool isSimple = false;
        bool modified = false;
    };

    static std::string trim(const std::string& s);
    static std::vector<std::string> splitWhitespace(const std::string& s);
    static bool isInteger(const std::string& s);
    static bool isTempName(const std::string& s);
    static bool isIdentifier(const std::string& s);
    static bool isSimpleToken(const std::string& s);
    static bool isLiteralToken(const std::string& s);
    static bool isCopySourceToken(const std::string& s);
    static bool isCommutativeOp(const std::string& op);
    static bool isPureBinaryOp(const std::string& op);
    static bool isMemoryLValue(const std::string& s);
    static std::string stripTrailingPunctuation(const std::string& s);
    static bool isZero(const std::string& s);
    static bool isOne(const std::string& s);

    IRLine parseLine(const std::string& line) const;
    std::string formatAssignment(const IRLine& line) const;
    std::string renderLine(const IRLine& line) const;

    bool applyConstantFolding(IRLine& line, std::string& before, std::string& after);
    bool applyAlgebraicSimplification(IRLine& line, std::string& before, std::string& after);
    bool tryCopyPropagation(const IRLine& from, IRLine& to, std::string& before, std::string& after);
    void applyLocalValueNumbering(std::vector<IRLine>& lines,
                                  std::vector<std::string>& report,
                                  int& cseCount);
    void applyLocalCopyPropagation(std::vector<IRLine>& lines,
                                   std::vector<std::string>& report,
                                   int& copyPropagationCount);

    void collectUseCounts(const std::vector<IRLine>& lines,
                          std::unordered_map<std::string, int>& uses) const;
    void addUseIfVar(const std::string& token,
                     std::unordered_map<std::string, int>& uses) const;
};

#endif
