import createModule from '../dist/suzume.js';
import {
  conjugationFormJapanese,
  conjugationTypeJapanese,
  extendedPosLabel,
  MORPHEME_FLAG,
  posEnglish,
  posJapanese,
} from '../js/abi_labels.js';
import { C_LAYOUTS } from '../js/abi_layout.js';

export interface WasmModule {
  cwrap: (
    name: string,
    returnType: string | null,
    argTypes: string[],
  ) => (...args: unknown[]) => unknown;
  UTF8ToString: (ptr: number) => string;
  stringToUTF8: (str: string, ptr: number, maxBytes: number) => void;
  lengthBytesUTF8: (str: string) => number;
  _malloc: (size: number) => number;
  _free: (ptr: number) => void;
  HEAPU32: Uint32Array;
}

export const RESULT_LAYOUT = C_LAYOUTS.result;
export const MORPHEME_LAYOUT = C_LAYOUTS.morpheme;
export const TAGS_LAYOUT = C_LAYOUTS.tags;
export const TAG_OPTIONS_LAYOUT = C_LAYOUTS.tagOptions;
export const EXTENDED_OPTIONS_LAYOUT = C_LAYOUTS.extendedOptions;

// Shared module instance (loaded once across all test files)
let cachedModule: WasmModule | null = null;

export async function getModule(): Promise<WasmModule> {
  if (!cachedModule) {
    const module = (await createModule()) as WasmModule & Record<string, unknown>;
    // C-API tests intentionally exercise functions by their public C names.
    // Production code calls the exported `_suzume_*` functions directly, so
    // keep this compatibility adapter in tests rather than shipping cwrap.
    module.cwrap = (name: string) => {
      const fn = module[`_${name}`];
      if (typeof fn !== 'function') {
        throw new Error(`Missing WASM export: ${name}`);
      }
      return fn as (...args: unknown[]) => unknown;
    };
    cachedModule = module;
  }
  return cachedModule;
}

export function allocString(module: WasmModule, text: string): number {
  const bytes = module.lengthBytesUTF8(text) + 1;
  const ptr = module._malloc(bytes);
  module.stringToUTF8(text, ptr, bytes);
  return ptr;
}

export interface ParsedMorpheme {
  surface: string;
  pos: string;
  baseForm: string;
  posJa: string;
  conjType: string | null;
  conjForm: string | null;
  extendedPos: string;
  start: number;
  end: number;
  isUserDict: boolean;
  isFormalNoun: boolean;
  isLowInfo: boolean;
  isUnknown: boolean;
  isFromDictionary: boolean;
  score: number;
}

export function parseMorphemes(module: WasmModule, resultPtr: number): ParsedMorpheme[] {
  const heapU8 = new Uint8Array(module.HEAPU32.buffer);
  const heapF32 = new Float32Array(module.HEAPU32.buffer);
  const morphemesPtr = module.HEAPU32[(resultPtr + RESULT_LAYOUT.morphemes) >> 2];
  const count = module.HEAPU32[(resultPtr + RESULT_LAYOUT.count) >> 2];
  const morphemes: ParsedMorpheme[] = [];

  for (let i = 0; i < count; i++) {
    const morphPtr = morphemesPtr + i * MORPHEME_LAYOUT.size;
    const surfacePtr = module.HEAPU32[(morphPtr + MORPHEME_LAYOUT.surface) >> 2];
    const baseFormPtr = module.HEAPU32[(morphPtr + MORPHEME_LAYOUT.baseForm) >> 2];
    const posCode = heapU8[morphPtr + MORPHEME_LAYOUT.pos];
    const flags = heapU8[morphPtr + MORPHEME_LAYOUT.flags];
    const conjugates = posCode === 2 || posCode === 3;

    morphemes.push({
      surface: module.UTF8ToString(surfacePtr),
      pos: posEnglish(posCode),
      baseForm: module.UTF8ToString(baseFormPtr),
      posJa: posJapanese(posCode),
      conjType: conjugates
        ? conjugationTypeJapanese(heapU8[morphPtr + MORPHEME_LAYOUT.conjugationType])
        : null,
      conjForm: conjugates
        ? conjugationFormJapanese(heapU8[morphPtr + MORPHEME_LAYOUT.conjugationForm])
        : null,
      extendedPos: extendedPosLabel(heapU8[morphPtr + MORPHEME_LAYOUT.extendedPos]),
      start: module.HEAPU32[(morphPtr + MORPHEME_LAYOUT.start) >> 2],
      end: module.HEAPU32[(morphPtr + MORPHEME_LAYOUT.end) >> 2],
      isUserDict: (flags & MORPHEME_FLAG.userDict) !== 0,
      isFormalNoun: (flags & MORPHEME_FLAG.formalNoun) !== 0,
      isLowInfo: (flags & MORPHEME_FLAG.lowInfo) !== 0,
      isUnknown: (flags & MORPHEME_FLAG.unknown) !== 0,
      isFromDictionary: (flags & MORPHEME_FLAG.fromDictionary) !== 0,
      score: heapF32[(morphPtr + MORPHEME_LAYOUT.score) >> 2],
    });
  }
  return morphemes;
}

// suzume_tags_t layout: { char** tags; uint8_t* pos; size_t count; }
export interface ParsedTag {
  tag: string;
  pos: string;
}

export function parseTags(module: WasmModule, tagsPtr: number): ParsedTag[] {
  const heapU8 = new Uint8Array(module.HEAPU32.buffer);
  const tagsArrayPtr = module.HEAPU32[(tagsPtr + TAGS_LAYOUT.tags) >> 2];
  const posArrayPtr = module.HEAPU32[(tagsPtr + TAGS_LAYOUT.pos) >> 2];
  const count = module.HEAPU32[(tagsPtr + TAGS_LAYOUT.count) >> 2];
  const tags: ParsedTag[] = [];

  for (let i = 0; i < count; i++) {
    const tagStrPtr = module.HEAPU32[(tagsArrayPtr >> 2) + i];
    tags.push({
      tag: module.UTF8ToString(tagStrPtr),
      pos: posEnglish(heapU8[posArrayPtr + i]),
    });
  }
  return tags;
}

export function getTagCount(module: WasmModule, tagsPtr: number): number {
  return module.HEAPU32[(tagsPtr + TAGS_LAYOUT.count) >> 2];
}
