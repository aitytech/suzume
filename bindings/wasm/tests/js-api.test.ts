/**
 * Tests for the JS API layer (js/index.ts).
 *
 * Since Suzume.create() uses dynamic import('./suzume.js') which doesn't resolve
 * in vitest, we test the JS API indirectly by verifying:
 * 1. The TypeScript types/interfaces are correctly defined
 * 2. The C API struct layouts match what parseTags/parseMorphemes expect
 *
 * The C API tests (c-api-analyze.test.ts, c-api-tags.test.ts) cover the actual
 * WASM function calls. This file tests the JS-specific concerns.
 */
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { Suzume } from '../dist/index.js';
import { extendedPosLabel } from '../js/abi_labels.js';
import {
  allocString,
  EXTENDED_OPTIONS_LAYOUT,
  getModule,
  parseMorphemes,
  parseTags,
  TAG_OPTIONS_LAYOUT,
  TAGS_LAYOUT,
  type WasmModule,
} from './helpers';

describe('JS API: struct layout compatibility', () => {
  let module: WasmModule;
  let handle: number;

  beforeAll(async () => {
    module = await getModule();
    const create = module.cwrap('suzume_create', 'number', []) as () => number;
    handle = create();
  });

  afterAll(() => {
    if (handle && module) {
      const destroy = module.cwrap('suzume_destroy', null, ['number']) as (h: number) => void;
      destroy(handle);
    }
  });

  it('labels every serialized ExtendedPOS code without shifting late additions', () => {
    expect(extendedPosLabel(0)).toBe('UNKNOWN');
    expect(extendedPosLabel(32)).toBe('AUX_開始');
    expect(extendedPosLabel(33)).toBe('AUX_様態');
    expect(extendedPosLabel(73)).toBe('AUX_文語完了');
    expect(extendedPosLabel(77)).toBe('SUFFIX_直後');
    expect(extendedPosLabel(80)).toBe('AUX_よう');
    expect(extendedPosLabel(81)).toBe('AUX_KURUWA_POLITE');
    expect(extendedPosLabel(82)).toBe('AUX_文語過去キ');
    // One past the last label: a code the C ABI has but the table does not must
    // degrade to UNKNOWN rather than shift every later label by one.
    expect(extendedPosLabel(83)).toBe('UNKNOWN');
  });

  it('exports the complete C ABI surface required by the JS binding', () => {
    const expectedExports = [
      '_suzume_analyze',
      '_suzume_create',
      '_suzume_create_with_extended_options',
      '_suzume_destroy',
      '_suzume_dictionary_warning',
      '_suzume_dictionary_warning_count',
      '_suzume_generate_tags',
      '_suzume_generate_tags_with_options',
      '_suzume_init_extended_options',
      '_suzume_last_error',
      '_suzume_load_binary_dict',
      '_suzume_load_user_dict',
      '_suzume_result_free',
      '_suzume_tags_free',
      '_suzume_version',
    ];
    const exports = Object.keys(module as object)
      .filter((name) => name.startsWith('_suzume_'))
      .sort();

    expect(exports).toEqual(expectedExports);
  });

  it('parseMorphemes returns the complete Morpheme result field snapshot', () => {
    const analyze = module.cwrap('suzume_analyze', 'number', ['number', 'number']) as (
      h: number,
      t: number,
    ) => number;
    const resultFree = module.cwrap('suzume_result_free', null, ['number']) as (r: number) => void;

    const textPtr = allocString(module, '食べた');
    const resultPtr = analyze(handle, textPtr);
    module._free(textPtr);

    const morphemes = parseMorphemes(module, resultPtr);
    expect(morphemes.length).toBeGreaterThan(0);

    const m = morphemes[0];
    expect(Object.keys(m).sort()).toEqual([
      'baseForm',
      'conjForm',
      'conjType',
      'end',
      'extendedPos',
      'isFormalNoun',
      'isFromDictionary',
      'isLowInfo',
      'isUnknown',
      'isUserDict',
      'pos',
      'posJa',
      'score',
      'start',
      'surface',
    ]);
    expect(typeof m.surface).toBe('string');
    expect(typeof m.pos).toBe('string');
    expect(typeof m.baseForm).toBe('string');
    expect(typeof m.posJa).toBe('string');
    // conjType/conjForm can be string or null
    expect(m.conjType === null || typeof m.conjType === 'string').toBe(true);
    expect(m.conjForm === null || typeof m.conjForm === 'string').toBe(true);
    expect(typeof m.extendedPos).toBe('string');
    expect(typeof m.start).toBe('number');
    expect(typeof m.end).toBe('number');
    expect(typeof m.isUserDict).toBe('boolean');
    expect(typeof m.isFormalNoun).toBe('boolean');
    expect(typeof m.isLowInfo).toBe('boolean');
    expect(typeof m.isUnknown).toBe('boolean');
    expect(typeof m.isFromDictionary).toBe('boolean');
    expect(typeof m.score).toBe('number');

    resultFree(resultPtr);
  });

  it('parseTags returns tag and pos fields matching Tag interface', () => {
    const generateTags = module.cwrap('suzume_generate_tags', 'number', ['number', 'number']) as (
      h: number,
      t: number,
    ) => number;
    const tagsFree = module.cwrap('suzume_tags_free', null, ['number']) as (t: number) => void;

    const textPtr = allocString(module, '東京タワーは美しい');
    const tagsPtr = generateTags(handle, textPtr);
    module._free(textPtr);

    const tags = parseTags(module, tagsPtr);
    expect(tags.length).toBeGreaterThan(0);

    // Verify fields match js/index.ts Tag interface
    for (const t of tags) {
      expect(typeof t.tag).toBe('string');
      expect(typeof t.pos).toBe('string');
      expect(t.tag.length).toBeGreaterThan(0);
      expect(t.pos.length).toBeGreaterThan(0);
    }

    tagsFree(tagsPtr);
  });

  it('suzume_tags_t layout: tags ptr at +0, pos ptr at +1, count at +2', () => {
    const generateTags = module.cwrap('suzume_generate_tags', 'number', ['number', 'number']) as (
      h: number,
      t: number,
    ) => number;
    const tagsFree = module.cwrap('suzume_tags_free', null, ['number']) as (t: number) => void;

    const textPtr = allocString(module, '東京タワー');
    const tagsPtr = generateTags(handle, textPtr);
    module._free(textPtr);

    // Verify struct layout directly
    const tagsArrayPtr = module.HEAPU32[(tagsPtr + TAGS_LAYOUT.tags) >> 2];
    const posArrayPtr = module.HEAPU32[(tagsPtr + TAGS_LAYOUT.pos) >> 2];
    const count = module.HEAPU32[(tagsPtr + TAGS_LAYOUT.count) >> 2];

    expect(tagsArrayPtr).toBeGreaterThan(0);
    expect(posArrayPtr).toBeGreaterThan(0);
    expect(count).toBeGreaterThanOrEqual(0);

    // Tags are string pointers; POS values are compact one-byte enum codes.
    if (count > 0) {
      const firstTagPtr = module.HEAPU32[tagsArrayPtr >> 2];
      const firstPos = new Uint8Array(module.HEAPU32.buffer)[posArrayPtr];
      expect(firstTagPtr).toBeGreaterThan(0);
      expect(firstPos).toBeGreaterThan(0);

      const tagStr = module.UTF8ToString(firstTagPtr);
      expect(tagStr.length).toBeGreaterThan(0);
    }

    tagsFree(tagsPtr);
  });

  it('loadBinaryDictionary uses HEAPU32 buffer derivation (not HEAPU8)', () => {
    // Verify that Uint8Array can be derived from HEAPU32.buffer
    // This is the pattern used in js/index.ts loadBinaryDictionary
    const heapU32 = module.HEAPU32;
    const heapU8 = new Uint8Array(heapU32.buffer);
    expect(heapU8).toBeInstanceOf(Uint8Array);
    expect(heapU8.length).toBeGreaterThan(0);
    // The derived view should share the same underlying buffer
    expect(heapU8.buffer).toBe(heapU32.buffer);
  });

  it('create_with_extended_options accepts split mode', () => {
    const initOptions = module.cwrap('suzume_init_extended_options', null, ['number']) as (
      optionsPtr: number,
    ) => void;
    const createWithOptions = module.cwrap('suzume_create_with_extended_options', 'number', [
      'number',
    ]) as (optionsPtr: number) => number;
    const analyze = module.cwrap('suzume_analyze', 'number', ['number', 'number']) as (
      h: number,
      t: number,
    ) => number;
    const resultFree = module.cwrap('suzume_result_free', null, ['number']) as (r: number) => void;
    const destroy = module.cwrap('suzume_destroy', null, ['number']) as (h: number) => void;

    const optionsPtr = module._malloc(EXTENDED_OPTIONS_LAYOUT.size);
    initOptions(optionsPtr);
    new Uint8Array(module.HEAPU32.buffer)[optionsPtr + EXTENDED_OPTIONS_LAYOUT.mode] = 2;

    const h = createWithOptions(optionsPtr);
    module._free(optionsPtr);

    try {
      expect(h).toBeGreaterThan(0);
      const textPtr = allocString(module, 'API開発');
      const resultPtr = analyze(h, textPtr);
      module._free(textPtr);
      const morphemes = parseMorphemes(module, resultPtr);
      resultFree(resultPtr);
      expect(morphemes.length).toBeGreaterThan(1);
    } finally {
      destroy(h);
    }
  });

  it('last_error reports invalid C API calls', () => {
    const analyze = module.cwrap('suzume_analyze', 'number', ['number', 'number']) as (
      h: number,
      t: number,
    ) => number;
    const lastError = module.cwrap('suzume_last_error', 'number', []) as () => number;

    expect(analyze(0, 0)).toBe(0);
    expect(module.UTF8ToString(lastError())).toContain('null handle');
  });

  it('tag_options struct layout accepts initialized fields', () => {
    const generateTagsWithOptions = module.cwrap('suzume_generate_tags_with_options', 'number', [
      'number',
      'number',
      'number',
    ]) as (h: number, t: number, o: number) => number;
    const tagsFree = module.cwrap('suzume_tags_free', null, ['number']) as (t: number) => void;

    const textPtr = allocString(module, '東京タワー');
    const optionsPtr = module._malloc(TAG_OPTIONS_LAYOUT.size);

    const heapU8 = new Uint8Array(module.HEAPU32.buffer);
    heapU8[optionsPtr + TAG_OPTIONS_LAYOUT.posFilter] = 0;
    heapU8[optionsPtr + TAG_OPTIONS_LAYOUT.excludeBasic] = 0;
    heapU8[optionsPtr + TAG_OPTIONS_LAYOUT.useLemma] = 1;
    module.HEAPU32[(optionsPtr + TAG_OPTIONS_LAYOUT.minLength) >> 2] = 1;
    module.HEAPU32[(optionsPtr + TAG_OPTIONS_LAYOUT.maxTags) >> 2] = 0;
    heapU8[optionsPtr + TAG_OPTIONS_LAYOUT.excludeParticles] = 1;
    heapU8[optionsPtr + TAG_OPTIONS_LAYOUT.excludeAuxiliaries] = 1;
    heapU8[optionsPtr + TAG_OPTIONS_LAYOUT.excludeFormalNouns] = 1;
    heapU8[optionsPtr + TAG_OPTIONS_LAYOUT.excludeLowInfo] = 1;
    heapU8[optionsPtr + TAG_OPTIONS_LAYOUT.removeDuplicates] = 1;

    const tagsPtr = generateTagsWithOptions(handle, textPtr, optionsPtr);
    module._free(textPtr);
    module._free(optionsPtr);

    expect(tagsPtr).toBeGreaterThan(0);
    const tags = parseTags(module, tagsPtr);
    expect(tags.length).toBeGreaterThan(0);

    tagsFree(tagsPtr);
  });
});

describe('JS API: error reporting', () => {
  it('exposes last C API error after a failed dictionary load', async () => {
    const suzume = await Suzume.create();

    try {
      expect(suzume.loadUserDictionary('"東京,NOUN,0.5\n')).toBe(false);
      expect(suzume.lastError).toContain('Invalid CSV quoting');
      expect(suzume.lastError).toContain('unterminated quoted field');
    } finally {
      suzume.destroy();
    }
  });

  it('loadUserDictionaryOrThrow includes C API parse details', async () => {
    const suzume = await Suzume.create();

    try {
      expect(() => suzume.loadUserDictionaryOrThrow('"東京,NOUN,0.5\n')).toThrow(
        /Invalid CSV quoting.*unterminated quoted field/,
      );
    } finally {
      suzume.destroy();
    }
  });

  it('loadBinaryDictionaryOrThrow includes C API parse details', async () => {
    const suzume = await Suzume.create();

    try {
      expect(() => suzume.loadBinaryDictionaryOrThrow(new Uint8Array([0, 1, 2, 3]))).toThrow(
        /Dictionary file too small/,
      );
    } finally {
      suzume.destroy();
    }
  });

  it('create forwards extended JS options', async () => {
    const suzume = await Suzume.create({
      mode: 'split',
      lemmatize: true,
      mergeCompounds: false,
    });

    try {
      const morphemes = suzume.analyze('API開発');
      expect(morphemes.length).toBeGreaterThan(1);
    } finally {
      suzume.destroy();
    }
  });

  it('throws when a destroyed instance is used', async () => {
    const suzume = await Suzume.create();
    suzume.destroy();

    expect(() => suzume.analyze('東京')).toThrow('Suzume instance has been destroyed');
  });
});
