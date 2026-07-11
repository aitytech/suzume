"""Low-level ctypes wrapper for the suzume shared C-ABI library.

This module owns library discovery, the ``ctypes.Structure`` mirrors of the
public C ABI (``include/suzume/suzume_c.h``), and the typed function
signatures. Nothing here is part of the public Python API; consumers use
:mod:`suzume`.
"""

from __future__ import annotations

import ctypes
import ctypes.util
import os
import platform
from pathlib import Path

# --- ctypes structure mirrors of include/suzume/suzume_c.h ---------------------
#
# Field order and types MUST match the C ABI exactly. tests/test_abi_layout.py
# asserts every sizeof/offsetof against the suzume_sizeof_*/suzume_offsetof_*
# helpers exported by the library, so silent drift here is a caught error rather
# than a segfault.


class SuzumeMorpheme(ctypes.Structure):
    _fields_ = [
        ("surface", ctypes.c_char_p),
        ("pos", ctypes.c_char_p),
        ("base_form", ctypes.c_char_p),
        ("pos_ja", ctypes.c_char_p),
        ("conj_type", ctypes.c_char_p),
        ("conj_form", ctypes.c_char_p),
        ("extended_pos", ctypes.c_char_p),
        ("start", ctypes.c_size_t),
        ("end", ctypes.c_size_t),
        ("is_user_dict", ctypes.c_int),
        ("is_formal_noun", ctypes.c_int),
        ("is_low_info", ctypes.c_int),
        ("is_unknown", ctypes.c_int),
        ("is_from_dictionary", ctypes.c_int),
        ("score", ctypes.c_float),
    ]


class SuzumeResult(ctypes.Structure):
    _fields_ = [
        ("morphemes", ctypes.POINTER(SuzumeMorpheme)),
        ("count", ctypes.c_size_t),
    ]


class SuzumeTags(ctypes.Structure):
    _fields_ = [
        ("tags", ctypes.POINTER(ctypes.c_char_p)),
        ("pos", ctypes.POINTER(ctypes.c_char_p)),
        ("count", ctypes.c_size_t),
    ]


class SuzumeExtendedOptions(ctypes.Structure):
    _fields_ = [
        ("size", ctypes.c_uint32),
        ("preserve_vu", ctypes.c_int),
        ("preserve_case", ctypes.c_int),
        ("preserve_symbols", ctypes.c_int),
        ("mode", ctypes.c_int),
        ("lemmatize", ctypes.c_int),
        ("merge_compounds", ctypes.c_int),
    ]


class SuzumeTagOptions(ctypes.Structure):
    _fields_ = [
        ("pos_filter", ctypes.c_uint8),
        ("exclude_basic", ctypes.c_int),
        ("use_lemma", ctypes.c_int),
        ("min_length", ctypes.c_size_t),
        ("max_tags", ctypes.c_size_t),
        ("exclude_particles", ctypes.c_int),
        ("exclude_auxiliaries", ctypes.c_int),
        ("exclude_formal_nouns", ctypes.c_int),
        ("exclude_low_info", ctypes.c_int),
        ("remove_duplicates", ctypes.c_int),
    ]


# --- Library discovery ---------------------------------------------------------


def _lib_filename() -> str:
    system = platform.system()
    if system == "Darwin":
        return "libsuzume.dylib"
    if system == "Windows":
        return "suzume.dll"
    return "libsuzume.so"


def _find_library() -> str:
    """Locate the suzume shared library.

    Search order:
        1. ``SUZUME_LIB_PATH`` environment variable (explicit override).
        2. Package-adjacent copy (the layout inside a built wheel).
        3. ``build/lib`` under the project root (editable/source checkouts).
        4. The system library path.
    """
    env_path = os.environ.get("SUZUME_LIB_PATH")
    if env_path and Path(env_path).exists():
        return env_path

    pkg_dir = Path(__file__).parent
    lib_name = _lib_filename()

    candidate = pkg_dir / lib_name
    if candidate.exists():
        return str(candidate)

    # Source checkout: bindings/python/src/suzume/_ffi.py -> project root is 4 up.
    project_root = pkg_dir.parents[3]
    for build_dir in ("build-python", "build-shared", "build"):
        build_path = project_root / build_dir / "lib" / lib_name
        if build_path.exists():
            return str(build_path)

    found = ctypes.util.find_library("suzume")
    if found:
        return found

    raise OSError(
        f"suzume shared library ({lib_name}) not found. Set SUZUME_LIB_PATH, or "
        "build it with: cmake -B build-shared -DBUILD_SHARED=ON && "
        "cmake --build build-shared --target suzume_shared"
    )


def _configure_signatures(lib: ctypes.CDLL) -> None:
    """Attach argtypes/restype to every function the binding calls."""
    handle = ctypes.c_void_p

    lib.suzume_create.restype = handle
    lib.suzume_create.argtypes = []

    lib.suzume_init_extended_options.restype = None
    lib.suzume_init_extended_options.argtypes = [ctypes.POINTER(SuzumeExtendedOptions)]

    lib.suzume_create_with_extended_options.restype = handle
    lib.suzume_create_with_extended_options.argtypes = [ctypes.POINTER(SuzumeExtendedOptions)]

    lib.suzume_destroy.restype = None
    lib.suzume_destroy.argtypes = [handle]

    lib.suzume_analyze.restype = ctypes.POINTER(SuzumeResult)
    lib.suzume_analyze.argtypes = [handle, ctypes.c_char_p]

    lib.suzume_result_free.restype = None
    lib.suzume_result_free.argtypes = [ctypes.POINTER(SuzumeResult)]

    lib.suzume_generate_tags.restype = ctypes.POINTER(SuzumeTags)
    lib.suzume_generate_tags.argtypes = [handle, ctypes.c_char_p]

    lib.suzume_generate_tags_with_options.restype = ctypes.POINTER(SuzumeTags)
    lib.suzume_generate_tags_with_options.argtypes = [
        handle,
        ctypes.c_char_p,
        ctypes.POINTER(SuzumeTagOptions),
    ]

    lib.suzume_tags_free.restype = None
    lib.suzume_tags_free.argtypes = [ctypes.POINTER(SuzumeTags)]

    lib.suzume_load_user_dict.restype = ctypes.c_int
    lib.suzume_load_user_dict.argtypes = [handle, ctypes.c_char_p, ctypes.c_size_t]

    lib.suzume_load_binary_dict.restype = ctypes.c_int
    lib.suzume_load_binary_dict.argtypes = [
        handle,
        ctypes.POINTER(ctypes.c_uint8),
        ctypes.c_size_t,
    ]

    lib.suzume_version.restype = ctypes.c_char_p
    lib.suzume_version.argtypes = []

    lib.suzume_last_error.restype = ctypes.c_char_p
    lib.suzume_last_error.argtypes = []

    lib.suzume_dictionary_warning_count.restype = ctypes.c_size_t
    lib.suzume_dictionary_warning_count.argtypes = [handle]

    lib.suzume_dictionary_warning.restype = ctypes.c_char_p
    lib.suzume_dictionary_warning.argtypes = [handle, ctypes.c_size_t]

    # ABI layout oracles (used by the layout-guard test).
    for name in (
        "suzume_sizeof_result",
        "suzume_sizeof_morpheme",
        "suzume_sizeof_tags",
        "suzume_sizeof_tag_options",
        "suzume_sizeof_extended_options",
    ):
        fn = getattr(lib, name)
        fn.restype = ctypes.c_size_t
        fn.argtypes = []
    for name in (
        "suzume_offsetof_result",
        "suzume_offsetof_morpheme",
        "suzume_offsetof_tags",
        "suzume_offsetof_tag_options",
        "suzume_offsetof_extended_options",
    ):
        fn = getattr(lib, name)
        fn.restype = ctypes.c_size_t
        fn.argtypes = [ctypes.c_uint32]


def _bundled_data_dir(pkg_dir: Path) -> Path | None:
    """Return the package dir if it ships compiled dictionaries, else None."""
    if (pkg_dir / "core.dic").exists() or (pkg_dir / "user.dic").exists():
        return pkg_dir
    return None


def load_library(lib_path: str | None = None) -> ctypes.CDLL:
    """Load the suzume shared library with configured signatures.

    When the wheel bundles ``core.dic``/``user.dic`` next to this module and the
    caller has not set ``SUZUME_DATA_DIR``, point the auto-loader at them so the
    native ``suzume_create`` finds the dictionaries without any external files.
    """
    path = lib_path or _find_library()

    pkg_dir = Path(__file__).parent
    data_dir = _bundled_data_dir(pkg_dir)
    if data_dir is not None:
        os.environ.setdefault("SUZUME_DATA_DIR", str(data_dir))

    lib = ctypes.CDLL(path)
    _configure_signatures(lib)
    return lib
