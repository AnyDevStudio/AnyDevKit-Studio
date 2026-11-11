#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <functional>

// A formula is a small whole-number expression a block can compute, such as "hp - max(1, atk * 2 - def)" or
// "level >= 3 and rand(1, 100) <= 25". Numbers are whole; / and % round toward zero and give 0 when dividing by 0;
// a comparison or and/or/not gives 1 for true and 0 for false. The format is described in docs/blocks-format.md.
//
//   formula   := or
//   or        := and (("or" | "||") and)*
//   and       := compare (("and" | "&&") compare)*
//   compare   := sum (("=" | "==" | "!=" | "<" | "<=" | ">" | ">=") sum)?
//   sum       := product (("+" | "-") product)*
//   product   := unary (("*" | "/" | "%") unary)*
//   unary     := ("-" | "not" | "!") unary | primary
//   primary   := number | name | name "(" formula ("," formula)* ")" | "(" formula ")"
//
// Functions: rand(low, high) (both included), min(a, b), max(a, b), abs(a), clamp(value, low, high).
// Names: the program's variables, "player" (the current player's number, 1 to 4), "players" (how many players are
// in the scene), and where things are, in world units: selfX and selfY (the middle of the copy the block runs
// for), playerX and playerY (the middle of the current player's character), dist (between those two) and
// nearestDist (from the copy to the nearest player's character); selfVX and selfVY are how fast the copy, a moving
// body, is going across and down (0 when still), for picking which way it should look. "seat" is the current
// player's place on their own screen, 1 or 2, and "online" is 0 when not playing online, 1 when friends can join
// this game, 2 while looking for a friend's game and 3 while playing in one. "guest" is 1 when the current
// player is a friend playing on their own screen, and 0 for a player on this console.
namespace Studio {

inline constexpr int MaximumFormulaLength = 400;
inline constexpr int MaximumFormulaDepth = 24; // nested brackets and function calls together

enum class FormulaNameKind { Unknown, GameVariable, PlayerVariable, CopyVariable, Player, Players };

struct FormulaName {
    FormulaNameKind kind = FormulaNameKind::Unknown;
    int index = 0; // the variable's place among the variables of its kind
};

using FormulaResolver = std::function<FormulaName(const QString& name)>;

struct FormulaResult {
    QString problem;         // empty when the formula is well formed; says what to fix otherwise
    QByteArray code;         // a C++ expression of type std::int64_t over a `world`, when well formed
    QStringList names;       // every variable name it reads, once each, in the order first used
    bool usesCopyVariables = false;
};

// Checks the formula and, when every name resolves, turns it into C++. With no resolver, names are not
// checked and no code is made.
FormulaResult compileFormula(const QString& formula, const FormulaResolver& resolver = {});

// True for selfX, selfY, playerX, playerY, dist, nearestDist, seat, online and the like, which a variable cannot be named.
bool isFormulaPlaceName(const QString& name);

// The formula with every whole-word use of a variable renamed, for when the user renames the variable.
QString renameVariableInFormula(const QString& formula, const QString& name, const QString& newName);

}
