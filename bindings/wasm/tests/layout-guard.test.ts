import { beforeAll, describe, expect, it } from 'vitest';
import { getModule, MORPHEME_LAYOUT, type WasmModule } from './helpers';

/**
 * Guards the hardcoded MORPHEME_LAYOUT in helpers.ts against the authoritative
 * WASM-exported struct geometry. Production js/index.ts reads these offsets
 * dynamically; the test helper hardcodes them for speed, so this test fails
 * loudly if suzume_morpheme_t ever changes shape and the helper drifts.
 */
describe('C API: morpheme struct layout', () => {
  let module: WasmModule;
  let sizeofMorpheme: () => number;
  let offsetofMorpheme: (field: number) => number;

  // Field index order must match suzume_offsetof_morpheme() in src/suzume_c.cpp.
  const FIELD_OFFSETS: Array<[keyof typeof MORPHEME_LAYOUT, number]> = [
    ['surface', 0],
    ['pos', 1],
    ['baseForm', 2],
    ['posJa', 3],
    ['conjType', 4],
    ['conjForm', 5],
    ['extendedPos', 6],
    ['start', 7],
    ['end', 8],
    ['isUserDict', 9],
    ['isFormalNoun', 10],
    ['isLowInfo', 11],
    ['isUnknown', 12],
    ['isFromDictionary', 13],
    ['score', 14],
  ];

  beforeAll(async () => {
    module = await getModule();
    sizeofMorpheme = module.cwrap('suzume_sizeof_morpheme', 'number', []) as () => number;
    offsetofMorpheme = module.cwrap('suzume_offsetof_morpheme', 'number', ['number']) as (
      field: number,
    ) => number;
  });

  it('hardcoded struct size matches the WASM export', () => {
    expect(MORPHEME_LAYOUT.size).toBe(sizeofMorpheme());
  });

  it('every hardcoded field offset matches the WASM export', () => {
    for (const [field, index] of FIELD_OFFSETS) {
      expect(MORPHEME_LAYOUT[field]).toBe(offsetofMorpheme(index));
    }
  });
});
