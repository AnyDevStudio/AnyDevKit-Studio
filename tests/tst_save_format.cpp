// Host checks for the game's save file format and the handoff to its save thread. No SDK or console
// is involved. Built under AddressSanitizer and UBSan by CMake.
#include "Save/SaveData.hpp.in"

#include <cstdio>
#include <cstring>
#include <limits>
#include <span>
#include <thread>
#include <vector>

namespace {

int failures = 0;
#define EXPECT(condition) do { if (!(condition)) { ++failures; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (false)

using SaveData::Decode;
using SaveData::Encode;
using SaveData::FileBytes;
using SaveData::Mailbox;
using SaveData::NameHash;

constexpr std::array<std::uint32_t, 2> Hashes{NameHash("score"), NameHash("lives")};

void theFileLayoutIsDocumentedAndStable() {
    // Magic "ADKS", version, count, count pairs of name hash and value, checksum over all of that.
    EXPECT(FileBytes(0) == 16);
    EXPECT(FileBytes(2) == 16 + 2 * 8);
    std::array<std::uint8_t, 32> file{};
    std::array<std::int32_t, 2> values{7, -9};
    const auto written = Encode(std::span<const std::int32_t>(values), std::span<const std::uint32_t>(Hashes), file);
    EXPECT(written == FileBytes(2));
    EXPECT(file[0] == 'A' && file[1] == 'D' && file[2] == 'K' && file[3] == 'S');
    EXPECT(file[4] == 1 && file[5] == 0 && file[6] == 0 && file[7] == 0); // version
    EXPECT(file[8] == 2); // count, little endian
    std::uint32_t storedHash = 0;
    std::memcpy(&storedHash, file.data() + 12, 4);
    EXPECT(storedHash == Hashes[0]);
    std::int32_t storedValue = 0;
    std::memcpy(&storedValue, file.data() + 16, 4);
    EXPECT(storedValue == 7);
}

void aSaveReadsBack() {
    std::array<std::uint8_t, 64> file{};
    std::array<std::int32_t, 2> values{123, std::numeric_limits<std::int32_t>::min()};
    const auto written = Encode(std::span<const std::int32_t>(values), std::span<const std::uint32_t>(Hashes), file);
    std::array<std::int32_t, 2> loaded{1, 2};
    EXPECT(Decode(std::span<const std::uint8_t>(file.data(), written), std::span<const std::uint32_t>(Hashes), loaded));
    EXPECT(loaded == values);
}

void valuesFollowTheirNameNotTheirPosition() {
    std::array<std::uint8_t, 64> file{};
    std::array<std::int32_t, 2> saved{10, 20};
    const auto written = Encode(std::span<const std::int32_t>(saved), std::span<const std::uint32_t>(Hashes), file);
    // The project was reordered and gained a variable: lives, coins, score.
    const std::array<std::uint32_t, 3> reordered{Hashes[1], NameHash("coins"), Hashes[0]};
    std::array<std::int32_t, 3> loaded{0, 99, 0};
    EXPECT(Decode(std::span<const std::uint8_t>(file.data(), written), std::span<const std::uint32_t>(reordered), loaded));
    EXPECT(loaded[0] == 20);
    EXPECT(loaded[1] == 99); // a variable the save does not know keeps its current value
    EXPECT(loaded[2] == 10);
}

void damagedFilesAreRefusedAndChangeNothing() {
    std::array<std::uint8_t, 64> file{};
    std::array<std::int32_t, 2> values{5, 6};
    const auto written = Encode(std::span<const std::int32_t>(values), std::span<const std::uint32_t>(Hashes), file);

    const auto check = [&](std::vector<std::uint8_t> bytes) {
        std::array<std::int32_t, 2> loaded{1, 2};
        const auto ok = Decode(std::span<const std::uint8_t>(bytes.data(), bytes.size()), std::span<const std::uint32_t>(Hashes), loaded);
        EXPECT(!ok);
        EXPECT(loaded[0] == 1 && loaded[1] == 2);
    };

    std::vector<std::uint8_t> bytes(file.begin(), file.begin() + written);
    check({});
    check({1, 2, 3});
    auto wrongMagic = bytes;
    wrongMagic[0] = 'X';
    check(wrongMagic);
    auto wrongVersion = bytes;
    wrongVersion[4] = 9;
    check(wrongVersion);
    auto flippedValue = bytes;
    flippedValue[16] ^= 0xff;
    check(flippedValue);
    auto extra = bytes;
    extra.push_back(0);
    check(extra);
    auto truncated = bytes;
    truncated.pop_back();
    check(truncated);
    auto hugeCount = bytes;
    hugeCount[8] = 0xff;
    hugeCount[9] = 0xff;
    hugeCount[10] = 0xff;
    hugeCount[11] = 0x7f;
    check(hugeCount);
}

void anEncodeThatDoesNotFitIsRefused() {
    std::array<std::uint8_t, 4> tiny{};
    std::array<std::int32_t, 2> values{1, 2};
    EXPECT(Encode(std::span<const std::int32_t>(values), std::span<const std::uint32_t>(Hashes), tiny) == 0);
}

void noVariablesStillRoundTrips() {
    std::array<std::uint8_t, 16> file{};
    const auto written = Encode(std::span<const std::int32_t>(), std::span<const std::uint32_t>(), file);
    EXPECT(written == 16);
    EXPECT(Decode(std::span<const std::uint8_t>(file.data(), written), std::span<const std::uint32_t>(), std::span<std::int32_t>()));
}

void theNewestSaveWinsAndEachIsTakenOnce() {
    Mailbox<2> mailbox;
    std::array<std::int32_t, 2> taken{};
    EXPECT(!mailbox.TakeSave(taken));
    mailbox.PublishSave(std::array<std::int32_t, 2>{1, 2});
    mailbox.PublishSave(std::array<std::int32_t, 2>{3, 4});
    EXPECT(mailbox.TakeSave(taken));
    EXPECT(taken[0] == 3 && taken[1] == 4);
    EXPECT(!mailbox.TakeSave(taken));
}

void aLoadRequestIsTakenOnceAndItsResultIsHandedBack() {
    Mailbox<2> mailbox;
    EXPECT(!mailbox.TakeLoadRequest());
    mailbox.RequestLoad();
    mailbox.RequestLoad(); // asking twice before the worker runs is one load
    EXPECT(mailbox.TakeLoadRequest());
    EXPECT(!mailbox.TakeLoadRequest());
    mailbox.PublishLoaded(std::array<std::int32_t, 2>{9, 8});
    std::array<std::int32_t, 2> values{};
    EXPECT(mailbox.TakeLoaded(values));
    EXPECT(values[0] == 9 && values[1] == 8);
    EXPECT(!mailbox.TakeLoaded(values));
}

void twoThreadsNeverSeeATornSave() {
    Mailbox<4> mailbox;
    std::atomic<bool> stop{false};
    std::atomic<int> taken{0};
    std::thread worker([&] {
        std::array<std::int32_t, 4> values{};
        while (!stop.load()) {
            if (!mailbox.TakeSave(values)) continue;
            taken.fetch_add(1);
            for (const auto value : values) EXPECT(value == values[0]);
        }
    });
    for (int round = 0; round < 20000; ++round) {
        std::array<std::int32_t, 4> values;
        values.fill(round);
        mailbox.PublishSave(values);
    }
    // The writer can finish before the reader thread starts, so let the reader see the last save.
    for (int wait = 0; wait < 5000 && taken.load() == 0; ++wait) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    stop.store(true);
    worker.join();
    EXPECT(taken.load() >= 1);
}

}

int main() {
    theFileLayoutIsDocumentedAndStable();
    aSaveReadsBack();
    valuesFollowTheirNameNotTheirPosition();
    damagedFilesAreRefusedAndChangeNothing();
    anEncodeThatDoesNotFitIsRefused();
    noVariablesStillRoundTrips();
    theNewestSaveWinsAndEachIsTakenOnce();
    aLoadRequestIsTakenOnceAndItsResultIsHandedBack();
    twoThreadsNeverSeeATornSave();
    if (failures != 0) {
        std::printf("%d checks failed\n", failures);
        return 1;
    }
    std::printf("all save format checks passed\n");
    return 0;
}
