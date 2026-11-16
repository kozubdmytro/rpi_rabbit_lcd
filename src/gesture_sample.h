#pragma once
namespace sr {
// Input acquisition is deferred; keep the existing UI gesture contract.
struct GestureSample {
    bool healthy=true;
    double position=-1; // -1 means released.
};
}
