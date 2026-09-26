#include "../OptiScaler/dlssnr/NrSubmissionLedger.h"
#include "../OptiScaler/dlssnr/NrRetirementCadence.h"
#include "../OptiScaler/dlssnr/NrHistoryContract.h"
#include <iostream>
#include <stdexcept>

using namespace DlssNr;
static unsigned checks = 0;
static void Check(bool value, const char* label)
{
    ++checks;
    if (!value) throw std::runtime_error(label);
}
int main()
{
    try
    {
        NrSubmissionLedger ledger;
        Check(NrResetWithSource(false, true, 1), "game camera-cut reset reaches NR");
        Check(!NrResetWithSource(false, true, 0), "ordinary frame does not reset history");
        Check(NrResetWithSource(true, true, 0), "skip/init reset stays latched despite source zero");
        Check(NrResetWithSource(true, false, 0), "missing parameter never clears pending reset");
        Check(!NrResetWithSource(false, false, 1), "failed parameter read cannot invent reset");
        Check(NrResetWithSource(false, true, -1), "nonzero source reset is respected");
        bool leftReset = NrResetWithSource(false, true, 1), rightReset = NrResetWithSource(false, true, 0);
        Check(leftReset && !rightReset, "source reset stays isolated to its own history");
        leftReset = NrResetWithSource(leftReset, true, 0);
        Check(leftReset, "one-frame reset pulse survives a skipped evaluate");
        NrRetirementCadence cadence;
        cadence.Observe(0, true, false, false);
        Check(cadence.calls == 1 && cadence.skipped == 0, "completed admission is not a skip");
        cadence.Observe(3, true, true, false);
        Check(cadence.waits == 1 && cadence.skipped == 0, "successful waits are visible without skips");
        cadence.Observe(8, false, true, false);
        Check(cadence.waits == 2 && cadence.skipped == 1, "timeout accounts wait and skip");
        cadence.Observe(0, false, false, true);
        Check(cadence.pending == 1 && cadence.skipped == 2, "unsubmitted recording distinguished");
        Check(cadence.totalMs == 11 && cadence.maximumMs == 8, "latency summary accumulates actual admission time");
        cadence = {};
        Check(cadence.calls == 0 && cadence.totalMs == 0, "new window never accumulates prior latency");
        Check(!NrFenceRetired(10, 11), "outstanding fence never retires");
        Check(NrFenceRetired(11, 11), "exact fence retires");
        Check(NrFenceRetired(12, 11), "later fence retires");
        Check(!NrFenceRetired(~std::uint64_t { 0 }, 11), "device loss is never completion");
        int listStorage = 0, secondStorage = 0;
        void* list = &listStorage;
        void* second = &secondStorage;
        const auto left = ledger.Record(0, list);
        auto submitted = ledger.Detach(list);
        Check(submitted.size() == 1, "detach exact pre-Execute recording");
        Check(ledger.Detach(list).empty(), "duplicate list in batch detaches once");
        // Opposite eye records the same object between Execute and its callback.
        const auto right = ledger.Record(1, list);
        Check(ledger.Complete(submitted[0]), "old Execute retires only left");
        Check(!ledger.Complete(left), "duplicate completion cannot overwrite fence");
        auto next = ledger.Detach(list);
        Check(next.size() == 1 && next[0].recording == right.recording,
              "future right recording not consumed by old callback");
        Check(ledger.Complete(next[0]), "future right requires own submission");
        // Handle released/recreated while an older callback is detached.
        const auto old = ledger.Record(7, list);
        submitted = ledger.Detach(list);
        ledger.Forget(7);
        const auto replacement = ledger.Record(7, second);
        Check(!ledger.Complete(old), "old callback cannot arm recreated handle");
        next = ledger.Detach(second);
        Check(next.size() == 1 && next[0].recording == replacement.recording,
              "recreated recording remains independently tracked");
        Check(ledger.Complete(next[0]), "recreated handle completes normally");
        // Two handles really recorded in one batch must BOTH receive its fence.
        ledger.Record(0, list);
        ledger.Record(1, list);
        submitted = ledger.Detach(list);
        Check(submitted.size() == 2, "same actual batch includes both eyes");
        Check(ledger.Complete(submitted[0]) && ledger.Complete(submitted[1]),
              "both actual receipts complete");
        const auto beforeReset = ledger.Record(0, list);
        ledger.Detach(list);
        ledger.Clear();
        const auto afterReset = ledger.Record(0, list);
        Check(afterReset.recording > beforeReset.recording, "reset never recycles serial");
        Check(!ledger.Complete(beforeReset), "reset rejects detached callback");
        Check(ledger.Complete(afterReset), "new generation completes");
        std::cout << checks << "/" << checks << " NR receipt checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
