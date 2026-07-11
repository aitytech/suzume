"""Guard the ctypes struct mirrors against the native ABI.

ctypes reads raw struct memory, so a field-offset or size mismatch between the
Python declarations in ``suzume._ffi`` and the compiled C ABI is a segfault
waiting to happen, not a compile error. The library exports sizeof/offsetof
oracles precisely so this test can assert byte-for-byte agreement.
"""

from __future__ import annotations

import ctypes

import suzume
from suzume._ffi import (
    SuzumeExtendedOptions,
    SuzumeMorpheme,
    SuzumeResult,
    SuzumeTagOptions,
    SuzumeTags,
)

_lib = suzume._lib


def _offsets(struct: type[ctypes.Structure]) -> list[int]:
    return [getattr(struct, name).offset for name, *_ in struct._fields_]


def test_sizeof_matches_native() -> None:
    assert ctypes.sizeof(SuzumeMorpheme) == _lib.suzume_sizeof_morpheme()
    assert ctypes.sizeof(SuzumeResult) == _lib.suzume_sizeof_result()
    assert ctypes.sizeof(SuzumeTags) == _lib.suzume_sizeof_tags()
    assert ctypes.sizeof(SuzumeTagOptions) == _lib.suzume_sizeof_tag_options()
    assert ctypes.sizeof(SuzumeExtendedOptions) == _lib.suzume_sizeof_extended_options()


def test_morpheme_offsets_match_native() -> None:
    for idx, offset in enumerate(_offsets(SuzumeMorpheme)):
        assert offset == _lib.suzume_offsetof_morpheme(idx), f"morpheme field {idx}"


def test_result_offsets_match_native() -> None:
    for idx, offset in enumerate(_offsets(SuzumeResult)):
        assert offset == _lib.suzume_offsetof_result(idx), f"result field {idx}"


def test_tags_offsets_match_native() -> None:
    for idx, offset in enumerate(_offsets(SuzumeTags)):
        assert offset == _lib.suzume_offsetof_tags(idx), f"tags field {idx}"


def test_tag_options_offsets_match_native() -> None:
    for idx, offset in enumerate(_offsets(SuzumeTagOptions)):
        assert offset == _lib.suzume_offsetof_tag_options(idx), f"tag_options field {idx}"


def test_extended_options_offsets_match_native() -> None:
    for idx, offset in enumerate(_offsets(SuzumeExtendedOptions)):
        assert offset == _lib.suzume_offsetof_extended_options(idx), f"extended_options field {idx}"
