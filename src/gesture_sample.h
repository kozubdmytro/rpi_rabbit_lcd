#pragma once
namespace sr {
// Normalized vertical input for the existing UI gesture contract.
struct GestureSample {
    bool healthy=true;
    double position=-1; // -1 means released.
};
}
