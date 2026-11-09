#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QtTest>

#include <array>
#include <optional>

// The 5x7 font exists twice: in the PS5 runtime and in the editor stage, which must draw what the
// game draws. Neither can include the other, so this test reads both source files and compares them.

namespace {

using Rows = std::array<unsigned, 7>;

struct Table {
    QHash<int, Rows> glyphs;
    QString problem;
};

const char* const RuntimeTable = STUDIO_SOURCE_DIR "/runtime/SceneGame/src/Graphics/Font.cpp.in";
const char* const EditorTable = STUDIO_SOURCE_DIR "/src/app/PixelText.cpp";

constexpr int FirstPrintable = 0x20;
constexpr int LastPrintable = 0x7E;

int unescaped(const QString& literal) {
    return literal.size() == 2 ? literal.at(1).unicode() : literal.at(0).unicode();
}

Table readTable(const QString& path) {
    Table table;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        table.problem = QString("%1 could not be read: %2").arg(path, file.errorString());
        return table;
    }

    static const QRegularExpression entry(R"(^\s*case\s+'(\\.|[^\\'])':\s*return\s*\{([^}]*)\};)");
    static const QRegularExpression blankDefault(R"(^\s*default:\s*return\s*\{\s*\};)");
    auto sawDefault = false;
    for (const auto& line : QString::fromUtf8(file.readAll()).split('\n')) {
        if (blankDefault.match(line).hasMatch()) sawDefault = true;
        const auto match = entry.match(line);
        if (!match.hasMatch()) continue;

        const auto character = unescaped(match.captured(1));
        if (table.glyphs.contains(character)) table.problem = QString("%1 lists '%2' twice.").arg(path, QChar(character));
        const auto cells = match.captured(2).trimmed();
        const auto values = cells.isEmpty() ? QStringList{} : cells.split(',');
        if (!values.isEmpty() && values.size() != 7) table.problem = QString("%1: '%2' needs 7 rows.").arg(path, QChar(character));

        Rows rows{};
        for (qsizetype row = 0; row < values.size() && row < 7; ++row) rows[row] = values[row].trimmed().toUInt();
        table.glyphs.insert(character, rows);
    }
    if (table.glyphs.isEmpty()) table.problem = QString("%1 has no glyph table this test can read. Update tst_font_tables if its layout changed.").arg(path);
    else if (!sawDefault) table.problem = QString("%1 must keep `default: return {};` so anything without a glyph draws blank.").arg(path);
    return table;
}

QString describe(int character) {
    return QString("'%1' (U+%2)").arg(QChar(character)).arg(character, 4, 16, QChar('0'));
}

QString describe(const std::optional<Rows>& rows) {
    if (!rows) return "no glyph";
    QStringList cells;
    for (const auto row : *rows) cells << QString::number(row);
    return '{' + cells.join(", ") + '}';
}

}

class FontTableTests : public QObject {
    Q_OBJECT

private slots:
    void everyPrintableCharacterHasAGlyph_data() {
        QTest::addColumn<QString>("path");
        QTest::newRow("runtime") << QString(RuntimeTable);
        QTest::newRow("editor") << QString(EditorTable);
    }

    void everyPrintableCharacterHasAGlyph() {
        QFETCH(QString, path);
        const auto table = readTable(path);
        QVERIFY2(table.problem.isEmpty(), qPrintable(table.problem));

        QStringList missing;
        for (int character = FirstPrintable + 1; character <= LastPrintable; ++character) {
            const auto found = table.glyphs.constFind(character);
            if (found == table.glyphs.cend() || *found == Rows{}) missing << QString(QChar(character));
        }
        QVERIFY2(missing.isEmpty(), qPrintable(QString("%1 has no glyph for: %2").arg(path, missing.join(' '))));
    }

    void spaceStaysBlank_data() { everyPrintableCharacterHasAGlyph_data(); }

    void spaceStaysBlank() {
        QFETCH(QString, path);
        const auto table = readTable(path);
        QVERIFY2(table.problem.isEmpty(), qPrintable(table.problem));
        QCOMPARE(table.glyphs.value(' ', Rows{}), Rows{});
    }

    void glyphRowsFitFiveCells_data() { everyPrintableCharacterHasAGlyph_data(); }

    void glyphRowsFitFiveCells() {
        QFETCH(QString, path);
        const auto table = readTable(path);
        QVERIFY2(table.problem.isEmpty(), qPrintable(table.problem));
        for (auto entry = table.glyphs.cbegin(); entry != table.glyphs.cend(); ++entry) {
            for (const auto row : entry.value()) QVERIFY2(row <= 31, qPrintable(describe(entry.key()) + " has a row wider than 5 cells."));
        }
    }

    void noTwoCharactersLookTheSame_data() { everyPrintableCharacterHasAGlyph_data(); }

    void noTwoCharactersLookTheSame() {
        QFETCH(QString, path);
        const auto table = readTable(path);
        QVERIFY2(table.problem.isEmpty(), qPrintable(table.problem));
        for (int first = FirstPrintable + 1; first <= LastPrintable; ++first) {
            for (int second = first + 1; second <= LastPrintable; ++second) {
                if (!table.glyphs.contains(first) || !table.glyphs.contains(second)) continue;
                QVERIFY2(table.glyphs[first] != table.glyphs[second], qPrintable(QString("%1 and %2 draw identically in %3.").arg(describe(first), describe(second), path)));
            }
        }
    }

    void runtimeAndEditorDrawTheSameGlyphs() {
        const auto runtime = readTable(RuntimeTable);
        const auto editor = readTable(EditorTable);
        QVERIFY2(runtime.problem.isEmpty(), qPrintable(runtime.problem));
        QVERIFY2(editor.problem.isEmpty(), qPrintable(editor.problem));

        // Beyond ASCII as well: a glyph added to one table only must fail here.
        for (int character = 0; character < 256; ++character) {
            const auto inRuntime = runtime.glyphs.contains(character) ? std::optional(runtime.glyphs[character]) : std::nullopt;
            const auto inEditor = editor.glyphs.contains(character) ? std::optional(editor.glyphs[character]) : std::nullopt;
            QVERIFY2(inRuntime == inEditor, qPrintable(QString("The font tables differ at %1: the PS5 runtime has %2, the editor has %3. Copy the same glyph to both tables.").arg(describe(character), describe(inRuntime), describe(inEditor))));
        }
    }
};

QTEST_GUILESS_MAIN(FontTableTests)
#include "tst_font_tables.moc"
