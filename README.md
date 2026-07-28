# Suzume

[![CI](https://img.shields.io/github/actions/workflow/status/libraz/suzume/ci.yml?branch=main&label=CI)](https://github.com/libraz/suzume/actions)
[![npm](https://img.shields.io/npm/v/@libraz/suzume)](https://www.npmjs.com/package/@libraz/suzume)
[![PyPI](https://img.shields.io/pypi/v/suzume)](https://pypi.org/project/suzume/)
[![codecov](https://codecov.io/gh/libraz/suzume/branch/main/graph/badge.svg)](https://codecov.io/gh/libraz/suzume)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue)](https://github.com/libraz/suzume/blob/main/LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue?logo=c%2B%2B)](https://en.cppreference.com/w/cpp/17)
[![Platform](https://img.shields.io/badge/platform-Browser%20%7C%20Node.js%20%7C%20Python%20%7C%20Go%20%7C%20C%2B%2B-lightgrey)](https://github.com/libraz/suzume)
[![Docs](https://img.shields.io/badge/docs-suzume.libraz.net-2563eb)](https://suzume.libraz.net)

**Suzume is a lightweight Japanese tokenizer for browsers and native apps.** It
is not a full morphological analyzer like MeCab: its primary goal is to split
text into useful units for search, display, and application code. Unlike a
boundary-only tokenizer, it also returns part-of-speech tags and lemmas.

**Reach for it when you need to:**

- **Tokenize Japanese anywhere** — use the same tokenizer in a browser, serverless function, Python process, Go service, or C/C++ application.
- **Extract search terms** — generate keyword tags with POS filtering, lemmas, and duplicate removal.
- **Add your vocabulary** — load application-specific words through a user dictionary.

📖 **[Documentation](https://suzume.libraz.net)** &nbsp;·&nbsp; 🧪 **[Live Demo](https://suzume.libraz.net/#demo)** &nbsp;·&nbsp; **[Getting Started](https://suzume.libraz.net/docs/getting-started)**

## How is it different from MeCab?

MeCab is a morphological analyzer designed for detailed, dictionary-based
analysis. Suzume is a tokenizer designed around practical token boundaries. It
uses character patterns and compact rules, keeping compounds and quantities
together when that produces a more useful search unit.

```text
Input:  経済成長     3人
MeCab:  経済 / 成長  3 / 人
Suzume: 経済成長     3人
```

Suzume still provides POS tagging and lemmatization, so applications can
normalize inflected verbs and adjectives without adopting MeCab-style output.
The outputs are intentionally different rather than drop-in compatible. See
[Differences from MeCab](https://suzume.libraz.net/docs/mecab-comparison) for
examples, trade-offs, and known constraints.

## Install

```bash
npm install @libraz/suzume            # JavaScript / TypeScript
pip install suzume                    # Python
go get github.com/libraz/go-suzume    # Go
```

The Python wheel also installs the `suzume` command. PyPI provides binary
wheels for Linux x86_64 and macOS arm64; Windows, other architectures, and
source distributions are not supported.

The Go module is maintained and versioned in a separate repository; it is not
built or compatibility-gated by this repository. It ships no precompiled
binary, builds the static library from source once via `make lib` in the module
directory, and embeds the dictionaries in your binary.

For C/C++ installation, native builds, user dictionaries, and all runtime
options, see the [documentation](https://suzume.libraz.net/docs/getting-started).

## Quick Start

```typescript
import { Suzume } from '@libraz/suzume'

const suzume = await Suzume.create()
const tokens = suzume.analyze('すもももももももものうち')
const tags = suzume.generateTags('東京の公園に行きました')

suzume.destroy() // optional immediate cleanup
```

```python
from suzume import Suzume

with Suzume() as sz:
    tokens = sz.analyze("すもももももももものうち")
    tags = sz.generate_tags("東京の公園に行きました")
```

```go
s, err := suzume.New()
if err != nil {
	log.Fatal(err)
}
defer s.Close()

morphemes := s.Analyze("すもももももももものうち")
```

The same Python package provides a command-line interface:

```bash
suzume "東京へ行く"
suzume --mode search --format json "東京の公園"
printf 'りんごを食べる\n' | suzume --format tags
```

Run `suzume --help` for analysis and tag options. Dictionary compilation,
validation, and test commands belong to the native developer tool,
`suzume-cli`, built from the CMake project.

## Documentation

- [Getting Started](https://suzume.libraz.net/docs/getting-started)
- [JavaScript / TypeScript API](https://suzume.libraz.net/docs/api)
- [Python API](https://suzume.libraz.net/docs/python)
- [Go Bindings](https://suzume.libraz.net/docs/go)
- [C / C++ Library](https://suzume.libraz.net/docs/cpp)
- [User Dictionary](https://suzume.libraz.net/docs/user-dictionary)

## License

[Apache License 2.0](LICENSE)
