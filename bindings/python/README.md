# suzume

[![PyPI](https://img.shields.io/pypi/v/suzume.svg)](https://pypi.org/project/suzume/)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](https://github.com/libraz/suzume/blob/main/LICENSE)
[![Python](https://img.shields.io/pypi/pyversions/suzume.svg)](https://pypi.org/project/suzume/)

Lightweight, dictionary-independent Japanese morphological analyzer (tokenizer +
part-of-speech tagging). This package is a thin [ctypes](https://docs.python.org/3/library/ctypes.html)
binding over the native suzume C++ core; the compiled library and dictionaries
are bundled in the wheel, so there is nothing else to install.

Full documentation: [suzume.libraz.net/docs/python](https://suzume.libraz.net/docs/python).

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
        print(tag.tag, tag.pos)

    # Restrict to selected parts of speech by name (or a raw bitmask).
    nouns = sz.generate_tags("美味しいラーメンを食べた", pos_filter=["noun"])
```

## API

### `Suzume(*, mode="normal", preserve_vu=True, preserve_case=True, preserve_symbols=False, lemmatize=True, merge_compounds=False)`

An analyzer instance. Use it as a context manager (or call `close()`); it is not
thread-safe, so use one instance per thread.

- `mode` — `"normal"`, `"search"`, or `"split"` (or a `Mode` enum member).
- `analyze(text) -> list[Morpheme]` — tokenize with full POS information.
- `generate_tags(text, **options) -> list[Tag]` — extract keyword tags. The
  `pos_filter` option accepts a raw bitmask (1=noun, 2=verb, 4=adjective,
  8=adverb) or an iterable of POS names such as `["noun", "verb"]`.
- `load_user_dict(csv)` — add a user dictionary from CSV text.
- `load_binary_dict(data)` — load a compiled `.dic` dictionary from memory.
- `dictionary_warnings` — warnings raised while auto-loading dictionaries.

### `Morpheme`

A frozen dataclass with `surface`, `pos`, `base_form`, `pos_ja`, `conj_type`,
`conj_form`, `extended_pos`, `start`, `end`, the `is_*` flags, and `score`.
`conj_type` and `conj_form` are `None` for non-conjugating words.

### `Tag`

A frozen dataclass with `tag` and `pos`.

### `suzume.version()`

Returns the native library version string.

## License

Apache-2.0. See [LICENSE](https://github.com/libraz/suzume/blob/main/LICENSE).
