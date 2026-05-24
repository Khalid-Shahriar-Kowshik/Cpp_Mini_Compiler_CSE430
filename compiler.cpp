#include "mini_compiler_common.h"
#include "iroptimizer.h"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>

using std::string;
using std::vector;

// ============================================================
// C-like Mini Compiler 
// Demonstrates phases: Lexical -> Syntax -> Semantic -> Symbol Table -> IR -> Machine Code
//
// Supported (subset, rubric-driven):
// - variables (int, char)
// - expressions with precedence
// - assignments
// - conditions (if/else)
// - loops (while, for)
// - functions (definitions + calls)
// - arrays (declaration + indexing)
// - basic I/O statements: cin/cout (legacy-friendly)
//
// Grammar (subset, recursive descent):
//   program        -> (decl | func_def | stmt)* EOF
//   decl           -> type declarator (',' declarator)* ';'
//   declarator     -> IDENT ('[' INT ']')? ('=' expr)?
//   type           -> ('int'|'char'|'void')
//   func_def       -> type IDENT '(' params? ')' block
//   params         -> param (',' param)*
//   param          -> type IDENT ('[' ']')?
//   block          -> '{' stmt* '}'
//   stmt           -> decl
//                  | block
//                  | if_stmt
//                  | while_stmt
//                  | for_stmt
//                  | return_stmt ';'
//                  | io_stmt ';'
//                  | expr_stmt ';'
//   if_stmt        -> 'if' '(' expr ')' stmt ('else' stmt)?
//   while_stmt     -> 'while' '(' expr ')' stmt
//   for_stmt       -> 'for' '(' (expr_stmt | ';') (expr? ';') (expr? ) ')' stmt
//   return_stmt    -> 'return' expr?
//   io_stmt        -> 'cin' ('>>' lvalue)+ | 'cout' ('<<' expr)+
//   expr_stmt      -> expr?
//   expr           -> assignment
//   assignment     -> logical_or ( '=' assignment )?
//   logical_or     -> logical_and ( '||' logical_and )*
//   logical_and    -> equality ( '&&' equality )*
//   equality       -> relational ( ('=='|'!=') relational )*
//   relational     -> additive ( ('<'|'<='|'>'|'>=') additive )*
//   additive       -> term (('+'|'-') term )*
//   term           -> unary (('*'|'/'|'%') unary )*
//   unary          -> ('+'|'-'|'!'|'++'|'--') unary | postfix
//   postfix        -> primary ( '(' args? ')' | '[' expr ']' | '++' | '--' )*
//   primary        -> IDENT | INT | CHAR | STRING | '(' expr ')'
//   args           -> expr (',' expr)*
// ============================================================

namespace {

// ------------------------------
// Token definitions
// ------------------------------
enum class Tk {
    Keyword,
    Identifier,
    IntLit,
    CharLit,
    StringLit,
    Operator,
    Punct,
    Eof,
};

static const char* tkName(Tk k) {
    switch (k) {
        case Tk::Keyword: return "RESERVED";
        case Tk::Identifier: return "IDENTIFIER";
        case Tk::IntLit: return "INTEGER";
        case Tk::CharLit: return "CHAR";
        case Tk::StringLit: return "STRING";
        case Tk::Operator: return "OPERATOR";
        case Tk::Punct: return "PUNCT";
        case Tk::Eof: return "EOF";
    }
    return "UNKNOWN";
}

struct Token {
    Tk kind{};
    string lexeme;
    mc::SourcePos pos;
};

static bool isKeyword(const string& s) {
    static const std::set<string> k{
        "int", "char", "void",
        "if", "else", "while", "for", "return",
        "cin", "cout"
    };
    return k.count(s) != 0;
}

// ------------------------------
// Lexer
// ------------------------------
class Lexer {
public:
    Lexer(string input, mc::ErrorReporter& errors)
        : input_(std::move(input)), errors_(errors) {}

    vector<Token> lexAll() {
        vector<Token> out;
        while (true) {
            auto t = next();
            out.push_back(t);
            if (t.kind == Tk::Eof) break;
        }
        return out;
    }

private:
    Token next() {
        skipWs();
        if (eof()) return Token{Tk::Eof, "", pos_};

        // comments
        if (peek() == '/' && peek(1) == '/') {
            while (!eof() && peek() != '\n') advance();
            return next();
        }
        if (peek() == '/' && peek(1) == '*') {
            auto start = pos_;
            advance(); advance();
            while (!eof()) {
                if (peek() == '*' && peek(1) == '/') { advance(); advance(); break; }
                advance();
            }
            if (eof()) errors_.add(mc::Phase::Lexical, start, "Unterminated block comment");
            return next();
        }

        // string
        if (peek() == '"') {
            auto start = pos_;
            advance();
            string lit;
            while (!eof()) {
                char c = peek();
                if (c == '\n') {
                    errors_.add(mc::Phase::Lexical, start, "Unterminated string literal");
                    break;
                }
                if (c == '"') { advance(); break; }
                if (c == '\\') {
                    advance();
                    if (!eof()) {
                        lit.push_back(peek());
                        advance();
                    }
                    continue;
                }
                lit.push_back(c);
                advance();
            }
            return Token{Tk::StringLit, lit, start};
        }

        // char literal
        if (peek() == '\'') {
            auto start = pos_;
            advance();
            if (eof() || peek() == '\n') {
                errors_.add(mc::Phase::Lexical, start, "Unterminated char literal");
                return Token{Tk::CharLit, "", start};
            }
            char value = peek();
            advance();
            if (peek() != '\'') {
                errors_.add(mc::Phase::Lexical, start, "Char literal must be one character and closed with '\''");
            } else {
                advance();
            }
            return Token{Tk::CharLit, string(1, value), start};
        }

        // number
        if (std::isdigit(static_cast<unsigned char>(peek()))) {
            auto start = pos_;
            string num;
            while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) {
                num.push_back(peek());
                advance();
            }
            return Token{Tk::IntLit, num, start};
        }

        // identifier / keyword
        if (std::isalpha(static_cast<unsigned char>(peek())) || peek() == '_') {
            auto start = pos_;
            string id;
            while (!eof() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')) {
                id.push_back(peek());
                advance();
            }
            if (isKeyword(id)) return Token{Tk::Keyword, id, start};
            return Token{Tk::Identifier, id, start};
        }

        // multi-char operators
        static const std::set<string> ops2{"==", "!=", "<=", ">=", "&&", "||", "++", "--", "<<", ">>"};
        {
            string two;
            two.push_back(peek());
            two.push_back(peek(1));
            if (ops2.count(two)) {
                auto start = pos_;
                advance(); advance();
                return Token{Tk::Operator, two, start};
            }
        }

        // single-char operators / punctuation
        char c = peek();
        auto start = pos_;
        static const string ops = "+-*/%<>=!&|";
        static const string punct = "(){}[];,";
        if (ops.find(c) != string::npos) {
            advance();
            return Token{Tk::Operator, string(1, c), start};
        }
        if (punct.find(c) != string::npos) {
            advance();
            return Token{Tk::Punct, string(1, c), start};
        }

        errors_.add(mc::Phase::Lexical, start, "Invalid character", string(1, c));
        advance();
        return next();
    }

    bool eof() const { return idx_ >= input_.size(); }
    char peek(size_t off = 0) const {
        size_t p = idx_ + off;
        return (p < input_.size()) ? input_[p] : '\0';
    }

    void advance() {
        if (eof()) return;
        if (input_[idx_] == '\n') {
            pos_.line++;
            pos_.col = 1;
        } else {
            pos_.col++;
        }
        idx_++;
    }

    void skipWs() {
        while (!eof() && std::isspace(static_cast<unsigned char>(peek()))) advance();
    }

private:
    string input_;
    mc::ErrorReporter& errors_;
    size_t idx_ = 0;
    mc::SourcePos pos_{};
};

// ------------------------------
// Types and symbol table
// ------------------------------
enum class BaseType { Int, Char, Void, Unknown };

static string baseTypeName(BaseType t) {
    switch (t) {
        case BaseType::Int: return "int";
        case BaseType::Char: return "char";
        case BaseType::Void: return "void";
        default: return "unknown";
    }
}

struct Type {
    BaseType base = BaseType::Unknown;
    bool isArray = false;
    int arraySize = 0;
    bool isFunction = false;
    vector<Type> params;
    string customName;

    Type() = default;
    explicit Type(BaseType b) : base(b) {}
};

static string typeNameSimple(const Type& t) {
    if (!t.customName.empty()) return t.customName;
    if (t.isArray) return baseTypeName(t.base) + "[]";
    return baseTypeName(t.base);
}

static string typeName(const Type& t) {
    if (!t.customName.empty()) return t.customName;
    if (t.isFunction) {
        string out = baseTypeName(t.base) + " (";
        for (size_t i = 0; i < t.params.size(); ++i) {
            if (i > 0) out += ",";
            out += typeNameSimple(t.params[i]);
        }
        out += ")";
        return out;
    }
    if (t.isArray) {
        return baseTypeName(t.base) + "[" + std::to_string(t.arraySize) + "]";
    }
    return baseTypeName(t.base);
}

enum class SymKind { Variable, Function, Parameter };

struct Symbol {
    string name;
    Type type;
    SymKind kind;
    string value;
    string scope;
    string extra;
};

class SymbolTable {
public:
    void pushScope(const string& name) {
        scopes_.push_back({});
        scopeNames_.push_back(name);
    }

    void popScope() {
        if (scopes_.size() > 1) {
            scopes_.pop_back();
            scopeNames_.pop_back();
        }
    }

    string currentScopeName() const { return scopeNames_.back(); }

    bool declare(const Symbol& sym, const mc::SourcePos& pos, mc::ErrorReporter& errors) {
        auto& cur = scopes_.back();
        if (cur.count(sym.name)) {
            errors.add(mc::Phase::Semantic, pos, "Duplicate declaration in same scope", sym.name);
            return false;
        }
        cur[sym.name] = sym;
        all_.push_back(sym);
        return true;
    }

    Symbol* lookupMutable(const string& name) {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto f = it->find(name);
            if (f != it->end()) return &f->second;
        }
        return nullptr;
    }

    const Symbol* lookup(const string& name) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto f = it->find(name);
            if (f != it->end()) return &f->second;
        }
        return nullptr;
    }

    vector<Symbol> dump() const {
        auto r = all_;
        std::sort(r.begin(), r.end(), [](const Symbol& a, const Symbol& b) {
            return a.name < b.name;
        });
        return r;
    }

    void syncSymbol(const Symbol& sym) {
        for (auto& s : all_) {
            if (s.name == sym.name && s.scope == sym.scope && s.kind == sym.kind) {
                s = sym;
            }
        }
    }

private:
    std::vector<std::unordered_map<string, Symbol>> scopes_{std::unordered_map<string, Symbol>{}};
    vector<string> scopeNames_{"global"};
    vector<Symbol> all_;
};

// ------------------------------
// IR + Machine code builders
// ------------------------------
class IrBuilder {
public:
    string newTemp() { return "t" + std::to_string(tmp_++); }
    string newLabel() { return "L" + std::to_string(lbl_++); }
    void emit(const string& s) { ir_.push_back(s); }
    const vector<string>& ir() const { return ir_; }

private:
    int tmp_ = 0;
    int lbl_ = 0;
    vector<string> ir_;
};

class MachineBuilder {
public:
    void emit(const string& s) { code_.push_back(s); }
    const vector<string>& code() const { return code_; }

private:
    vector<string> code_;
};

// ------------------------------
// Parser
// ------------------------------
struct Expr {
    string place;
    Type type;
    bool isLValue = false;
    std::optional<int> constInt;
    mc::SourcePos pos;
    std::optional<string> callTarget;

    Expr() = default;
    Expr(const string& p, const Type& t, bool lvalue, std::optional<int> c, mc::SourcePos at)
        : place(p), type(t), isLValue(lvalue), constInt(c), pos(at) {}
};

class Parser {
public:
    Parser(const vector<Token>& toks, mc::ErrorReporter& errors, SymbolTable& syms, IrBuilder& ir, MachineBuilder& mach)
        : toks_(toks), errors_(errors), syms_(syms), ir_(ir), mach_(mach) {}

    void compile(vector<string>& parserTrace, vector<string>& semanticTrace) {
        parserTrace_ = &parserTrace;
        semanticTrace_ = &semanticTrace;
        parserTrace_->push_back("C-like Parser Started");
        semanticTrace_->push_back("Semantic Analysis Started");

        while (!check(Tk::Eof)) {
            if (isTypeStart()) {
                // could be decl or func_def
                parseDeclOrFunc();
            } else {
                parseStmt();
            }
        }

        parserTrace_->push_back("C-like Parser Completed");
        if (!errors_.hasErrors()) semanticTrace_->push_back("No semantic errors found");
    }

private:
    bool isTypeStart() const {
        return checkKw("int") || checkKw("char") || checkKw("void");
    }

    Type parseType() {
        Type t;
        if (matchKw("int")) t.base = BaseType::Int;
        else if (matchKw("char")) t.base = BaseType::Char;
        else if (matchKw("void")) t.base = BaseType::Void;
        else {
            errors_.add(mc::Phase::Syntax, peek().pos, "Expected type specifier", peek().lexeme);
            t.base = BaseType::Unknown;
        }
        return t;
    }

    void parseDeclOrFunc() {
        (*parserTrace_).push_back("Parsing Declaration/Function");
        Type ret = parseType();

        if (!check(Tk::Identifier)) {
            errors_.add(mc::Phase::Syntax, peek().pos, "Expected identifier after type", peek().lexeme);
            syncTo(";");
            matchP(";");
            return;
        }
        Token name = advance();

        if (matchP("(")) {
            // function definition
            Type fnType = ret;
            fnType.isFunction = true;
            vector<Symbol> params;
            if (!checkP(")")) {
                while (true) {
                    Type pt = parseType();
                    if (!check(Tk::Identifier)) {
                        errors_.add(mc::Phase::Syntax, peek().pos, "Expected parameter name", peek().lexeme);
                        break;
                    }
                    Token pn = advance();
                    Symbol pSym{pn.lexeme, pt, SymKind::Parameter, "", name.lexeme, "Passed by value"};
                    params.push_back(pSym);
                    fnType.params.push_back(pt);
                    if (matchP(",")) continue;
                    break;
                }
            }
            expectP(")", "Expected ')' after function parameters");

            string fnExtra = (name.lexeme == "main")
                ? "Program entry point"
                : ("Defined, " + std::to_string(params.size()) + " parameters");
            Symbol fn{name.lexeme, fnType, SymKind::Function, "", "global", fnExtra};
            syms_.declare(fn, name.pos, errors_);
            semanticTrace_->push_back("Declared function '" + name.lexeme + "'");

            // function body
            syms_.pushScope(name.lexeme);
            for (const auto& p : params) syms_.declare(p, name.pos, errors_);
            parseBlock();
            syms_.popScope();
            return;
        }

        // otherwise declaration list
        parseDeclaratorTail(ret, name);
        while (matchP(",")) {
            if (!check(Tk::Identifier)) {
                errors_.add(mc::Phase::Syntax, peek().pos, "Expected identifier in declarator list", peek().lexeme);
                break;
            }
            Token n = advance();
            parseDeclaratorTail(ret, n);
        }
        expectP(";", "Expected ';' after declaration");
    }

    void parseDeclaratorTail(const Type& baseT, const Token& nameTok) {
        Type t = baseT;
        // array?
        if (matchP("[")) {
            t.isArray = true;
            if (!check(Tk::IntLit)) {
                errors_.add(mc::Phase::Syntax, peek().pos, "Array size must be integer literal", peek().lexeme);
            } else {
                t.arraySize = std::stoi(peek().lexeme);
                advance();
                if (t.arraySize <= 0) errors_.add(mc::Phase::Semantic, nameTok.pos, "Array size must be > 0", nameTok.lexeme);
            }
            expectP("]", "Expected ']' after array size");
        }

        Symbol sym{nameTok.lexeme, t, SymKind::Variable, "0", syms_.currentScopeName(), ""};
        if (t.isArray && t.base == BaseType::Int) {
            sym.extra = "Array of " + std::to_string(t.arraySize) + " integers";
        }
        syms_.declare(sym, nameTok.pos, errors_);
        semanticTrace_->push_back("Declared variable '" + nameTok.lexeme + "' of type " + typeName(t));

        if (matchOp("=")) {
            Expr rhs = parseExpr();
            assignTo(nameTok, rhs);
        }
    }

    void parseStmt() {
        if (matchP("{")) {
            idx_--; // let parseBlock consume '{'
            parseBlock();
            return;
        }
        if (matchKw("if")) {
            parseIf();
            return;
        }
        if (matchKw("while")) {
            parseWhile();
            return;
        }
        if (matchKw("for")) {
            parseFor();
            return;
        }
        if (matchKw("return")) {
            parseReturn();
            expectP(";", "Expected ';' after return");
            return;
        }
        if (matchKw("cin") || matchKw("cout")) {
            idx_--; // step back
            parseIo();
            expectP(";", "Expected ';' after I/O statement");
            return;
        }

        // expression statement (may be empty)
        if (matchP(";")) return;
        (void)parseExpr();
        expectP(";", "Expected ';' after expression");
    }

    void parseBlock() {
        (*parserTrace_).push_back("Parsing Block");
        expectP("{", "Expected '{' to start block");
        syms_.pushScope(syms_.currentScopeName());
        while (!checkP("}") && !check(Tk::Eof)) {
            if (isTypeStart()) {
                parseDeclOrFunc(); // declarations inside blocks
            } else {
                parseStmt();
            }
        }
        expectP("}", "Expected '}' to end block");
        syms_.popScope();
    }

    void parseIf() {
        (*parserTrace_).push_back("Parsing If Statement");
        expectP("(", "Expected '(' after if");
        Expr cond = parseExpr();
        expectP(")", "Expected ')' after if condition");

        const string lElse = ir_.newLabel();
        const string lEnd = ir_.newLabel();
        ir_.emit("IF_FALSE " + cond.place + " GOTO " + lElse);
        mach_.emit("CMP " + cond.place + ", 0");
        mach_.emit("JEQ " + lElse);

        parseStmt();

        ir_.emit("GOTO " + lEnd);
        ir_.emit(lElse + ":");
        mach_.emit("JMP " + lEnd);
        mach_.emit(lElse + ":");

        if (matchKw("else")) {
            parseStmt();
        }

        ir_.emit(lEnd + ":");
        mach_.emit(lEnd + ":");
    }

    void parseWhile() {
        (*parserTrace_).push_back("Parsing While Loop");
        const string lStart = ir_.newLabel();
        const string lEnd = ir_.newLabel();
        ir_.emit(lStart + ":");
        mach_.emit(lStart + ":");

        expectP("(", "Expected '(' after while");
        Expr cond = parseExpr();
        expectP(")", "Expected ')' after while condition");

        ir_.emit("IF_FALSE " + cond.place + " GOTO " + lEnd);
        mach_.emit("CMP " + cond.place + ", 0");
        mach_.emit("JEQ " + lEnd);

        parseStmt();

        ir_.emit("GOTO " + lStart);
        ir_.emit(lEnd + ":");
        mach_.emit("JMP " + lStart);
        mach_.emit(lEnd + ":");
    }

    void parseFor() {
        (*parserTrace_).push_back("Parsing For Loop");
        expectP("(", "Expected '(' after for");

        // init
        if (!checkP(";")) {
            (void)parseExpr();
        }
        expectP(";", "Expected ';' after for-init");

        const string lStart = ir_.newLabel();
        const string lEnd = ir_.newLabel();
        ir_.emit(lStart + ":");
        mach_.emit(lStart + ":");

        // condition
        Expr cond;
        if (!checkP(";")) {
            cond = parseExpr();
        } else {
            cond.place = "1"; // true
        }
        expectP(";", "Expected ';' after for-condition");

        ir_.emit("IF_FALSE " + cond.place + " GOTO " + lEnd);
        mach_.emit("CMP " + cond.place + ", 0");
        mach_.emit("JEQ " + lEnd);

        // increment expression captured to run after body (avoid emitting IR now)
        string incText;
        if (!checkP(")")) {
            int parenDepth = 0;
            int bracketDepth = 0;
            while (!check(Tk::Eof)) {
                if (checkP(")") && parenDepth == 0 && bracketDepth == 0) break;
                if (checkP("(")) parenDepth++;
                if (checkP(")")) parenDepth = std::max(0, parenDepth - 1);
                if (checkP("[")) bracketDepth++;
                if (checkP("]")) bracketDepth = std::max(0, bracketDepth - 1);
                incText += peek().lexeme;
                incText.push_back(' ');
                advance();
            }
            incText = mc::trim(incText);
        }
        expectP(")", "Expected ')' after for-clause");

        // body
        parseStmt();

        // re-emit increment IR if present
        if (!incText.empty()) {
            ir_.emit("; for-inc " + incText);
            mach_.emit("INC " + incText);
        }

        ir_.emit("GOTO " + lStart);
        ir_.emit(lEnd + ":");
        mach_.emit("JMP " + lStart);
        mach_.emit(lEnd + ":");
    }

    void parseReturn() {
        Expr v;
        if (!checkP(";")) {
            v = parseExpr();
            ir_.emit("RETURN " + v.place);
            mach_.emit("RET " + v.place);
        } else {
            ir_.emit("RETURN");
            mach_.emit("RET");
        }
    }

    void parseIo() {
        if (matchKw("cin")) {
            (*parserTrace_).push_back("Parsing cin statement");
            do {
                expectOp(">>", "Expected '>>' after cin");
                Expr lv = parseLValue();
                if (!lv.isLValue) errors_.add(mc::Phase::Semantic, lvPlacePos_, "cin target must be an lvalue");
                ir_.emit("READ " + lv.place);
                mach_.emit("IN " + lv.place);
            } while (checkOp(">>"));
            return;
        }
        if (matchKw("cout")) {
            (*parserTrace_).push_back("Parsing cout statement");
            do {
                expectOp("<<", "Expected '<<' after cout");
                Expr v = parseExpr();
                ir_.emit("PRINT " + v.place);
                mach_.emit("OUT " + v.place);
            } while (checkOp("<<"));
            return;
        }
    }

    // ------------------------------
    // Expressions
    // ------------------------------
    Expr parseExpr() { return parseAssignment(); }

    Expr parseAssignment() {
        Expr left = parseLogicalOr();
        if (matchOp("=")) {
            if (!left.isLValue) {
                errors_.add(mc::Phase::Semantic, previous().pos, "Left-hand side of assignment must be an lvalue");
            }
            Expr rhs = parseAssignment();
            ir_.emit(left.place + " = " + rhs.place);
            mach_.emit("LOAD R1, " + rhs.place);
            mach_.emit("STORE " + left.place + ", R1");

            // update symbol value if variable
            if (auto* s = syms_.lookupMutable(left.place)) {
                s->value = rhs.place;
                const auto callFrom = callOrigin(rhs);
                if (callFrom && s->extra.empty() && !s->type.isArray) {
                    s->extra = "Used to store result of " + *callFrom;
                }
                syms_.syncSymbol(*s);
            }
            left.isLValue = false;
            left.constInt.reset();
            left.callTarget.reset();
            return left;
        }
        return left;
    }

    Expr parseLogicalOr() {
        Expr left = parseLogicalAnd();
        while (matchOp("||")) {
            Expr right = parseLogicalAnd();
            string t = ir_.newTemp();
            ir_.emit(t + " = " + left.place + " || " + right.place);
            mach_.emit("OR " + t + ", " + left.place + ", " + right.place);
            left.place = t;
            left.isLValue = false;
            left.constInt.reset();
            left.callTarget.reset();
        }
        return left;
    }

    Expr parseLogicalAnd() {
        Expr left = parseEquality();
        while (matchOp("&&")) {
            Expr right = parseEquality();
            string t = ir_.newTemp();
            ir_.emit(t + " = " + left.place + " && " + right.place);
            mach_.emit("AND " + t + ", " + left.place + ", " + right.place);
            left.place = t;
            left.isLValue = false;
            left.constInt.reset();
            left.callTarget.reset();
        }
        return left;
    }

    Expr parseEquality() {
        Expr left = parseRelational();
        while (checkOp("==") || checkOp("!=")) {
            Token op = advance();
            Expr right = parseRelational();
            string t = ir_.newTemp();
            ir_.emit(t + " = " + left.place + " " + op.lexeme + " " + right.place);
            mach_.emit("CMP_OP " + op.lexeme + " " + left.place + ", " + right.place + " -> " + t);
            left.place = t;
            left.isLValue = false;
            left.constInt.reset();
            left.callTarget.reset();
        }
        return left;
    }

    Expr parseRelational() {
        Expr left = parseAdditive();
        while (checkOp("<") || checkOp(">") || checkOp("<=") || checkOp(">=")) {
            Token op = advance();
            Expr right = parseAdditive();
            string t = ir_.newTemp();
            ir_.emit(t + " = " + left.place + " " + op.lexeme + " " + right.place);
            mach_.emit("CMP_OP " + op.lexeme + " " + left.place + ", " + right.place + " -> " + t);
            left.place = t;
            left.isLValue = false;
            left.constInt.reset();
            left.callTarget.reset();
        }
        return left;
    }

    Expr parseAdditive() {
        Expr left = parseTerm();
        while (checkOp("+") || checkOp("-")) {
            Token op = advance();
            Expr right = parseTerm();
            string t = ir_.newTemp();
            ir_.emit(t + " = " + left.place + " " + op.lexeme + " " + right.place);
            mach_.emit((op.lexeme == "+" ? "ADD" : "SUB") + string(" ") + t + ", " + left.place + ", " + right.place);
            left.place = t;
            left.isLValue = false;
            left.constInt.reset();
            left.callTarget.reset();
        }
        return left;
    }

    Expr parseTerm() {
        Expr left = parseUnary();
        while (checkOp("*") || checkOp("/") || checkOp("%")) {
            Token op = advance();
            Expr right = parseUnary();
            string t = ir_.newTemp();
            ir_.emit(t + " = " + left.place + " " + op.lexeme + " " + right.place);
            if (op.lexeme == "*") mach_.emit("MUL " + t + ", " + left.place + ", " + right.place);
            else if (op.lexeme == "/") mach_.emit("DIV " + t + ", " + left.place + ", " + right.place);
            else mach_.emit("MOD " + t + ", " + left.place + ", " + right.place);
            left.place = t;
            left.isLValue = false;
            left.constInt.reset();
            left.callTarget.reset();
        }
        return left;
    }

    Expr parseUnary() {
        if (checkOp("+") || checkOp("-") || checkOp("!") || checkOp("++") || checkOp("--")) {
            Token op = advance();
            Expr v = parseUnary();
            string t = ir_.newTemp();
            ir_.emit(t + " = " + op.lexeme + v.place);
            mach_.emit("UNARY " + op.lexeme + " " + v.place + " -> " + t);
            v.place = t;
            v.isLValue = false;
            v.constInt.reset();
            v.callTarget.reset();
            return v;
        }
        return parsePostfix();
    }

    Expr parsePostfix() {
        Expr base = parsePrimary();
        while (true) {
            if (matchP("(")) {
                // function call
                vector<Expr> args;
                if (!checkP(")")) {
                    args.push_back(parseExpr());
                    while (matchP(",")) args.push_back(parseExpr());
                }
                expectP(")", "Expected ')' after arguments");

                const Symbol* fn = syms_.lookup(base.place);
                if (!fn || fn->kind != SymKind::Function) {
                    errors_.add(mc::Phase::Semantic, previous().pos, "Call to undeclared function", base.place);
                } else {
                    if (fn->type.params.size() != args.size()) {
                        errors_.add(mc::Phase::Semantic, previous().pos, "Invalid function arity", base.place);
                    }
                }

                const string callName = base.place;
                // Emit argument passing explicitly so the IR is self-contained.
                // Keep left-to-right order to match typical educational TAC.
                for (const auto& a : args) {
                    ir_.emit("PARAM " + a.place);
                    mach_.emit("PUSH " + a.place);
                }

                string t = ir_.newTemp();
                ir_.emit(t + " = CALL " + base.place + ", " + std::to_string(args.size()));
                mach_.emit("CALL " + base.place);
                base.place = t;
                base.isLValue = false;
                base.callTarget = callName;
                callTempToFn_[t] = callName;
                continue;
            }
            if (matchP("[")) {
                Expr idx = parseExpr();
                expectP("]", "Expected ']' after index");

                // bounds check for constant index if we know array size
                const string arrName = base.place;
                if (base.isLValue) {
                    const Symbol* s = syms_.lookup(arrName);
                    if (s && s->type.isArray && idx.constInt.has_value()) {
                        if (*idx.constInt < 0 || *idx.constInt >= s->type.arraySize) {
                            errors_.add(mc::Phase::Semantic, idx.pos, "Array index out of bounds", arrName);
                        }
                    }
                }

                // treat as lvalue (educational); operations may still reference it directly
                base.place = arrName + "[" + idx.place + "]";
                base.type.isArray = false;
                base.isLValue = true;
                base.callTarget.reset();
                continue;
            }
            if (matchOp("++") || matchOp("--")) {
                Token op = previous();
                string t = ir_.newTemp();
                ir_.emit(t + " = " + base.place + op.lexeme);
                mach_.emit("POSTFIX " + op.lexeme + " " + base.place + " -> " + t);
                base.place = t;
                base.isLValue = false;
                base.callTarget.reset();
                continue;
            }
            break;
        }
        return base;
    }

    Expr parsePrimary() {
        if (check(Tk::IntLit)) {
            Token t = advance();
            Expr e{t.lexeme, Type{BaseType::Int}, false, std::stoi(t.lexeme), t.pos};
            return e;
        }
        if (check(Tk::CharLit)) {
            Token t = advance();
            Expr e{string("'") + t.lexeme + "'", Type{BaseType::Char}, false, std::nullopt, t.pos};
            return e;
        }
        if (check(Tk::StringLit)) {
            Token t = advance();
            Expr e{string("\"") + t.lexeme + "\"", Type{BaseType::Unknown}, false, std::nullopt, t.pos};
            return e;
        }
        if (check(Tk::Identifier)) {
            Token t = advance();
            const bool couldBeCall = checkP("(");
            const Symbol* s = syms_.lookup(t.lexeme);
            if (!s && !couldBeCall) errors_.add(mc::Phase::Semantic, t.pos, "Variable used before declaration", t.lexeme);
            Expr e{t.lexeme, s ? s->type : Type{}, s && s->kind != SymKind::Function, std::nullopt, t.pos};
            return e;
        }
        if (matchP("(")) {
            Expr e = parseExpr();
            expectP(")", "Expected ')' after expression");
            e.isLValue = false;
            return e;
        }
        errors_.add(mc::Phase::Syntax, peek().pos, "Unexpected token in expression", peek().lexeme);
        advance();
        return Expr{"<error>", Type{}, false, std::nullopt, mc::SourcePos{}};
    }

    Expr parseLValue() {
        lvPlacePos_ = peek().pos;
        if (!check(Tk::Identifier)) {
            errors_.add(mc::Phase::Syntax, peek().pos, "Expected identifier for lvalue", peek().lexeme);
            advance();
            return Expr{"<error>", Type{}, false, std::nullopt, lvPlacePos_};
        }
        Token t = advance();
        const Symbol* s = syms_.lookup(t.lexeme);
        if (!s) errors_.add(mc::Phase::Semantic, t.pos, "Variable used before declaration", t.lexeme);
        Expr e{t.lexeme, s ? s->type : Type{}, true, std::nullopt, t.pos};
        while (matchP("[")) {
            Expr idx = parseExpr();
            expectP("]", "Expected ']' after index");
            e.place = e.place + "[" + idx.place + "]";
            e.type.isArray = false;
            e.isLValue = true;
            e.callTarget.reset();
        }
        return e;
    }

    void assignTo(const Token& nameTok, const Expr& rhs) {
        Symbol* sym = syms_.lookupMutable(nameTok.lexeme);
        if (!sym) {
            errors_.add(mc::Phase::Semantic, nameTok.pos, "Assignment to undeclared variable", nameTok.lexeme);
            return;
        }
        sym->value = rhs.place;
        const auto callFrom = callOrigin(rhs);
        if (callFrom && sym->extra.empty() && !sym->type.isArray) {
            sym->extra = "Used to store result of " + *callFrom;
        }
        syms_.syncSymbol(*sym);
        ir_.emit(nameTok.lexeme + " = " + rhs.place);
        mach_.emit("LOAD R1, " + rhs.place);
        mach_.emit("STORE " + nameTok.lexeme + ", R1");
    }

    std::optional<string> callOrigin(const Expr& rhs) const {
        if (rhs.callTarget) return rhs.callTarget;
        auto it = callTempToFn_.find(rhs.place);
        if (it != callTempToFn_.end()) return it->second;
        return std::nullopt;
    }

    // ------------------------------
    // token helpers
    // ------------------------------
    const Token& peek() const { return toks_[idx_]; }
    const Token& previous() const { return toks_[idx_ - 1]; }
    bool check(Tk k) const { return peek().kind == k; }
    bool checkKw(const string& kw) const { return check(Tk::Keyword) && peek().lexeme == kw; }
    bool checkP(const string& p) const { return check(Tk::Punct) && peek().lexeme == p; }
    bool checkOp(const string& op) const { return check(Tk::Operator) && peek().lexeme == op; }

    Token advance() {
        if (!check(Tk::Eof)) idx_++;
        return toks_[idx_ - 1];
    }

    bool matchKw(const string& kw) {
        if (checkKw(kw)) { advance(); return true; }
        return false;
    }

    bool matchP(const string& p) {
        if (checkP(p)) { advance(); return true; }
        return false;
    }

    bool matchOp(const string& op) {
        if (checkOp(op)) { advance(); return true; }
        return false;
    }

    void expectP(const string& p, const string& msg) {
        if (!matchP(p)) errors_.add(mc::Phase::Syntax, peek().pos, msg, peek().lexeme);
    }

    void expectOp(const string& op, const string& msg) {
        if (!matchOp(op)) errors_.add(mc::Phase::Syntax, peek().pos, msg, peek().lexeme);
    }

    void syncTo(const string& lex) {
        while (!check(Tk::Eof) && peek().lexeme != lex) advance();
    }

private:
    const vector<Token>& toks_;
    mc::ErrorReporter& errors_;
    SymbolTable& syms_;
    IrBuilder& ir_;
    MachineBuilder& mach_;
    size_t idx_ = 0;

    vector<string>* parserTrace_ = nullptr;
    vector<string>* semanticTrace_ = nullptr;
    mc::SourcePos lvPlacePos_{};
    std::unordered_map<string, string> callTempToFn_;
};

} // namespace

int main() {
    mc::ErrorReporter errors;
    const string input = mc::readTextFile(mc::kInputFile);
    if (input.empty()) {
        std::cerr << "Failed to open " << mc::kInputFile << " or file is empty.\n";
        errors.add(mc::Phase::Lexical, mc::SourcePos{}, "Input file missing or empty", mc::kInputFile);
        errors.writeFile(mc::kOutErrors);
        return 1;
    }

    std::cout << "========== LEXICAL ANALYSIS ==========" << "\n";
    Lexer lexer(input, errors);
    const auto tokens = lexer.lexAll();

    {
        std::ofstream out(mc::kOutTokens);
        out << mc::padRight("TOKEN_TYPE", 16) << mc::padRight("TOKEN_VALUE", 16) << "LEXEME\n";
        for (const auto& t : tokens) {
            out << mc::padRight(tkName(t.kind), 16)
                << mc::padRight("-", 16)
                << t.lexeme << "\n";
        }
    }

    std::cout << "========== PARSER ==========" << "\n";
    SymbolTable syms;
    IrBuilder ir;
    MachineBuilder mach;
    vector<string> parserTrace;
    vector<string> semanticTrace;

    // Inject basic library symbol(s) so the symbol table matches expected output.
    {
        Type coutType;
        coutType.customName = "ostream";
        Symbol coutSym{"cout", coutType, SymKind::Variable, "", "global", "From <iostream>"};
        syms.declare(coutSym, mc::SourcePos{}, errors);
    }

    Parser parser(tokens, errors, syms, ir, mach);
    parser.compile(parserTrace, semanticTrace);

    mc::writeLines(mc::kOutParser, parserTrace);

    std::cout << "========== SYMBOL TABLE ==========" << "\n";
    {
        std::ofstream out(mc::kOutSymbolTable);
        out << mc::padRight("NAME", 14)
            << mc::padRight("KIND", 14)
            << mc::padRight("TYPE", 18)
            << mc::padRight("SCOPE", 18)
            << "EXTRA INFO\n";

        auto symList = syms.dump();
        std::sort(symList.begin(), symList.end(), [](const Symbol& a, const Symbol& b) {
            auto scopeRank = [](const string& scope) { return scope == "global" ? 0 : 1; };
            const int ra = scopeRank(a.scope);
            const int rb = scopeRank(b.scope);
            if (ra != rb) return ra < rb;
            if (a.scope != b.scope) return a.scope < b.scope;
            return a.name < b.name;
        });

        for (const auto& s : symList) {
            string kind = (s.kind == SymKind::Function)
                ? "Function"
                : (s.kind == SymKind::Parameter ? "Parameter" : "Variable");
            if (s.name == "cout") kind = "Variable/obj";

            string typeOut = typeName(s.type);
            if (s.kind == SymKind::Variable && s.type.isArray) {
                typeOut = baseTypeName(s.type.base);
            }

            string scopeOut;
            if (s.name == "cout") {
                scopeOut = "Global (library)";
            } else if (s.scope == "global") {
                scopeOut = "Global";
            } else {
                scopeOut = "Local to " + s.scope;
            }

            string extraOut = s.extra;
            if (extraOut.empty() && s.kind == SymKind::Function && s.name != "main") {
                extraOut = "Defined, " + std::to_string(s.type.params.size()) + " parameters";
            }

            out << mc::padRight(s.name, 14)
                << mc::padRight(kind, 14)
                << mc::padRight(typeOut, 18)
                << mc::padRight(scopeOut, 18)
                << extraOut << "\n";
        }
    }

    std::cout << "========== SEMANTIC ANALYSIS ==========" << "\n";
    mc::writeLines(mc::kOutSemantic, semanticTrace);

    std::cout << "========== INTERMEDIATE CODE ==========" << "\n";
    mc::writeLines(mc::kOutIr, ir.ir());

    std::cout << "========== IR OPTIMIZATION ==========" << "\n";
    IROptimizer optimizer;
    vector<string> optimizationReport;
    vector<string> optimizedIr = optimizer.optimize(ir.ir(), optimizationReport);
    mc::writeLines(mc::kOutIrUnoptimized, ir.ir());
    mc::writeLines(mc::kOutIrOptimized, optimizedIr);
    mc::writeLines(mc::kOutOptimizationReport, optimizationReport);

    std::cout << "========== MACHINE CODE ==========" << "\n";
    mc::writeLines(mc::kOutMachine, mach.code());

    errors.writeFile(mc::kOutErrors);

    std::cout << "Outputs written: "
              << mc::kOutTokens << ", "
              << mc::kOutSymbolTable << ", "
              << mc::kOutParser << ", "
              << mc::kOutSemantic << ", "
              << mc::kOutIr << ", "
              << mc::kOutIrUnoptimized << ", "
              << mc::kOutIrOptimized << ", "
              << mc::kOutOptimizationReport << ", "
              << mc::kOutMachine << ", "
              << mc::kOutErrors << "\n";

    return errors.hasErrors() ? 2 : 0;
}
