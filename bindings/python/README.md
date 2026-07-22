# suzume

[![PyPI](https://img.shields.io/pypi/v/suzume)](https://pypi.org/project/suzume/)
[![npm](https://img.shields.io/npm/v/@libraz/suzume)](https://www.npmjs.com/package/@libraz/suzume)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue)](https://github.com/libraz/suzume/blob/main/LICENSE)
[![Docs](https://img.shields.io/badge/docs-suzume.libraz.net-2563eb)](https://suzume.libraz.net)

**Japanese tokenization for Python applications.** Suzume is not a full
morphological analyzer like MeCab: it prioritizes useful search units while
still providing part-of-speech tags, lemmas, and keyword extraction. The wheel
requires no extra dictionary installation.

📖 **[Full API reference and guides](https://suzume.libraz.net/docs/python)**

See the [MeCab comparison](https://suzume.libraz.net/docs/mecab-comparison) for
concrete examples of token boundaries, lemmatization, and trade-offs.

## Installation

```bash
pip install suzume
```

## Quick Start

```python
from suzume import Suzume

with Suzume() as sz:
    tokens = sz.analyze("すもももももももものうち")
    tags = sz.generate_tags("東京の公園に行きました")
```

See the [Python API](https://suzume.libraz.net/docs/python) for the complete
interface, including user dictionaries and tag-generation options.

## Also available

```bash
npm install @libraz/suzume  # JavaScript / TypeScript WebAssembly package
```

The native C and C++ library is documented at
[suzume.libraz.net/docs/cpp](https://suzume.libraz.net/docs/cpp).

## License

[Apache License 2.0](https://github.com/libraz/suzume/blob/main/LICENSE)
