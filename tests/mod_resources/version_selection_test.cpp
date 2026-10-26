#ifdef NDEBUG
#undef NDEBUG
#endif
#include "mod_runtime.h"
#include "mod_version.h"
#include "content_variant.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

extern "C" void snes_variant_clear_declared(void) {}
extern "C" const SnesContentVariant* snes_variant_register_declared(const SnesVariantDecl*) { return nullptr; }

int main(int argc, char** argv) {
  assert(argc == 2);
  namespace fs = std::filesystem;
  fs::path root = argv[1];
  const std::string digest(64, '0');
  for (const char* version : {"1.9.0", "1.10.0", "1.10.0-rc.1"}) {
    fs::path dir = root / "packages/test.version" / version;
    fs::create_directories(dir);
    std::ofstream f(dir / "manifest.toml");
    f << "format_version = 1\nid = \"test.version\"\nversion = \"" << version
      << "\"\nname = \"Version selection\"\nresolver = \"declarative\"\n"
         "[[target]]\ngame_id = \"test\"\nrom_sha256 = \"" << digest
      << "\"\n[[feature]]\nid = \"v";
    for (char c : std::string(version)) f << (c == '.' ? '_' : c);
    f << "\"\nname = \"version\"\ndefault_enabled = true\n";
  }
  std::string error;
  assert(SNESRecomp::mod_runtime_initialize(root, "test", digest, &error));
  assert(snes_mod_runtime_feature_enabled_c("test.version", "v1_10_0"));
  assert(!snes_mod_runtime_feature_enabled_c("test.version", "v1_9_0"));
  // Exact pins keep working, and a missing pin falls back to the newest.
  for (const char* version : {"1.9.0", "missing"}) {
    std::ofstream state(root / "state.toml");
    state << "format_version = 1\n[[package]]\nid = \"test.version\"\nversion = \""
          << version << "\"\n";
    state.close();
    assert(SNESRecomp::mod_runtime_initialize(root, "test", digest, &error));
    assert(snes_mod_runtime_feature_enabled_c("test.version",
        std::string(version) == "1.9.0" ? "v1_9_0" : "v1_10_0"));
  }
  using ModVersion::less;
  assert(less("1.9.0", "1.10.0"));
  assert(less("1.0.0-alpha.2", "1.0.0-alpha.10"));
  assert(less("1.0.0-rc.1", "1.0.0"));
  assert(less("1.0.0-alpha", "1.0.0-alpha.1"));
  assert(less("1.0.0-1", "1.0.0-alpha"));
  assert(less("1.0.0", "100000000000000000000000.0.0"));
  assert(less("legacy", "1.0.0"));
  assert(less("1.0.0+aaa", "1.0.0+bbb"));
  const std::string labels[] = {"1.2.0", "1.02.0", "1.10.0", "dev", "1.0.0-1", "1.0.0"};
  for (const auto& a : labels) for (const auto& b : labels) for (const auto& c : labels)
    if (less(a, b) && less(b, c)) assert(less(a, c));
}
