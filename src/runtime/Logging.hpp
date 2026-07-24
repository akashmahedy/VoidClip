#pragma once

// Centralized logging setup, mirroring the reference module
// voidclip/runtime/logging.py.

#include <spdlog/common.h>

namespace voidclip::runtime {

// Configure the application logger at `level`. The first call installs a single
// stderr sink and makes it the default; later calls only adjust the level, so it
// is safe to call repeatedly without duplicating sinks.
void configure_logging(spdlog::level::level_enum level = spdlog::level::info);

} // namespace voidclip::runtime
