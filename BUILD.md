# Build

Build the project with CMake presets:

```bash
cmake --preset clang
cmake --build --preset clang-release
cmake --build --preset msvc-debug
cmake --build --preset msvc-release
```

The Clang binary is placed in `build/clang/bath`. MSVC builds are generated under `build/msvc/`.
