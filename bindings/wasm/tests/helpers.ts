import createModule from '../dist/suzume.js';
import { conjugationTypeJapanese, posEnglish } from '../js/abi_labels.js';
import { C_LAYOUTS } from '../js/abi_layout.js';
import { decodeAnalysisResult, decodeTags } from '../js/decode.js';
import type { Morpheme as ParsedMorpheme, Tag as ParsedTag } from '../js/index.js';

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

export function parseMorphemes(module: WasmModule, resultPtr: number): ParsedMorpheme[] {
  return decodeAnalysisResult(module, resultPtr, conjugationTypeJapanese, posEnglish).morphemes;
}

export function parseTags(module: WasmModule, tagsPtr: number): ParsedTag[] {
  return decodeTags(module, tagsPtr, posEnglish);
}

export function getTagCount(module: WasmModule, tagsPtr: number): number {
  return module.HEAPU32[(tagsPtr + TAGS_LAYOUT.count) >> 2];
}
