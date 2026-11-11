#include "Formula.h"

#include <QHash>
#include <QRegularExpression>

namespace Studio {

namespace {

enum class TokenKind { Number, Name, Symbol, End };

struct Token {
    TokenKind kind = TokenKind::End;
    QString text;
    qint64 number = 0;
    int column = 0; // 1-based, for messages
};

constexpr qint64 MaximumLiteral = 1000000000;

struct FunctionInfo {
    int arguments;
    const char* call; // the runtime function the call becomes
};

const QHash<QString, FunctionInfo>& functions() {
    static const QHash<QString, FunctionInfo> table{
        {"rand", {2, "world.Random"}},
        {"min", {2, "Formula::Min"}},
        {"max", {2, "Formula::Max"}},
        {"abs", {1, "Formula::Abs"}},
        {"clamp", {3, "Formula::Clamp"}},
    };
    return table;
}

// Where things are. Each is a whole number of world units, and 0 or far away when there is no such thing.
const QHash<QString, const char*>& placeNames() {
    static const QHash<QString, const char*> table{
        {"selfX", "world.SelfX()"},
        {"selfY", "world.SelfY()"},
        {"playerX", "world.PlayerX()"},
        {"playerY", "world.PlayerY()"},
        {"dist", "world.PlayerDistance()"},
        {"nearestDist", "world.NearestPlayerDistance()"},
        {"selfVX", "world.SelfVelocityX()"},
        {"selfVY", "world.SelfVelocityY()"},
        {"seat", "world.Seat()"},
        {"online", "world.OnlineState()"},
        {"guest", "world.OnFriendScreen()"},
    };
    return table;
}

bool isWordStart(QChar character) { return character.isLetter() || character == '_'; }
bool isWordCharacter(QChar character) { return character.isLetterOrNumber() || character == '_'; }

class Parser {
public:
    Parser(const QString& text, const FormulaResolver& resolver) : text(text), resolver(resolver) {}

    FormulaResult run() {
        FormulaResult result;
        if (text.trimmed().isEmpty()) {
            result.problem = "The formula is empty. Write a number, a variable or a sum such as \"hp - 1\".";
            return result;
        }
        if (text.size() > MaximumFormulaLength) {
            result.problem = QString("The formula is %1 characters long, but a formula can be at most %2. Split it over two blocks.").arg(text.size()).arg(MaximumFormulaLength);
            return result;
        }
        if (!tokenize()) {
            result.problem = problem;
            return result;
        }
        const auto code = parseOr(0);
        if (problem.isEmpty() && current().kind != TokenKind::End) fail(QString("\"%1\" at character %2 does not belong here. Check for a missing + - * / or a stray bracket.").arg(current().text).arg(current().column));
        result.problem = problem;
        result.names = names;
        result.usesCopyVariables = usesCopy;
        if (problem.isEmpty() && resolver) result.code = code;
        return result;
    }

private:
    bool tokenize() {
        int position = 0;
        while (position < text.size()) {
            const auto character = text.at(position);
            if (character.isSpace()) {
                ++position;
                continue;
            }
            Token token;
            token.column = position + 1;
            if (character.isDigit()) {
                int end = position;
                while (end < text.size() && text.at(end).isDigit()) ++end;
                if (end < text.size() && isWordCharacter(text.at(end))) {
                    fail(QString("\"%1\" at character %2 is not a number or a name. A name cannot start with a digit.").arg(text.mid(position, end - position + 1)).arg(position + 1));
                    return false;
                }
                const auto digits = text.mid(position, end - position);
                bool ok = false;
                const auto value = digits.toLongLong(&ok);
                if (!ok || value > MaximumLiteral) {
                    fail(QString("The number %1 is too large. A formula's numbers go up to %2.").arg(digits).arg(MaximumLiteral));
                    return false;
                }
                token.kind = TokenKind::Number;
                token.text = digits;
                token.number = value;
                position = end;
            } else if (isWordStart(character)) {
                int end = position;
                while (end < text.size() && isWordCharacter(text.at(end))) ++end;
                token.kind = TokenKind::Name;
                token.text = text.mid(position, end - position);
                const auto lower = token.text.toLower();
                if (lower == "and" || lower == "or" || lower == "not") {
                    token.kind = TokenKind::Symbol;
                    token.text = lower == "and" ? "&&" : lower == "or" ? "||" : "!";
                }
                position = end;
            } else {
                static const QStringList twoCharacter{"==", "!=", "<=", ">=", "&&", "||"};
                const auto pair = text.mid(position, 2);
                token.kind = TokenKind::Symbol;
                if (twoCharacter.contains(pair)) {
                    token.text = pair;
                    position += 2;
                } else if (QString("+-*/%()<>=!,").contains(character)) {
                    token.text = character == '=' ? QString("==") : QString(character);
                    position += 1;
                } else {
                    fail(QString("\"%1\" at character %2 cannot be used in a formula. Use numbers, names, + - * / %, brackets and comparisons.").arg(character).arg(position + 1));
                    return false;
                }
            }
            tokens.append(token);
        }
        Token end;
        end.column = static_cast<int>(text.size()) + 1;
        tokens.append(end);
        return true;
    }

    const Token& current() const { return tokens.at(cursor); }
    void advance() {
        if (cursor + 1 < tokens.size()) ++cursor;
    }
    bool isSymbol(const char* symbol) const { return current().kind == TokenKind::Symbol && current().text == QLatin1String(symbol); }

    void fail(const QString& message) {
        if (problem.isEmpty()) problem = message;
    }

    bool enter(int depth) {
        if (depth > MaximumFormulaDepth) {
            fail(QString("The formula nests brackets and functions more than %1 deep. Simplify it.").arg(MaximumFormulaDepth));
            return false;
        }
        return problem.isEmpty();
    }

    QByteArray parseOr(int depth) {
        if (!enter(depth)) return {};
        auto code = parseAnd(depth);
        while (problem.isEmpty() && isSymbol("||")) {
            advance();
            const auto right = parseAnd(depth);
            code = "Formula::Or(" + code + ", " + right + ")";
        }
        return code;
    }

    QByteArray parseAnd(int depth) {
        auto code = parseCompare(depth);
        while (problem.isEmpty() && isSymbol("&&")) {
            advance();
            const auto right = parseCompare(depth);
            code = "Formula::And(" + code + ", " + right + ")";
        }
        return code;
    }

    QByteArray parseCompare(int depth) {
        auto code = parseSum(depth);
        static const QHash<QString, const char*> comparisons{{"==", "Equal"}, {"!=", "NotEqual"}, {"<", "Less"}, {"<=", "LessOrEqual"}, {">", "Greater"}, {">=", "GreaterOrEqual"}};
        if (problem.isEmpty() && current().kind == TokenKind::Symbol && comparisons.contains(current().text)) {
            const auto name = comparisons.value(current().text);
            advance();
            const auto right = parseSum(depth);
            code = QByteArray("Formula::") + name + "(" + code + ", " + right + ")";
            if (problem.isEmpty() && current().kind == TokenKind::Symbol && comparisons.contains(current().text)) {
                fail(QString("\"%1\" at character %2 compares a comparison. Join two comparisons with and or or, as in \"a < b and b < c\".").arg(current().text).arg(current().column));
            }
        }
        return code;
    }

    QByteArray parseSum(int depth) {
        auto code = parseProduct(depth);
        while (problem.isEmpty() && (isSymbol("+") || isSymbol("-"))) {
            const auto operation = isSymbol("+") ? "Formula::Add(" : "Formula::Subtract(";
            advance();
            const auto right = parseProduct(depth);
            code = operation + code + ", " + right + ")";
        }
        return code;
    }

    QByteArray parseProduct(int depth) {
        auto code = parseUnary(depth);
        while (problem.isEmpty() && (isSymbol("*") || isSymbol("/") || isSymbol("%"))) {
            const auto operation = isSymbol("*") ? "Formula::Multiply(" : isSymbol("/") ? "Formula::Divide(" : "Formula::Remainder(";
            advance();
            const auto right = parseUnary(depth);
            code = operation + code + ", " + right + ")";
        }
        return code;
    }

    QByteArray parseUnary(int depth) {
        if (!enter(depth)) return {};
        if (isSymbol("-")) {
            advance();
            return "Formula::Negate(" + parseUnary(depth + 1) + ")";
        }
        if (isSymbol("!")) {
            advance();
            return "Formula::Not(" + parseUnary(depth + 1) + ")";
        }
        return parsePrimary(depth);
    }

    QByteArray parsePrimary(int depth) {
        const auto token = current();
        if (token.kind == TokenKind::Number) {
            advance();
            return "std::int64_t{" + QByteArray::number(token.number) + "}";
        }
        if (isSymbol("(")) {
            advance();
            const auto inner = parseOr(depth + 1);
            if (problem.isEmpty() && !isSymbol(")")) fail(QString("A bracket opened at character %1 is never closed. Add a ).").arg(token.column));
            advance();
            return "(" + inner + ")";
        }
        if (token.kind == TokenKind::Name) {
            advance();
            if (isSymbol("(")) return parseCall(token, depth);
            return name(token);
        }
        if (token.kind == TokenKind::End) fail("The formula ends too soon. Something is missing after the last + - * / or comparison.");
        else fail(QString("\"%1\" at character %2 does not belong here. A number, a name or a bracket is missing before it.").arg(token.text).arg(token.column));
        return {};
    }

    QByteArray parseCall(const Token& token, int depth) {
        const auto lower = token.text.toLower();
        if (!functions().contains(lower)) {
            QStringList known = functions().keys();
            known.sort();
            fail(QString("There is no function called %1. The functions are %2.").arg(token.text, known.join(", ")));
            return {};
        }
        const auto info = functions().value(lower);
        advance(); // (
        QList<QByteArray> arguments;
        if (!isSymbol(")")) {
            arguments.append(parseOr(depth + 1));
            while (problem.isEmpty() && isSymbol(",")) {
                advance();
                arguments.append(parseOr(depth + 1));
            }
        }
        if (problem.isEmpty() && !isSymbol(")")) fail(QString("The call to %1 at character %2 is never closed. Add a ).").arg(token.text).arg(token.column));
        advance();
        if (problem.isEmpty() && arguments.size() != info.arguments) {
            fail(QString("%1 takes %2, but it was given %3.").arg(lower, info.arguments == 1 ? QString("1 number") : QString("%1 numbers").arg(info.arguments)).arg(arguments.size()));
        }
        QByteArray code = QByteArray(info.call) + "(";
        for (qsizetype index = 0; index < arguments.size(); ++index) code += (index ? ", " : "") + arguments[index];
        return code + ")";
    }

    QByteArray name(const Token& token) {
        if (placeNames().contains(token.text)) return placeNames().value(token.text);
        const auto lower = token.text.toLower();
        if (!resolver) {
            if (lower != "player" && lower != "players" && !names.contains(token.text)) names.append(token.text);
            return {};
        }
        const auto found = resolver(token.text);
        switch (found.kind) {
        case FormulaNameKind::Player: return "world.PlayerNumber()";
        case FormulaNameKind::Players: return "world.PlayersHere()";
        case FormulaNameKind::GameVariable:
            if (!names.contains(token.text)) names.append(token.text);
            return "std::int64_t{world.Variable(" + QByteArray::number(found.index) + ")}";
        case FormulaNameKind::PlayerVariable:
            if (!names.contains(token.text)) names.append(token.text);
            return "std::int64_t{world.PlayerVariable(" + QByteArray::number(found.index) + ")}";
        case FormulaNameKind::CopyVariable:
            if (!names.contains(token.text)) names.append(token.text);
            usesCopy = true;
            return "std::int64_t{world.CopyVariable(" + QByteArray::number(found.index) + ")}";
        case FormulaNameKind::Unknown: break;
        }
        fail(QString("The formula uses \"%1\", which is not a variable. Add a variable named %1 in the Variables area, or check the spelling.").arg(token.text));
        return {};
    }

    const QString& text;
    const FormulaResolver& resolver;
    QList<Token> tokens;
    qsizetype cursor = 0;
    QString problem;
    QStringList names;
    bool usesCopy = false;
};

}

FormulaResult compileFormula(const QString& formula, const FormulaResolver& resolver) {
    return Parser(formula, resolver).run();
}

bool isFormulaPlaceName(const QString& name) {
    return placeNames().contains(name);
}

QString renameVariableInFormula(const QString& formula, const QString& name, const QString& newName) {
    const QRegularExpression word("(?<![A-Za-z0-9_])" + QRegularExpression::escape(name) + "(?![A-Za-z0-9_])");
    auto renamed = formula;
    return renamed.replace(word, newName);
}

}
