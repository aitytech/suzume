import {
  conjugationFormJapanese,
  extendedPosLabel,
  MORPHEME_FLAG,
  posJapanese,
} from './abi_labels.js';
import { C_LAYOUTS } from './abi_layout.js';
import type { AnalysisResult, Morpheme, Tag } from './index.js';

export interface DecodeMemory {
  UTF8ToString: (ptr: number) => string;
  HEAPU32: Uint32Array;
}

export type ConjugationTypeLabel = (code: number) => string | null;
export type PosLabel = (code: number) => string;

export function decodeAnalysisResult(
  module: DecodeMemory,
  resultPtr: number,
  conjugationTypeLabel: ConjugationTypeLabel,
  posLabel: PosLabel,
): AnalysisResult {
  const heapU32 = module.HEAPU32;
  const heapU8 = new Uint8Array(heapU32.buffer);
  const heapF32 = new Float32Array(heapU32.buffer);
  const resultLayout = C_LAYOUTS.result;
  const morphemeLayout = C_LAYOUTS.morpheme;
  const morphemesPtr = heapU32[(resultPtr + resultLayout.morphemes) >> 2];
  const count = heapU32[(resultPtr + resultLayout.count) >> 2];
  const normalizedTextPtr = heapU32[(resultPtr + resultLayout.normalizedText) >> 2];
  const normalizedTextSize = heapU32[(resultPtr + resultLayout.normalizedTextSize) >> 2];
  const morphemes: Morpheme[] = [];

  for (let idx = 0; idx < count; idx++) {
    const morphPtr = morphemesPtr + idx * morphemeLayout.size;
    const surfacePtr = heapU32[(morphPtr + morphemeLayout.surface) >> 2];
    const baseFormPtr = heapU32[(morphPtr + morphemeLayout.baseForm) >> 2];
    const posCode = heapU8[morphPtr + morphemeLayout.pos];
    const flags = heapU8[morphPtr + morphemeLayout.flags];
    const conjugates = (flags & MORPHEME_FLAG.conjugatable) !== 0;
    morphemes.push({
      surface: module.UTF8ToString(surfacePtr),
      pos: posLabel(posCode),
      baseForm: module.UTF8ToString(baseFormPtr),
      posJa: posJapanese(posCode),
      conjType: conjugates
        ? conjugationTypeLabel(heapU8[morphPtr + morphemeLayout.conjugationType])
        : null,
      conjForm: conjugates
        ? conjugationFormJapanese(heapU8[morphPtr + morphemeLayout.conjugationForm])
        : null,
      extendedPos: extendedPosLabel(heapU8[morphPtr + morphemeLayout.extendedPos]),
      start: heapU32[(morphPtr + morphemeLayout.start) >> 2],
      end: heapU32[(morphPtr + morphemeLayout.end) >> 2],
      isUserDict: (flags & MORPHEME_FLAG.userDict) !== 0,
      isFormalNoun: (flags & MORPHEME_FLAG.formalNoun) !== 0,
      isLowInfo: (flags & MORPHEME_FLAG.lowInfo) !== 0,
      isUnknown: (flags & MORPHEME_FLAG.unknown) !== 0,
      isFromDictionary: (flags & MORPHEME_FLAG.fromDictionary) !== 0,
      score: heapF32[(morphPtr + morphemeLayout.score) >> 2],
    });
  }

  return {
    normalizedText: new TextDecoder().decode(
      new Uint8Array(heapU32.buffer, normalizedTextPtr, normalizedTextSize),
    ),
    morphemes,
  };
}

export function decodeTags(module: DecodeMemory, tagsPtr: number, posLabel: PosLabel): Tag[] {
  const heapU32 = module.HEAPU32;
  const heapU8 = new Uint8Array(heapU32.buffer);
  const layout = C_LAYOUTS.tags;
  const tagsArrayPtr = heapU32[(tagsPtr + layout.tags) >> 2];
  const posArrayPtr = heapU32[(tagsPtr + layout.pos) >> 2];
  const count = heapU32[(tagsPtr + layout.count) >> 2];
  const tags: Tag[] = [];

  for (let idx = 0; idx < count; idx++) {
    const tagPtr = heapU32[(tagsArrayPtr >> 2) + idx];
    tags.push({
      tag: module.UTF8ToString(tagPtr),
      pos: posLabel(heapU8[posArrayPtr + idx]),
    });
  }
  return tags;
}
