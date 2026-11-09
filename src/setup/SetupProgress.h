#pragma once

#include <QList>
#include <QString>

#include <optional>

namespace Studio {

enum class SetupStep { Tools, Download, Unpack, Dependencies };
enum class SetupStepState { Waiting, Running, Done, Failed };

QString setupStepId(SetupStep step);
QString setupStepTitle(SetupStep step);
QString setupStepStateName(SetupStepState state);

struct SetupStepStatus {
    SetupStep step = SetupStep::Tools;
    SetupStepState state = SetupStepState::Waiting;
    QString detail;
    double progress = -1; // 0..1, or negative when the amount is unknown
};

// The checklist: which step is doing what. Steps run in order and one at a time.
class SetupProgress {
public:
    SetupProgress();

    const QList<SetupStepStatus>& steps() const;
    const SetupStepStatus& status(SetupStep step) const;

    // False, and nothing changes, if an earlier step is not done or another step is running.
    bool begin(SetupStep step, const QString& detail = {});
    void report(SetupStep step, const QString& detail, double progress = -1);
    void complete(SetupStep step, const QString& detail = {});
    void fail(SetupStep step, const QString& detail);
    // A running step goes back to waiting with `detail`, ready to continue.
    void pause(const QString& detail);
    // A failed step goes back to waiting so it can be retried.
    void clearFailure();
    // This step and every later one go back to waiting, as when a download must be redone.
    void reopen(SetupStep step);
    void restart(); // everything back to waiting

    std::optional<SetupStep> running() const;
    std::optional<SetupStep> failed() const;
    bool allDone() const;

private:
    QList<SetupStepStatus> entries;
};

}
