#pragma once

#include <QString>

namespace modelharbor::core {

inline constexpr int kIpcProtocolVersion = 1;
inline constexpr auto kProductVersion = "0.1.0-dev";

QString productVersion();

} // namespace modelharbor::core
