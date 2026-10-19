#include "net_session.hpp"

namespace bodycam {
std::unique_ptr<AppSession> make_network_session(const SessionConfig&) { return nullptr; }
}
