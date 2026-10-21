#pragma once

#include <string>

namespace videocut::base {
// Distribution builds admit the running signed executable, never a caller-
// supplied identifier, executable name, path, or environment-variable token.
// Development builds deliberately keep CLI fixtures usable.
[[nodiscard]] bool AuthorizeHostProcess(std::string &error);
void RequireAuthorizedHostProcess();
} // namespace videocut::base
