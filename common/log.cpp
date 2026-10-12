#include "common/log.h"

#include <cstdio>
#include <ctime>

namespace fleetcore {

void log_timestamp() {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    std::fprintf(stderr, "%6ld.%03ld ",
                 static_cast<long>(ts.tv_sec),
                 static_cast<long>(ts.tv_nsec / 1000000));
}

}  // namespace fleetcore