# Why `suzume.wasm` and `suzume.js` are committed here

Everything else in `dist/` is generated from TypeScript source by this package's own
`prepare` script (`tsc`) on a plain `npm install` -- no different from any other TS package.

`suzume.wasm` and `suzume.js` (its Emscripten glue) are the exception: producing them from
`src/` requires the full Emscripten SDK (`emcc`/`emcmake`/`cmake`), which is not a reasonable
thing to expect of every consumer's install step. They are committed as prebuilt binary
artifacts instead, the same way you would vendor any other precompiled native dependency.

To rebuild them after changing C++ source or the CMake link flags:

```
source /path/to/emsdk/emsdk_env.sh
make build   # native CLI first -- generates the embedded dictionary data the WASM build needs
make wasm    # emcc build, writes bindings/wasm/dist/suzume.{wasm,js}
```

Then commit the regenerated files alongside the source change that required them.
