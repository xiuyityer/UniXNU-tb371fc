/* SPDX-License-Identifier: MIT
 * Shared actual lifecycle logic; host tests exercise partial-start rollback.
 * No assumptions about service names or fabricated matching success.
 */
#pragma once
template<class Service> class Tb371fcPublicationLifecycle {
  Service *provider = nullptr, *nub = nullptr, *platform = nullptr;
  const char *failed = nullptr;
  bool pa = false, ps = false, na = false, ns = false, committed = false;
public:
  bool attach(Service *p, Service *n, Service *root) {
    if (provider || !p || !n || !root) { failed = "invalid lifecycle input"; return false; }
    provider = p; nub = n; platform = root;
    failed = "provider.attach";
    if (!(pa = provider->attach(platform))) return false;
    failed = "provider.start";
    if (!(ps = provider->start(platform))) return false;
    failed = "nub.attach";
    if (!(na = nub->attach(provider))) return false;
    failed = "nub.start";
    if (!(ns = nub->start(provider))) return false;
    failed = nullptr;
    return true;
  }
  const char *failure() const { return failed; }
  void commit() { if (pa && ps && na && ns) committed = true; }
  void rollback() {
    if (committed) return;
    if (ns) { nub->stop(provider); ns = false; }
    if (na) { nub->detach(provider); na = false; }
    if (ps) { provider->stop(platform); ps = false; }
    if (pa) { provider->detach(platform); pa = false; }
  }
  ~Tb371fcPublicationLifecycle() { rollback(); }
};
