#ifndef BODYCAM_NET_SESSION_HPP
#define BODYCAM_NET_SESSION_HPP

#include "app_session.hpp"
#include <memory>

namespace bodycam {
// Host (server autoritar + jucator local) sau client cu predictie
std::unique_ptr<AppSession> make_network_session(const SessionConfig& cfg);
}
#endif
