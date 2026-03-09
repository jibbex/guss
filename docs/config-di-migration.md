# Config: Singleton → Dependency Injection Migration Plan

## Problem

`Config` is a Meyer's singleton (`static Config instance(cfg_path)` inside `instance()`).
The static local is initialized exactly once — subsequent calls with different paths update
`cfg_path` but cannot reconstruct the already-live object.

This breaks `test_config.cpp`: six tests each write a fresh YAML file and call
`Config::instance(&path)`, but only the first test (`ParsesGhostAdapter`) ever gets the right
config. All others see the Ghost config, causing four assertion failures.

## Key observation: the codebase is already 90 % DI

`Pipeline` already receives `SiteConfig`, `TemplateConfig`, `PermalinkConfig`, and
`OutputConfig` as constructor arguments — it never calls `Config::instance()` internally.
`GhostAdapter` receives `GhostAdapterConfig` by value in its constructor.
The sub-structs (`SiteConfig`, `OutputConfig`, …) are already plain, copyable value types.

The singleton is only called in three places in `main.cpp` (`cmd_build`, `cmd_ping`,
`cmd_clean`) and in the test file. Removing it is a narrow, contained change.

## Target design

`Config` becomes a regular value type: construct it on the stack, pass it into whatever needs
it, let it go out of scope normally. No global state.

```cpp
// Before (main.cpp)
const auto& config = guss::config::Config::instance(&cfg_path);

// After (main.cpp)
auto config = guss::config::Config(cfg_path);          // or via load_config()

// Before (test_config.cpp)
const auto& config = guss::config::Config::instance(&path_str);

// After (test_config.cpp)
guss::config::Config config(path_str);
```

---

## Files to change

### 1. `include/guss/core/config.hpp`

**Remove:**
- `static const Config& instance(std::optional<const std::string*> config_path);`
- `static const Config& instance();`
- `Config() = delete;`
- `Config(const Config&) = delete;`
- `Config& operator=(const Config&) = delete;`
- `Config(Config&&) = delete;`

**Change:**
- Move `explicit Config(std::string_view config_path);` from `private` to `public`.
- Let the compiler generate copy and move (all member fields — `std::string`,
  `std::filesystem::path`, `std::variant`, `std::optional<std::string>`, `int`, `bool` — are
  already copyable and movable).

**Change `load_config` signature** (free function at bottom of header):
```cpp
// Before
error::VoidResult load_config(const std::filesystem::path& path);

// After
error::Result<Config> load_config(const std::filesystem::path& path);
```
Callers that want error-checked construction use `load_config()`; callers that want to throw
or handle errors inline construct `Config` directly.

No other headers need to change — they `#include "guss/core/config.hpp"` only to use the
sub-struct types (`SiteConfig`, etc.), which are unaffected.

---

### 2. `src/core/config.cpp`

**Remove entirely:**
- Both `Config::instance(...)` implementations.

**Change `load_config`:**
```cpp
// Before: triggers singleton, returns VoidResult
error::VoidResult load_config(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) { ... }
    const std::string path_str = path.string();
    Config::instance(&path_str);
    return {};
}

// After: constructs a Config and returns it (or error)
error::Result<Config> load_config(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return error::make_error(
            error::ErrorCode::ConfigNotFound,
            "Configuration file not found",
            path.string()
        );
    }
    try {
        return Config(path.string());
    } catch (const YAML::Exception& e) {
        return error::make_error(
            error::ErrorCode::ConfigParseError,
            std::string("YAML parse error: ") + e.what(),
            path.string()
        );
    }
}
```

`validate_yaml()` is already standalone — no change required.

---

### 3. `src/cli/main.cpp`

Three `cmd_*` functions each do:
```cpp
const std::string& cfg_path = config_path;
const auto& config = guss::config::Config::instance(&cfg_path);
```

Replace each with:
```cpp
guss::config::Config config(config_path);
```

Or, if the file-not-found error path is important at the CLI level:
```cpp
auto config_result = guss::config::load_config(config_path);
if (!config_result) {
    spdlog::error("Failed to load config: {}", config_result.error().format());
    return 1;
}
auto& config = *config_result;
```

The rest of each function (`config.site()`, `config.templates()`, etc.) is unchanged.

Affected functions: `cmd_build` (line 487), `cmd_ping` (line 573), `cmd_clean` (line 613).

---

### 4. `tests/test_config.cpp`

Six tests each call `Config::instance(&path_str)`. Replace every occurrence with a local:
```cpp
// Before
const auto& config = guss::config::Config::instance(&path_str);

// After
guss::config::Config config(path_str);
```

No fixture changes needed — `SetUp`/`TearDown` already manage the temp directory; the Config
object now lives as a local in each `TEST_F` body and is destroyed at the end of it.

---

## What does NOT change

| Component | Reason |
|-----------|--------|
| `Pipeline` constructor | Already takes sub-structs by value — no `Config` in sight |
| `GhostAdapter` constructor | Already takes `GhostAdapterConfig` by value |
| All other adapter headers | `#include "config.hpp"` for sub-struct types only |
| `permalink.hpp` / `PermalinkGenerator` | Takes `PermalinkConfig` by value already |
| Sub-struct definitions (`SiteConfig`, etc.) | Pure value types, unchanged |
| `validate_yaml()` | Standalone free function, unchanged |
| `CMakeLists.txt` | No new files, no new targets |

---

## Migration sequence

1. `include/guss/core/config.hpp` — expose constructor, remove singleton API, update
   `load_config` signature.
2. `src/core/config.cpp` — delete `instance()` bodies, update `load_config` body.
3. `src/cli/main.cpp` — update three call sites.
4. `tests/test_config.cpp` — replace six `instance()` calls with local construction.
5. Build and run `ctest` to confirm all config tests pass.

Steps 1–2 must happen together (compiler will reject the removed declarations immediately).
Steps 3–4 are independent once step 2 compiles.
