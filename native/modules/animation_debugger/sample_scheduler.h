#pragma once
#include <algorithm>
#include <cmath>

namespace BetterEndfield::AnimationDebugger {
// Advance from the planned deadline, not the late frame's time. Otherwise a
// 60 Hz frame loop can permanently undersample a requested 30 Hz capture.
class SampleScheduler {
    double next_ = 0;
    int rate_ = 0;
public:
    bool Due(double now, int hz) {
        hz = std::clamp(hz, 1, 120);
        if (!std::isfinite(now)) return false;
        if (hz != rate_) { rate_ = hz; next_ = now; }
        if (now + 1e-9 < next_) return false;
        const double period = 1.0 / hz;
        const double missed = std::max(0.0, std::floor((now - next_ + 1e-9) / period));
        next_ += (missed + 1) * period;
        return true;
    }
};
}
