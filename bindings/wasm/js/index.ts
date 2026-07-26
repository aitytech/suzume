/**
 * Suzume - Lightweight Japanese morphological analyzer
 *
 * @example
 * ```typescript
 * import { Suzume } from 'suzume';
 *
 * const suzume = await Suzume.create();
 * const result = suzume.analyze('すもももももももものうち');
 * console.log(result);
 * ```
 */

import { C_LAYOUTS } from './abi_layout.js';
import { decodeAnalysisResult, decodeTags } from './decode.js';

// Types for Emscripten module
interface EmscriptenModule {
  UTF8ToString: (ptr: number) => string;
  stringToUTF8: (str: string, ptr: number, maxBytes: number) => void;
  lengthBytesUTF8: (str: string) => number;
  _malloc: (size: number) => number;
  _free: (ptr: number) => void;
  HEAPU32: Uint32Array;
  _suzume_create: () => number;
  _suzume_init_extended_options: (optionsPtr: number) => void;
  _suzume_create_with_extended_options: (optionsPtr: number) => number;
  _suzume_destroy: (handle: number) => void;
  _suzume_analyze: (handle: number, textPtr: number) => number;
  _suzume_analyze_n: (handle: number, textPtr: number, size: number) => number;
  _suzume_result_free: (resultPtr: number) => void;
  _suzume_generate_tags: (handle: number, textPtr: number) => number;
  _suzume_generate_tags_n: (handle: number, textPtr: number, size: number) => number;
  _suzume_init_tag_options: (optionsPtr: number) => void;
  _suzume_generate_tags_with_options: (
    handle: number,
    textPtr: number,
    optionsPtr: number,
  ) => number;
  _suzume_generate_tags_with_options_n: (
    handle: number,
    textPtr: number,
    size: number,
    optionsPtr: number,
  ) => number;
  _suzume_tags_free: (tagsPtr: number) => void;
  _suzume_load_user_dict: (handle: number, dataPtr: number, size: number) => number;
  _suzume_load_binary_dict: (handle: number, dataPtr: number, size: number) => number;
  _suzume_clear_user_dictionaries: (handle: number) => number;
  _suzume_version: () => number;
  _suzume_last_error: () => number;
  _suzume_last_error_code: () => number;
  _suzume_conjugation_type_label: (code: number) => number;
  _suzume_pos_label: (code: number) => number;
  _suzume_dictionary_warning_count: (handle: number) => number;
  _suzume_dictionary_warning: (handle: number, index: number) => number;
}

/**
 * Options for creating a Suzume instance
 */
export interface SuzumeOptions {
  /** Preserve ヴ (don't normalize to ビ etc.), default: true */
  preserveVu?: boolean;
  /** Preserve case (don't lowercase ASCII), default: true */
  preserveCase?: boolean;
  /** Preserve symbols/emoji in output, default: false */
  preserveSymbols?: boolean;
  /** Analysis mode, default: normal */
  mode?: 'normal' | 'search' | 'split';
  /** Retain corrected lemmas; conjugation/POS annotations are always computed. Default: true */
  lemmatize?: boolean;
  /** Merge consecutive noun compounds, default: false */
  mergeCompounds?: boolean;
  /** Skip automatic loading of the bundled user dictionary, default: false */
  skipUserDictionary?: boolean;
  /** Skip automatic loading of the bundled core dictionary, default: false */
  skipCoreDictionary?: boolean;
  /** Report scorer configuration diagnostics, default: false */
  reportScorerConfig?: boolean;
  /** Scorer override JSON, or an object serialized to JSON */
  scorerOptions?: string | Record<string, unknown>;
}

/**
 * Morpheme - A single unit of morphological analysis
 */
export interface Morpheme {
  /** Surface form (as it appears in the text) */
  surface: string;
  /** Part of speech (English) */
  pos: string;
  /** Base/dictionary form */
  baseForm: string;
  /** Part of speech (Japanese) */
  posJa: string;
  /** Conjugation type (Japanese, e.g., "一段", "五段・カ行") - null for non-conjugating words */
  conjType: string | null;
  /** Conjugation form (Japanese, e.g., "連用形", "終止形") - null for non-conjugating words */
  conjForm: string | null;
  /** Stable extended POS code (e.g., "VERB_連用", "AUX_過去") */
  extendedPos: string;
  /** Start character offset in normalized text */
  start: number;
  /** End character offset in normalized text */
  end: number;
  /** True if matched from a user dictionary */
  isUserDict: boolean;
  /** True if the morpheme is a formal noun */
  isFormalNoun: boolean;
  /** True if the morpheme is low information for tag generation */
  isLowInfo: boolean;
  /** True if generated as an unknown word */
  isUnknown: boolean;
  /** True if matched from any dictionary */
  isFromDictionary: boolean;
  /** Candidate score/cost */
  score: number;
}

/** Normalized input together with its morphemes. */
export interface AnalysisResult {
  normalizedText: string;
  morphemes: Morpheme[];
}

/**
 * Tag entry with POS information
 */
export interface Tag {
  /** Tag text (surface or lemma) */
  tag: string;
  /** Part of speech (English) */
  pos: string;
}

/**
 * Options for tag generation
 */
export type TagPosFilterName = 'noun' | 'verb' | 'adjective' | 'adverb';

export interface TagOptions {
  /**
   * POS categories to include. An empty array includes all content words,
   * matching the native `pos_filter = 0` default.
   */
  posFilter?: readonly TagPosFilterName[];
  /**
   * Deprecated alias for `posFilter`. When both are present, `posFilter` wins.
   *
   * @deprecated Use `posFilter` instead.
   */
  pos?: readonly TagPosFilterName[];
  /** Exclude basic/common words with hiragana-only lemma (default: false) */
  excludeBasic?: boolean;
  /** Use lemma instead of surface form (default: true) */
  useLemma?: boolean;
  /** Minimum tag length in characters (default: 2) */
  minLength?: number;
  /** Maximum number of tags, 0 for unlimited (default: 0) */
  maxTags?: number;
  /** Exclude particles (default: true) */
  excludeParticles?: boolean;
  /** Exclude auxiliaries (default: true) */
  excludeAuxiliaries?: boolean;
  /** Exclude formal nouns (default: true) */
  excludeFormalNouns?: boolean;
  /** Exclude low information words (default: true) */
  excludeLowInfo?: boolean;
  /** Remove duplicate tags (default: true) */
  removeDuplicates?: boolean;
}

const TAG_POS_FILTER_BITS: Readonly<Record<string, number>> = {
  noun: 1,
  verb: 2,
  adjective: 4,
  adverb: 8,
};

function resolveTagPosFilter(options: TagOptions): number {
  const selectedPos = options.posFilter !== undefined ? options.posFilter : options.pos;
  let filter = 0;

  for (const pos of selectedPos ?? []) {
    const bit = TAG_POS_FILTER_BITS[pos];
    if (bit === undefined) {
      throw new Error(
        `unknown POS filter name: ${JSON.stringify(pos)} ` +
          `(expected one of ${Object.keys(TAG_POS_FILTER_BITS).sort().join(', ')})`,
      );
    }
    filter |= bit;
  }

  return filter;
}

// Release handle ref for destructor so the destroy fn is not bound to the Suzume instance
interface CleanupRef {
  module: EmscriptenModule;
  handle: number;
}

const registry = new FinalizationRegistry((ref: CleanupRef) => {
  if (ref.handle !== 0) {
    ref.module._suzume_destroy(ref.handle);
    ref.handle = 0;
  }
});

/**
 * Suzume instance for Japanese morphological analysis.
 *
 * Error contract note: under the WebAssembly build, a memory-allocation failure
 * aborts the module rather than returning NULL, so the C++ allocation-failure
 * path (which maps to a NULL return and a thrown Error on native/Python) is
 * effectively unreachable here.
 */
export class Suzume {
  private module: EmscriptenModule;
  private handle: number;
  private cleanupRef: CleanupRef;
  private _analyzeN: (handle: number, textPtr: number, size: number) => number;
  private _resultFree: (resultPtr: number) => void;
  private _generateTagsN: (handle: number, textPtr: number, size: number) => number;
  private _generateTagsWithOptionsN: (
    handle: number,
    textPtr: number,
    size: number,
    optionsPtr: number,
  ) => number;
  private _tagsFree: (tagsPtr: number) => void;
  private _loadUserDict: (handle: number, dataPtr: number, size: number) => number;
  private _loadBinaryDict: (handle: number, dataPtr: number, size: number) => number;
  private _clearUserDictionaries: (handle: number) => number;
  private _version: () => number;
  private _lastError: () => number;
  private _lastErrorCode: () => number;
  private _conjugationTypeLabel: (code: number) => number;
  private _posLabel: (code: number) => number;
  private _dictionaryWarningCount: (handle: number) => number;
  private _dictionaryWarning: (handle: number, index: number) => number;
  private layouts = C_LAYOUTS;
  private unregisterToken = {};

  private constructor(module: EmscriptenModule, handle: number) {
    this.module = module;
    this.handle = handle;
    this.cleanupRef = { module, handle };
    registry.register(this, this.cleanupRef, this.unregisterToken);

    this._analyzeN = module._suzume_analyze_n;
    this._resultFree = module._suzume_result_free;
    this._generateTagsN = module._suzume_generate_tags_n;
    this._generateTagsWithOptionsN = module._suzume_generate_tags_with_options_n;
    this._tagsFree = module._suzume_tags_free;
    this._loadUserDict = module._suzume_load_user_dict;
    this._loadBinaryDict = module._suzume_load_binary_dict;
    this._clearUserDictionaries = module._suzume_clear_user_dictionaries;
    this._version = module._suzume_version;
    this._lastError = module._suzume_last_error;
    this._lastErrorCode = module._suzume_last_error_code;
    this._conjugationTypeLabel = module._suzume_conjugation_type_label;
    this._posLabel = module._suzume_pos_label;
    this._dictionaryWarningCount = module._suzume_dictionary_warning_count;
    this._dictionaryWarning = module._suzume_dictionary_warning;
  }

  /**
   * Create a new Suzume instance
   *
   * @param options - Optional configuration options
   * @returns Promise resolving to Suzume instance
   */
  static async create(options?: SuzumeOptions & { wasmPath?: string }): Promise<Suzume> {
    const wasmPath = options?.wasmPath;

    // Dynamic import of the Emscripten-generated module
    const createModule = await import('./suzume.js');
    const moduleOptions: Record<string, unknown> = {};
    if (wasmPath) {
      moduleOptions.locateFile = (path: string) => (path.endsWith('.wasm') ? wasmPath : path);
    }
    const module: EmscriptenModule = await createModule.default(moduleOptions);

    let handle: number;

    if (
      options &&
      (options.preserveVu !== undefined ||
        options.preserveCase !== undefined ||
        options.preserveSymbols !== undefined ||
        options.mode !== undefined ||
        options.lemmatize !== undefined ||
        options.mergeCompounds !== undefined ||
        options.skipUserDictionary !== undefined ||
        options.skipCoreDictionary !== undefined ||
        options.reportScorerConfig !== undefined ||
        options.scorerOptions !== undefined)
    ) {
      // Create with options
      const layout = C_LAYOUTS.extendedOptions;
      const OPTIONS_SIZE = layout.size;
      const optionsPtr = module._malloc(OPTIONS_SIZE);
      let scorerOptionsPtr = 0;

      try {
        // _malloc hands back uninitialized heap, so seed the struct with the C
        // defaults before overriding fields. Every field below is written today,
        // but a field added to the C struct would otherwise be read as garbage.
        module._suzume_init_extended_options(optionsPtr);

        const heap = new Uint8Array(module.HEAPU32.buffer);
        const modeMap: Record<NonNullable<SuzumeOptions['mode']>, number> = {
          normal: 0,
          search: 1,
          split: 2,
        };
        const selectedMode = options.mode ?? 'normal';
        const modeValue = modeMap[selectedMode];
        if (modeValue === undefined) {
          throw new Error(`Invalid Suzume mode: ${String(options.mode)}`);
        }
        // preserve_vu: default true
        heap[optionsPtr + layout.preserveVu] = options.preserveVu !== false ? 1 : 0;
        // preserve_case: default true
        heap[optionsPtr + layout.preserveCase] = options.preserveCase !== false ? 1 : 0;
        // preserve_symbols: default false
        heap[optionsPtr + layout.preserveSymbols] = options.preserveSymbols === true ? 1 : 0;
        heap[optionsPtr + layout.mode] = modeValue;
        heap[optionsPtr + layout.lemmatize] = options.lemmatize !== false ? 1 : 0;
        heap[optionsPtr + layout.mergeCompounds] = options.mergeCompounds === true ? 1 : 0;
        heap[optionsPtr + layout.skipUserDictionary] = options.skipUserDictionary === true ? 1 : 0;
        heap[optionsPtr + layout.skipCoreDictionary] = options.skipCoreDictionary === true ? 1 : 0;
        heap[optionsPtr + layout.reportScorerConfig] = options.reportScorerConfig === true ? 1 : 0;
        if (options.scorerOptions !== undefined) {
          const scorerJson =
            typeof options.scorerOptions === 'string'
              ? options.scorerOptions
              : JSON.stringify(options.scorerOptions);
          const scorerBytes = module.lengthBytesUTF8(scorerJson) + 1;
          scorerOptionsPtr = module._malloc(scorerBytes);
          module.stringToUTF8(scorerJson, scorerOptionsPtr, scorerBytes);
          module.HEAPU32[(optionsPtr + layout.scorerOptionsJson) >> 2] = scorerOptionsPtr;
        }

        handle = module._suzume_create_with_extended_options(optionsPtr);
      } finally {
        if (scorerOptionsPtr !== 0) {
          module._free(scorerOptionsPtr);
        }
        module._free(optionsPtr);
      }
    } else {
      // Create with default options
      handle = module._suzume_create();
    }

    if (handle === 0) {
      const message = module.UTF8ToString(module._suzume_last_error());
      throw new Error(
        message
          ? `Failed to create Suzume instance: ${message}`
          : 'Failed to create Suzume instance',
      );
    }

    return new Suzume(module, handle);
  }

  /**
   * Analyze Japanese text into morphemes
   *
   * @param text - UTF-8 encoded Japanese text
   * @returns Array of morphemes
   */
  analyze(text: string): Morpheme[] {
    return this.analyzeWithNormalizedText(text).morphemes;
  }

  /**
   * Analyze text and return the exact normalized text used for offsets.
   */
  analyzeWithNormalizedText(text: string): AnalysisResult {
    this.ensureAlive();

    return this.withUtf8String(text, (textPtr, textBytes) => {
      const resultPtr = this._analyzeN(this.handle, textPtr, textBytes - 1);

      if (resultPtr === 0) {
        throw new Error(`Suzume analyze failed: ${this.lastError || 'unknown error'}`);
      }

      try {
        return this.parseResult(resultPtr);
      } finally {
        this._resultFree(resultPtr);
      }
    });
  }

  /**
   * Generate tags from Japanese text
   *
   * @param text - UTF-8 encoded Japanese text
   * @param options - Optional tag generation options
   * @returns Array of tag entries with POS information
   */
  generateTags(text: string, options?: TagOptions): Tag[] {
    this.ensureAlive();

    return this.withUtf8String(text, (textPtr, textBytes) => {
      if (options) {
        const posFilter = resolveTagPosFilter(options);

        const optionsPtr = this.module._malloc(this.layouts.tagOptions.size);

        try {
          // Same reason as create(): seed the C defaults into freshly malloc'd
          // memory, including the struct padding the field writes never touch.
          this.module._suzume_init_tag_options(optionsPtr);

          const heapU32 = this.module.HEAPU32;
          const heapU8 = new Uint8Array(heapU32.buffer);
          const layout = this.layouts.tagOptions;

          heapU8[optionsPtr + layout.posFilter] = posFilter & 0xff;
          heapU8[optionsPtr + layout.excludeBasic] = options.excludeBasic ? 1 : 0;
          heapU8[optionsPtr + layout.useLemma] = options.useLemma !== false ? 1 : 0;
          heapU32[(optionsPtr + layout.minLength) >> 2] = options.minLength ?? 2;
          heapU32[(optionsPtr + layout.maxTags) >> 2] = options.maxTags ?? 0;
          heapU8[optionsPtr + layout.excludeParticles] = options.excludeParticles !== false ? 1 : 0;
          heapU8[optionsPtr + layout.excludeAuxiliaries] =
            options.excludeAuxiliaries !== false ? 1 : 0;
          heapU8[optionsPtr + layout.excludeFormalNouns] =
            options.excludeFormalNouns !== false ? 1 : 0;
          heapU8[optionsPtr + layout.excludeLowInfo] = options.excludeLowInfo !== false ? 1 : 0;
          heapU8[optionsPtr + layout.removeDuplicates] = options.removeDuplicates !== false ? 1 : 0;
          return this.consumeTags(
            this._generateTagsWithOptionsN(this.handle, textPtr, textBytes - 1, optionsPtr),
          );
        } finally {
          this.module._free(optionsPtr);
        }
      }

      return this.consumeTags(this._generateTagsN(this.handle, textPtr, textBytes - 1));
    });
  }

  /**
   * Load user dictionary from string data
   *
   * @param data - Dictionary data in CSV format
   * @returns true on success
   */
  loadUserDictionary(data: string): boolean {
    this.ensureAlive();

    return this.withUtf8String(
      data,
      (dataPtr, dataBytes) => this._loadUserDict(this.handle, dataPtr, dataBytes - 1) === 1,
    );
  }

  /**
   * Load user dictionary from string data, throwing with C API details on failure.
   *
   * @param data - Dictionary data in CSV format
   */
  loadUserDictionaryOrThrow(data: string): void {
    if (!this.loadUserDictionary(data)) {
      throw new Error(`Suzume user dictionary load failed: ${this.lastError || 'unknown error'}`);
    }
  }

  /**
   * Load binary dictionary from buffer data (as user dictionary)
   *
   * @param data - Binary dictionary data (.dic format)
   * @returns true on success
   */
  loadBinaryDictionary(data: Uint8Array): boolean {
    this.ensureAlive();

    const dataPtr = this.module._malloc(data.byteLength);
    try {
      // Derive Uint8Array view from HEAPU32's underlying buffer (HEAPU8 may not be exported)
      const heapU32 = this.module.HEAPU32;
      const heapU8 = new Uint8Array(heapU32.buffer);
      heapU8.set(data, dataPtr);
      return this._loadBinaryDict(this.handle, dataPtr, data.byteLength) === 1;
    } finally {
      this.module._free(dataPtr);
    }
  }

  /**
   * Load binary dictionary from buffer data, throwing with C API details on failure.
   *
   * @param data - Binary dictionary data (.dic format)
   */
  loadBinaryDictionaryOrThrow(data: Uint8Array): void {
    if (!this.loadBinaryDictionary(data)) {
      throw new Error(`Suzume binary dictionary load failed: ${this.lastError || 'unknown error'}`);
    }
  }

  /**
   * Remove all user dictionaries loaded by this instance.
   */
  clearUserDictionaries(): void {
    this.ensureAlive();
    if (this._clearUserDictionaries(this.handle) !== 1) {
      throw new Error(`Suzume dictionary clear failed: ${this.lastError || 'unknown error'}`);
    }
  }

  /**
   * Get Suzume version string
   */
  get version(): string {
    this.ensureAlive();

    const versionPtr = this._version();
    return this.module.UTF8ToString(versionPtr);
  }

  /**
   * Last C API error for this thread, or empty string if the last C API call succeeded.
   */
  get lastError(): string {
    return this.module.UTF8ToString(this._lastError());
  }

  /** Stable native error category for the last failed C ABI call. */
  get lastErrorCode(): number {
    return this._lastErrorCode();
  }

  /** Current WebAssembly linear-memory size in bytes. */
  wasmMemoryBytes(): number {
    this.ensureAlive();
    return this.module.HEAPU32.buffer.byteLength;
  }

  /**
   * Dictionary warnings produced while auto-loading dictionaries at construction.
   */
  get dictionaryWarnings(): string[] {
    this.ensureAlive();
    const count = this._dictionaryWarningCount(this.handle);
    const warnings: string[] = [];
    for (let idx = 0; idx < count; idx++) {
      const warningPtr = this._dictionaryWarning(this.handle, idx);
      if (warningPtr !== 0) {
        warnings.push(this.module.UTF8ToString(warningPtr));
      }
    }
    return warnings;
  }

  /**
   * Destroy the Suzume instance and free resources.
   * Called automatically via FinalizationRegistry when garbage collected,
   * but can be called explicitly for immediate cleanup.
   */
  destroy(): void {
    if (this.handle !== 0) {
      registry.unregister(this.unregisterToken);
      this.module._suzume_destroy(this.handle);
      this.handle = 0;
      this.cleanupRef.handle = 0;
    }
  }

  private ensureAlive(): void {
    if (this.handle === 0) {
      throw new Error('Suzume instance has been destroyed');
    }
  }

  private withUtf8String<T>(
    value: string,
    operation: (pointer: number, byteLength: number) => T,
  ): T {
    const byteLength = this.module.lengthBytesUTF8(value) + 1;
    const pointer = this.module._malloc(byteLength);
    try {
      this.module.stringToUTF8(value, pointer, byteLength);
      return operation(pointer, byteLength);
    } finally {
      this.module._free(pointer);
    }
  }

  private consumeTags(tagsPtr: number): Tag[] {
    if (tagsPtr === 0) {
      throw new Error(`Suzume tag generation failed: ${this.lastError || 'unknown error'}`);
    }
    try {
      return this.parseTags(tagsPtr);
    } finally {
      this._tagsFree(tagsPtr);
    }
  }

  private conjugationTypeLabel(code: number): string | null {
    const labelPtr = this._conjugationTypeLabel(code);
    return labelPtr === 0 ? null : this.module.UTF8ToString(labelPtr);
  }

  // Parse suzume_result_t structure from WASM memory
  private parseResult(resultPtr: number): AnalysisResult {
    return decodeAnalysisResult(
      this.module,
      resultPtr,
      (code) => this.conjugationTypeLabel(code),
      (code) => this.posLabel(code),
    );
  }

  // Parse suzume_tags_t structure from WASM memory
  private parseTags(tagsPtr: number): Tag[] {
    return decodeTags(this.module, tagsPtr, (code) => this.posLabel(code));
  }

  private posLabel(code: number): string {
    const labelPtr = this._posLabel(code);
    return labelPtr === 0 ? 'OTHER' : this.module.UTF8ToString(labelPtr);
  }
}

// Default export
export default Suzume;
