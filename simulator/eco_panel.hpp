#pragma once
#include "eco_adapter.hpp"
#include "fm1/ui.hpp"

namespace fm1 {
// Local native engine only; no endpoint or remote-state claim.
class EcoPanel {
public:
    enum class Change { None, Edit, Flush };
    void render(Canvas&, const EcoView&) const;
    Change touch(TouchPhase, int, int, EcoAdapter&, bool audioLease);
    void cancel(){ capture_=-1; }
private:
    int capture_=-1;
    static int hit(int,int);
};
}
