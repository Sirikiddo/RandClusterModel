# RandClusterModel

## Planet model loading

The `Planet/ModelHandler` now exposes a shared-loading path to avoid re-reading mesh
files and duplicating GPU buffers when the same model is requested multiple times.
Use `ModelHandler::loadShared()` to obtain a cached handler by absolute file path,
then call `uploadToGPU()` once per OpenGL context:

```cpp
auto tree = ModelHandler::loadShared("Planet/tree.obj");
if (tree) {
    tree->uploadToGPU();
    // draw via tree->draw(...)
}
```

Repeated calls with the same path return the existing instance, reusing previously
parsed CPU buffers and GPU allocations instead of cloning them.

## Memory check utility

A lightweight console helper lives in `Planet/model_cache_test.cpp`. It loads the
same model twice and prints the RSS after each step to validate that cached loads
avoid additional allocations.

Build example (requires Qt 5/6 development packages and OpenGL headers):

```bash
mkdir -p build && cd build
qmake ../Planet/Planet.pro  # or use your existing CMake/qmake setup
make model_cache_test
./model_cache_test ../Planet/tree.obj
```

## Notes

* `ModelHandler::clearCache()` removes all cached instances if you need to force
  a reload (for example, after assets change on disk).
* Use `ModelHandler::loadedPath()` to introspect which mesh is currently bound to
  an instance.
