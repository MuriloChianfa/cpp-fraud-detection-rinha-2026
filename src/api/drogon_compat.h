#pragma once

#include <utility>

namespace rinha::drogon_compat {

template <typename App, typename Cb>
inline auto set_before_listen_sockopt(App& app, Cb&& cb)
    -> decltype(app.setBeforeListenSockOptCallback(std::forward<Cb>(cb)), void()) {
    app.setBeforeListenSockOptCallback(std::forward<Cb>(cb));
}

template <typename... Args>
inline void set_before_listen_sockopt(Args&&...) noexcept {}

template <typename App, typename Cb>
inline auto set_after_accept_sockopt(App& app, Cb&& cb)
    -> decltype(app.setAfterAcceptSockOptCallback(std::forward<Cb>(cb)), void()) {
    app.setAfterAcceptSockOptCallback(std::forward<Cb>(cb));
}

template <typename... Args>
inline void set_after_accept_sockopt(Args&&...) noexcept {}

}
