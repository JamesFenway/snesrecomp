#pragma once
#include <algorithm>
#include <string>
#include <vector>

// Keep the launcher and snesrecomp's mod_version.h ordering in sync. Package
// keys remain strings: explicit version pins still mean an exact match.
namespace ModVersion {
inline bool numeric(const std::string& s) {
  return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}
inline std::vector<std::string> split(const std::string& s) {
  std::vector<std::string> out;
  size_t at = 0, end;
  do {
    end = s.find('.', at);
    out.push_back(s.substr(at, end == std::string::npos ? end : end - at));
    at = end == std::string::npos ? s.size() : end + 1;
  } while (end != std::string::npos);
  return out;
}
struct Version { std::vector<std::string> core, pre; bool valid = false; };
inline Version parse(const std::string& text) {
  Version v;
  size_t plus = text.find('+');
  std::string base = text.substr(0, plus);
  size_t dash = base.find('-');
  v.core = split(base.substr(0, dash));
  if (v.core.size() != 3) return v;
  for (const auto& n : v.core)
    if (!numeric(n) || (n.size() > 1 && n[0] == '0')) return v;
  auto identifiers = [](const std::vector<std::string>& ids, bool pre) {
    for (const auto& id : ids) {
      if (id.empty()) return false;
      for (char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-')) return false;
      if (pre && numeric(id) && id.size() > 1 && id[0] == '0') return false;
    }
    return true;
  };
  if (dash != std::string::npos) {
    v.pre = split(base.substr(dash + 1));
    if (!identifiers(v.pre, true)) return v;
  }
  if (plus != std::string::npos && !identifiers(split(text.substr(plus + 1)), false)) return v;
  v.valid = true;
  return v;
}
inline int number_compare(const std::string& a, const std::string& b) {
  // Compare decimal strings without an integer limit.
  if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
  return a.compare(b);
}
inline bool less(const std::string& a, const std::string& b) {
  Version x = parse(a), y = parse(b);
  // Legacy non-SemVer labels remain selectable; valid releases rank above
  // them. Never mix numeric and lexical comparisons pairwise (not transitive).
  if (x.valid != y.valid) return !x.valid;
  if (!x.valid) return a < b;
  for (size_t i = 0; i < 3; ++i) {
    int c = number_compare(x.core[i], y.core[i]);
    if (c) return c < 0;
  }
  if (x.pre.empty() != y.pre.empty()) return !x.pre.empty();
  for (size_t i = 0; i < std::min(x.pre.size(), y.pre.size()); ++i) {
    if (x.pre[i] == y.pre[i]) continue;
    bool xn = numeric(x.pre[i]), yn = numeric(y.pre[i]);
    if (xn != yn) return xn;
    return xn ? number_compare(x.pre[i], y.pre[i]) < 0 : x.pre[i] < y.pre[i];
  }
  if (x.pre.size() != y.pre.size()) return x.pre.size() < y.pre.size();
  // Build metadata has equal precedence; use a stable lexical tie-break so
  // directory enumeration order cannot change the selected package.
  return a < b;
}
}  // namespace ModVersion
