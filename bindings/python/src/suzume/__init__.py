"""Suzume — lightweight Japanese morphological analyzer.

Dictionary-independent tokenizer with part-of-speech tagging, distributed as a
thin ctypes binding over the native suzume C-ABI library.

Example:
    >>> from suzume import Suzume
    >>> with Suzume() as sz:
    ...     for m in sz.analyze("東京都に住む"):
    ...         print(m.surface, m.pos)
"""

from __future__ import annotations

import ctypes
from collections.abc import Iterable
from dataclasses import dataclass
from enum import Enum
from types import TracebackType

from ._ffi import (
    SuzumeExtendedOptions,
    SuzumeTagOptions,
    load_library,
)

__all__ = [
    "Suzume",
    "Morpheme",
    "Tag",
    "Mode",
    "SuzumeError",
    "version",
]

_lib = load_library()

# POS names accepted by ``generate_tags(pos_filter=...)``, mapped to their
# bitmask value. Names and casing mirror the WASM binding's TagOptions.pos.
_POS_FILTER_BITS = {"noun": 1, "verb": 2, "adjective": 4, "adverb": 8}


def _resolve_pos_filter(pos_filter: int | Iterable[str]) -> int:
    """Resolve a ``pos_filter`` argument to a native bitmask.

    Accepts a raw bitmask ``int`` (passed through unchanged) or an iterable of
    POS names (``"noun"``, ``"verb"``, ``"adjective"``, ``"adverb"``) whose bits
    are OR-ed together.
    """
    if isinstance(pos_filter, int):
        return pos_filter
    mask = 0
    for name in pos_filter:
        try:
            mask |= _POS_FILTER_BITS[name]
        except KeyError:
            raise ValueError(
                f"unknown POS filter name: {name!r} "
                f"(expected one of {sorted(_POS_FILTER_BITS)})"
            ) from None
    return mask


class SuzumeError(RuntimeError):
    """Raised when a native suzume call fails."""


class Mode(str, Enum):
    """Analysis mode passed to :class:`Suzume`."""

    NORMAL = "normal"
    SEARCH = "search"
    SPLIT = "split"

    @property
    def _code(self) -> int:
        return {Mode.NORMAL: 0, Mode.SEARCH: 1, Mode.SPLIT: 2}[self]


@dataclass(frozen=True, kw_only=True)
class Morpheme:
    """A single analyzed token."""

    surface: str
    pos: str
    base_form: str
    pos_ja: str
    conj_type: str | None = None
    conj_form: str | None = None
    extended_pos: str
    start: int
    end: int
    is_user_dict: bool
    is_formal_noun: bool
    is_low_info: bool
    is_unknown: bool
    is_from_dictionary: bool
    score: float


@dataclass(frozen=True)
class Tag:
    """A generated tag: the keyword text plus its part of speech."""

    tag: str
    pos: str


def _decode(value: bytes | None) -> str:
    return value.decode("utf-8") if value else ""


def _decode_optional(value: bytes | None) -> str | None:
    """Decode a native string, mapping the empty string to ``None``.

    A non-conjugating word yields an empty native conjugation field; expose that
    as ``None`` rather than ``""`` to match the WASM binding's null semantics.
    """
    return value.decode("utf-8") if value else None


def version() -> str:
    """Return the native library version string."""
    return _decode(_lib.suzume_version())


class Suzume:
    """A reusable analyzer instance.

    Instances hold a native handle; use as a context manager or call
    :meth:`close` when done. Not thread-safe — use one instance per thread.
    """

    def __init__(
        self,
        *,
        mode: Mode | str = Mode.NORMAL,
        preserve_vu: bool = True,
        preserve_case: bool = True,
        preserve_symbols: bool = False,
        lemmatize: bool = True,
        merge_compounds: bool = False,
    ) -> None:
        mode = Mode(mode)
        opts = SuzumeExtendedOptions()
        _lib.suzume_init_extended_options(ctypes.byref(opts))
        opts.preserve_vu = int(preserve_vu)
        opts.preserve_case = int(preserve_case)
        opts.preserve_symbols = int(preserve_symbols)
        opts.mode = mode._code
        opts.lemmatize = int(lemmatize)
        opts.merge_compounds = int(merge_compounds)

        handle = _lib.suzume_create_with_extended_options(ctypes.byref(opts))
        if not handle:
            raise SuzumeError(
                _decode(_lib.suzume_last_error()) or "failed to create Suzume instance"
            )
        self._handle: int | None = handle

    # --- lifecycle ---

    def close(self) -> None:
        """Release the native handle. Idempotent."""
        if getattr(self, "_handle", None) is not None:
            _lib.suzume_destroy(self._handle)
            self._handle = None

    def __del__(self) -> None:
        self.close()

    def __enter__(self) -> Suzume:
        return self

    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc: BaseException | None,
        tb: TracebackType | None,
    ) -> None:
        self.close()

    def _require_handle(self) -> int:
        if self._handle is None:
            raise SuzumeError("Suzume instance has been closed")
        return self._handle

    # --- analysis ---

    def analyze(self, text: str) -> list[Morpheme]:
        """Analyze ``text`` into a list of :class:`Morpheme`."""
        handle = self._require_handle()
        result = _lib.suzume_analyze(handle, text.encode("utf-8"))
        if not result:
            raise SuzumeError(_decode(_lib.suzume_last_error()) or "analysis failed")
        try:
            data = result.contents
            out: list[Morpheme] = []
            for idx in range(data.count):
                m = data.morphemes[idx]
                out.append(
                    Morpheme(
                        surface=_decode(m.surface),
                        pos=_decode(m.pos),
                        base_form=_decode(m.base_form),
                        pos_ja=_decode(m.pos_ja),
                        conj_type=_decode_optional(m.conj_type),
                        conj_form=_decode_optional(m.conj_form),
                        extended_pos=_decode(m.extended_pos),
                        start=int(m.start),
                        end=int(m.end),
                        is_user_dict=bool(m.is_user_dict),
                        is_formal_noun=bool(m.is_formal_noun),
                        is_low_info=bool(m.is_low_info),
                        is_unknown=bool(m.is_unknown),
                        is_from_dictionary=bool(m.is_from_dictionary),
                        score=float(m.score),
                    )
                )
            return out
        finally:
            _lib.suzume_result_free(result)

    def generate_tags(
        self,
        text: str,
        *,
        pos_filter: int | Iterable[str] = 0,
        exclude_basic: bool = False,
        use_lemma: bool = True,
        min_length: int = 2,
        max_tags: int = 0,
        exclude_particles: bool = True,
        exclude_auxiliaries: bool = True,
        exclude_formal_nouns: bool = True,
        exclude_low_info: bool = True,
        remove_duplicates: bool = True,
    ) -> list[Tag]:
        """Generate keyword tags from ``text``.

        ``pos_filter`` selects which parts of speech to keep. Pass either a raw
        bitmask ``int`` (1=noun, 2=verb, 4=adjective, 8=adverb; 0=all) or an
        iterable of POS names, e.g. ``["noun", "verb"]``, whose bits are OR-ed
        together. All other flags map directly to the native tag options.
        """
        handle = self._require_handle()
        opts = SuzumeTagOptions(
            pos_filter=_resolve_pos_filter(pos_filter),
            exclude_basic=int(exclude_basic),
            use_lemma=int(use_lemma),
            min_length=min_length,
            max_tags=max_tags,
            exclude_particles=int(exclude_particles),
            exclude_auxiliaries=int(exclude_auxiliaries),
            exclude_formal_nouns=int(exclude_formal_nouns),
            exclude_low_info=int(exclude_low_info),
            remove_duplicates=int(remove_duplicates),
        )
        result = _lib.suzume_generate_tags_with_options(
            handle, text.encode("utf-8"), ctypes.byref(opts)
        )
        if not result:
            raise SuzumeError(_decode(_lib.suzume_last_error()) or "tag generation failed")
        try:
            data = result.contents
            out: list[Tag] = []
            for idx in range(data.count):
                out.append(Tag(tag=_decode(data.tags[idx]), pos=_decode(data.pos[idx])))
            return out
        finally:
            _lib.suzume_tags_free(result)

    # --- dictionaries ---

    def load_user_dict(self, csv: str) -> None:
        """Load a user dictionary from CSV text."""
        handle = self._require_handle()
        payload = csv.encode("utf-8")
        if not _lib.suzume_load_user_dict(handle, payload, len(payload)):
            raise SuzumeError(_decode(_lib.suzume_last_error()) or "failed to load user dictionary")

    def load_binary_dict(self, data: bytes) -> None:
        """Load a compiled binary (.dic) dictionary from memory."""
        handle = self._require_handle()
        buf = (ctypes.c_uint8 * len(data)).from_buffer_copy(data)
        if not _lib.suzume_load_binary_dict(handle, buf, len(data)):
            raise SuzumeError(
                _decode(_lib.suzume_last_error()) or "failed to load binary dictionary"
            )

    @property
    def dictionary_warnings(self) -> list[str]:
        """Warnings emitted while auto-loading dictionaries."""
        handle = self._require_handle()
        count = _lib.suzume_dictionary_warning_count(handle)
        return [_decode(_lib.suzume_dictionary_warning(handle, idx)) for idx in range(count)]
