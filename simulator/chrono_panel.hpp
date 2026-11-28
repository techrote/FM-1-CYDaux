#pragma once
#include "chrono_adapter.hpp"
#include "fm1/ui.hpp"

namespace fm1 {
// Local native evaluator view. No CY1 messages, no device telemetry.
class ChronoPanel {
public:
    enum class Change { None, Edit, Flush };
    void render(Canvas&, const ChronoView&) const;
    Change touch(TouchPhase, int x, int y, ChronoAdapter&, bool audioLease);
    void cancel() { captured_=-1; }
private:
    int captured_=-1;
    static int hit(int x,int y,const ChronoView&);
};
}
