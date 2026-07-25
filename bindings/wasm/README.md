# Suzume

[![CI](https://img.shields.io/github/actions/workflow/status/libraz/suzume/ci.yml?branch=main&label=CI)](https://github.com/libraz/suzume/actions)
[![npm](https://img.shields.io/npm/v/@libraz/suzume)](https://www.npmjs.com/package/@libraz/suzume)
[![npm downloads](https://img.shields.io/npm/dm/@libraz/suzume)](https://www.npmjs.com/package/@libraz/suzume)
[![types](https://img.shields.io/npm/types/@libraz/suzume)](https://www.npmjs.com/package/@libraz/suzume)
[![License](https://img.shields.io/github/license/libraz/suzume)](https://github.com/libraz/suzume/blob/main/LICENSE)
[![Docs](https://img.shields.io/badge/docs-suzume.libraz.net-2563eb)](https://suzume.libraz.net)
[![PyPI](https://img.shields.io/pypi/v/suzume?label=PyPI)](https://pypi.org/project/suzume/)

**Japanese tokenization for browser and Node.js applications.** Suzume is not a
full morphological analyzer like MeCab: it prioritizes useful search units while
still providing part-of-speech tags, lemmas, and keyword extraction in one
WebAssembly package.

📖 **[Documentation & getting started](https://suzume.libraz.net/docs/getting-started)** &nbsp;·&nbsp; 🧪 **[Live Demo](https://suzume.libraz.net/#demo)**

See the [MeCab comparison](https://suzume.libraz.net/docs/mecab-comparison) for
concrete examples of token boundaries, lemmatization, and trade-offs.

## Installation

```bash
npm install @libraz/suzume
```

## Quick Start

```typescript
import { Suzume } from '@libraz/suzume'

const suzume = await Suzume.create()

const tokens = suzume.analyze('すもももももももものうち')
const tags = suzume.generateTags('東京の公園に行きました')

suzume.destroy() // optional immediate cleanup
```

## Loading the `.wasm` file

Bundlers that do not resolve the WebAssembly asset automatically can pass its
URL explicitly:

```typescript
import wasmUrl from '@libraz/suzume/wasm?url' // Vite

const suzume = await Suzume.create({ wasmPath: wasmUrl })
```

For CDN usage, user dictionaries, and the full API, see the
[JavaScript / TypeScript guide](https://suzume.libraz.net/docs/api).

## Error handling

A failing native call throws an `Error` carrying the message from the
WebAssembly module, so ordinary failures — a rejected user dictionary, use
after `destroy()` — are catchable:

```typescript
try {
  suzume.loadUserDictionaryOrThrow(csv)
} catch (error) {
  // message comes from the module
}
```

Running out of memory is the exception. The module is compiled without C++
exceptions, so an allocation failure aborts the WebAssembly instance rather
than returning an error, and no `catch` can recover from it. The instance is
unusable afterwards. Analyze long documents in chunks rather than relying on
error handling to survive a large input.

## Also available

```bash
pip install suzume  # Native Python bindings
```

The C and C++ library is documented at
[suzume.libraz.net/docs/cpp](https://suzume.libraz.net/docs/cpp).

## License

[Apache License 2.0](https://github.com/libraz/suzume/blob/main/LICENSE)
