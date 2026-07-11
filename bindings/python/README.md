# suzume

[![PyPI](https://img.shields.io/pypi/v/suzume.svg)](https://pypi.org/project/suzume/)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](https://github.com/libraz/suzume/blob/main/LICENSE)
[![Python](https://img.shields.io/pypi/pyversions/suzume.svg)](https://pypi.org/project/suzume/)

Lightweight, dictionary-independent Japanese morphological analyzer (tokenizer +
part-of-speech tagging). This package is a thin [ctypes](https://docs.python.org/3/library/ctypes.html)
binding over the native suzume C++ core; the compiled library and dictionaries
are bundled in the wheel, so there is nothing else to install.

## Installation

```bash
pip install suzume
```

## Quick start

```python
from suzume import Suzume

with Suzume() as sz:
    for m in sz.analyze("東京都に住む"):
        print(m.surface, m.pos, m.base_form)
```

Generate keyword tags:

```python
from suzume import Suzume

with Suzume() as sz:
    for tag in sz.generate_tags("東京都に住んでいます"):
        print(tag.text, tag.pos)
```

## API

### `Suzume(*, mode="normal", preserve_vu=False, preserve_case=True, preserve_symbols=False, lemmatize=True, merge_compounds=False)`

An analyzer instance. Use it as a context manager (or call `close()`); it is not
thread-safe, so use one instance per thread.

- `mode` — `"normal"`, `"search"`, or `"split"` (or a `Mode` enum member).
- `analyze(text) -> list[Morpheme]` — tokenize with full POS information.
- `generate_tags(text, **options) -> list[Tag]` — extract keyword tags.
- `load_user_dict(csv)` — add a user dictionary from CSV text.
- `load_binary_dict(data)` — load a compiled `.dic` dictionary from memory.
- `dictionary_warnings` — warnings raised while auto-loading dictionaries.

### `Morpheme`

A frozen dataclass with `surface`, `pos`, `base_form`, `pos_ja`, `conj_type`,
`conj_form`, `extended_pos`, `start`, `end`, the `is_*` flags, and `score`.

### `Tag`

A frozen dataclass with `text` and `pos`.

### `suzume.version()`

Returns the native library version string.

## License

Apache-2.0. See [LICENSE](https://github.com/libraz/suzume/blob/main/LICENSE).
