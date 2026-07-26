"""Canonical surface-difference classification for MCP quality tools."""

import unicodedata


def normalize_width(text: str) -> str:
    """Normalize compatibility-width variants for comparison."""
    return unicodedata.normalize("NFKC", text)


def classify_surface_diff(expected: list[str], actual: list[str]) -> str:
    """Classify one token-surface comparison with a shared result vocabulary."""
    if not expected or not actual:
        return "empty"
    if expected == actual:
        return "match"
    if normalize_width("\0".join(expected)) == normalize_width("\0".join(actual)):
        return "minor"
    if len(actual) > len(expected):
        return "over-split"
    if len(actual) < len(expected):
        return "under-split"
    return "boundary"
