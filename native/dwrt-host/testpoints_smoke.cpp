#include "dwrt_host_testpoints.hpp"

#include <cstdint>

int main() {
    dwrt::host::reset_testpoints();
    dwrt::host::record_initialize_call();
    dwrt::host::record_initialize_reentrant_reject();
    dwrt::host::record_shutdown_call();

    {
        dwrt::host::CallbackScope outer(1);
        if (outer.recursive() || outer.depth() != 1 || outer.point_id() != 1) {
            return 1;
        }
        {
            dwrt::host::CallbackScope inner(1);
            if (!inner.recursive() || inner.depth() != 2 || inner.point_id() != 1) {
                return 2;
            }
        }
    }

    const dwrt::host::HostTestpointSnapshot snapshot = dwrt::host::testpoint_snapshot();
    if (snapshot.initialize_calls != 1) {
        return 3;
    }
    if (snapshot.initialize_reentrant_rejects != 1) {
        return 4;
    }
    if (snapshot.shutdown_calls != 1) {
        return 5;
    }
    if (snapshot.callback_entries != 2) {
        return 6;
    }
    if (snapshot.callback_recursive_entries != 1) {
        return 7;
    }
    if (snapshot.callback_current_depth != 0) {
        return 8;
    }
    if (snapshot.callback_max_depth != 2) {
        return 9;
    }
    return 0;
}
