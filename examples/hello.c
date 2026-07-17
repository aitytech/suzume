/*
 * Minimal C consumer of the Suzume C ABI.
 *
 * Build in-tree with -DSUZUME_BUILD_EXAMPLES=ON, or standalone against an
 * installed package via examples/consumer/CMakeLists.txt.
 */
#include <stdio.h>

#include "suzume/suzume_c.h"

int main(int argc, char** argv) {
  const char* text = argc > 1 ? argv[1] : "東京都に住んでいます";

  suzume_t handle = suzume_create();
  if (handle == NULL) {
    fprintf(stderr, "suzume_create failed: %s\n", suzume_last_error());
    return 1;
  }

  suzume_result_t* result = suzume_analyze(handle, text);
  if (result == NULL) {
    fprintf(stderr, "suzume_analyze failed: %s\n", suzume_last_error());
    suzume_destroy(handle);
    return 1;
  }

  printf("suzume %s: %zu morpheme(s)\n", suzume_version(), result->count);
  for (size_t idx = 0; idx < result->count; ++idx) {
    const suzume_morpheme_t* morph = &result->morphemes[idx];
    printf("  %s\t%s\t%s\n", morph->surface, morph->pos, morph->base_form);
  }

  suzume_result_free(result);
  suzume_destroy(handle);
  return result != NULL ? 0 : 1;
}
