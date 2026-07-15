"""MCP dictionary tools, grouped by operation responsibility."""

from ._dict_tools_bulk import (
    dict_bulk_add,
    dict_bulk_move,
)
from ._dict_tools_maintenance import (
    dict_cleanup,
    dict_grep,
    dict_remove_matching,
    dict_sort,
)
from ._dict_tools_operations import (
    dict_add,
    dict_check,
    dict_disable,
    dict_enable,
    dict_remove,
    dict_suggest,
    dict_validate,
)

__all__ = [
    "dict_add",
    "dict_bulk_add",
    "dict_bulk_move",
    "dict_check",
    "dict_cleanup",
    "dict_disable",
    "dict_enable",
    "dict_grep",
    "dict_remove",
    "dict_remove_matching",
    "dict_sort",
    "dict_suggest",
    "dict_validate",
]
