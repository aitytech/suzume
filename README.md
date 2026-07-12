# Suzume

[![CI](https://img.shields.io/github/actions/workflow/status/libraz/suzume/ci.yml?branch=main&label=CI)](https://github.com/libraz/suzume/actions)
[![npm](https://img.shields.io/npm/v/@libraz/suzume)](https://www.npmjs.com/package/@libraz/suzume)
[![PyPI](https://img.shields.io/pypi/v/suzume)](https://pypi.org/project/suzume/)
[![codecov](https://codecov.io/gh/libraz/suzume/branch/main/graph/badge.svg)](https://codecov.io/gh/libraz/suzume)
[![License](https://img.shields.io/github/license/libraz/suzume)](https://github.com/libraz/suzume/blob/main/LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue?logo=c%2B%2B)](https://en.cppreference.com/w/cpp/17)

A lightweight Japanese tokenizer that runs in the browser via WebAssembly. Uses feature-based analysis instead of large dictionary files.

[Documentation](https://suzume.libraz.net) | [Live Demo](https://suzume.libraz.net/#demo)

## Overview

Suzume tokenizes Japanese text using character patterns, connection rules, and a small dictionary, rather than the large dictionaries (20-50MB+) used by traditional dictionary-based morphological analyzers. The WASM build is around 424KB gzipped.

| | Traditional Analyzers | Suzume |
|---|---|---|
| **Bundle Size** | 20-50MB+ (dictionary) | <450KB gzipped |
| **Browser Support** | Limited or none | Supported (WASM) |
| **Server Required** | Usually yes | No |
| **POS Tagging** | Yes | Yes |
| **Lemmatization** | Yes | Yes |

### How the output differs

The difference isn't accuracy — it's the *unit*. Suzume splits text into search-friendly tokens (merging compounds, numbers, and dates) rather than minimal morphemes:

```
Input:            データベースで3人が検索する
Dictionary-based: データ / ベース / で / 3 / 人 / が / 検索 / する
Suzume:           データベース / で / 3人 / が / 検索 / する
```

See [Tokenization Differences](https://suzume.libraz.net/docs/mecab-comparison) for the full list of intentional design differences and known limitations.

### Trade-offs

- **Smaller footprint** — No large dictionary download; suitable for frontend, edge, and serverless environments
- **Handles unknown words** — Feature-based analysis doesn't fail on words missing from a dictionary
- **Compounds stay merged** — Without a full dictionary, Suzume cannot split kanji/katakana compounds into their parts (e.g. `東京都庁前` stays one token); register specific split points via the user dictionary

## Installation

JavaScript / TypeScript (browser + Node.js, via WebAssembly):

```bash
npm install @libraz/suzume
# or: yarn add / pnpm add / bun add @libraz/suzume
```

Python (native, via a bundled shared library — no other dependencies):

```bash
pip install suzume
```

## Quick Start

### JavaScript / TypeScript

```typescript
import { Suzume } from '@libraz/suzume'

const suzume = await Suzume.create()

const tokens = suzume.analyze('すもももももももものうち')
for (const t of tokens) {
  console.log(`${t.surface} [${t.posJa}]`)
}

// Tag extraction (returns { tag, pos } objects)
const tags = suzume.generateTags('東京スカイツリーに行きました')
// → [{ tag: '東京', pos: 'NOUN' }, { tag: 'スカイツリー', pos: 'NOUN' }, { tag: '行く', pos: 'VERB' }]

// Nouns only
suzume.generateTags('美味しいラーメンを食べた', { pos: ['noun'] })
// → [{ tag: 'ラーメン', pos: 'NOUN' }]

// Exclude basic words (hiragana-only lemma like する, ある, いい)
suzume.generateTags('今日はいい天気ですね', { excludeBasic: true })
// → [{ tag: '今日', pos: 'NOUN' }, { tag: '天気', pos: 'NOUN' }]
```

### Python

```python
from suzume import Suzume

with Suzume() as sz:
    for m in sz.analyze('すもももももももものうち'):
        print(m.surface, m.pos_ja)

    # Tag extraction (returns Tag objects with .tag / .pos)
    tags = sz.generate_tags('東京に行きました')
    print([(t.tag, t.pos) for t in tags])
```

See [bindings/python/README.md](bindings/python/README.md) for the full Python API.

### Browser (CDN)

```html
<script type="module">
  import { Suzume } from 'https://esm.sh/@libraz/suzume'

  const suzume = await Suzume.create()
  console.log(suzume.analyze('こんにちは'))
</script>
```

### C++

```cpp
#include "suzume.h"

suzume::Suzume tokenizer;
auto tokens = tokenizer.analyze("東京に行きました");

for (const auto& t : tokens) {
    std::cout << t.surface << "\t" << t.lemma << std::endl;
}
```

Build from source (requires C++17, CMake 3.15+):

```bash
make          # Build
make test     # Run tests
```

## Documentation

- [Getting Started](https://suzume.libraz.net/docs/getting-started) — Installation and basic usage
- [API Reference](https://suzume.libraz.net/docs/api) — API documentation
- [User Dictionary](https://suzume.libraz.net/docs/user-dictionary) — Adding custom words
- [How It Works](https://suzume.libraz.net/docs/how-it-works) — Technical details
- [Tokenization Differences](https://suzume.libraz.net/docs/mecab-comparison) — How Suzume differs from dictionary-based analyzers

## License

[Apache License 2.0](LICENSE)
