"""MCP test tools, grouped by inspection, mutation, and review responsibility."""

from ._test_tools_mutation import (
    test_add,
    test_batch_add,
    test_delete,
    test_list_pos,
    test_map_pos,
    test_replace_pos,
    test_update,
)
from ._test_tools_read import (
    test_compare,
    test_diff_mecab,
    test_diff_suzume,
    test_failed,
    test_list,
    test_needs_suzume_update,
    test_search,
    test_show,
)
from ._test_tools_review import (
    test_accept_diff,
    test_check_coverage,
    test_reset_suzume,
    test_suggest_file,
    test_validate_ids,
)

__all__ = [
    "test_accept_diff",
    "test_add",
    "test_batch_add",
    "test_check_coverage",
    "test_compare",
    "test_delete",
    "test_diff_mecab",
    "test_diff_suzume",
    "test_failed",
    "test_list",
    "test_list_pos",
    "test_map_pos",
    "test_needs_suzume_update",
    "test_replace_pos",
    "test_reset_suzume",
    "test_search",
    "test_show",
    "test_suggest_file",
    "test_update",
    "test_validate_ids",
]
