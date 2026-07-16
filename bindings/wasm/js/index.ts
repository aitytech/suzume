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

// Types for Emscripten module
interface EmscriptenModule {
  UTF8ToString: (ptr: number) => string;
  stringToUTF8: (str: string, ptr: number, maxBytes: number) => void;
  lengthBytesUTF8: (str: string) => number;
  _malloc: (size: number) => number;
  _free: (ptr: number) => void;
  HEAPU32: Uint32Array;
  _suzume_create: () => number;
  _suzume_create_with_extended_options: (optionsPtr: number) => number;
  _suzume_destroy: (handle: number) => void;
  _suzume_analyze: (handle: number, textPtr: number) => number;
  _suzume_result_free: (resultPtr: number) => void;
  _suzume_generate_tags: (handle: number, textPtr: number) => number;
  _suzume_generate_tags_with_options: (
    handle: number,
    textPtr: number,
    optionsPtr: number,
  ) => number;
  _suzume_tags_free: (tagsPtr: number) => void;
  _suzume_load_user_dict: (handle: number, dataPtr: number, size: number) => number;
  _suzume_load_binary_dict: (handle: number, dataPtr: number, size: number) => number;
  _suzume_version: () => number;
  _suzume_last_error: () => number;
  _suzume_dictionary_warning_count: (handle: number) => number;
  _suzume_dictionary_warning: (handle: number, index: number) => number;
  _suzume_sizeof_result: () => number;
  _suzume_sizeof_morpheme: () => number;
  _suzume_sizeof_tags: () => number;
  _suzume_sizeof_tag_options: () => number;
  _suzume_sizeof_extended_options: () => number;
  _suzume_offsetof_result: (field: number) => number;
  _suzume_offsetof_morpheme: (field: number) => number;
  _suzume_offsetof_tags: (field: number) => number;
  _suzume_offsetof_tag_options: (field: number) => number;
  _suzume_offsetof_extended_options: (field: number) => number;
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
  /** Apply lemmatization, default: true */
  lemmatize?: boolean;
  /** Merge consecutive noun compounds, default: false */
  mergeCompounds?: boolean;
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
  /** Extended POS subcategory (English, e.g., "VerbRenyokei", "AuxTenseTa") */
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
export interface TagOptions {
  /** POS categories to include (default: all content words) */
  pos?: ('noun' | 'verb' | 'adjective' | 'adverb')[];
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

// Release handle ref for destructor so the destroy fn is not bound to the Suzume instance
interface CleanupRef {
  module: EmscriptenModule;
  handle: number;
}

interface CLayouts {
  result: {
    size: number;
    morphemes: number;
    count: number;
  };
  morpheme: {
    size: number;
    surface: number;
    pos: number;
    baseForm: number;
    posJa: number;
    conjType: number;
    conjForm: number;
    extendedPos: number;
    start: number;
    end: number;
    isUserDict: number;
    isFormalNoun: number;
    isLowInfo: number;
    isUnknown: number;
    isFromDictionary: number;
    score: number;
  };
  tags: {
    size: number;
    tags: number;
    pos: number;
    count: number;
  };
  tagOptions: {
    size: number;
    posFilter: number;
    excludeBasic: number;
    useLemma: number;
    minLength: number;
    maxTags: number;
    excludeParticles: number;
    excludeAuxiliaries: number;
    excludeFormalNouns: number;
    excludeLowInfo: number;
    removeDuplicates: number;
    structSize: number;
  };
  extendedOptions: {
    size: number;
    structSize: number;
    preserveVu: number;
    preserveCase: number;
    preserveSymbols: number;
    mode: number;
    lemmatize: number;
    mergeCompounds: number;
  };
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
  private _analyze: (handle: number, textPtr: number) => number;
  private _resultFree: (resultPtr: number) => void;
  private _generateTags: (handle: number, textPtr: number) => number;
  private _generateTagsWithOptions: (handle: number, textPtr: number, optionsPtr: number) => number;
  private _tagsFree: (tagsPtr: number) => void;
  private _loadUserDict: (handle: number, dataPtr: number, size: number) => number;
  private _loadBinaryDict: (handle: number, dataPtr: number, size: number) => number;
  private _version: () => number;
  private _lastError: () => number;
  private _dictionaryWarningCount: (handle: number) => number;
  private _dictionaryWarning: (handle: number, index: number) => number;
  private layouts: CLayouts;
  private unregisterToken = {};

  private constructor(module: EmscriptenModule, handle: number, layouts?: CLayouts) {
    this.module = module;
    this.handle = handle;
    this.cleanupRef = { module, handle };
    registry.register(this, this.cleanupRef, this.unregisterToken);

    this._analyze = module._suzume_analyze;
    this._resultFree = module._suzume_result_free;
    this._generateTags = module._suzume_generate_tags;
    this._generateTagsWithOptions = module._suzume_generate_tags_with_options;
    this._tagsFree = module._suzume_tags_free;
    this._loadUserDict = module._suzume_load_user_dict;
    this._loadBinaryDict = module._suzume_load_binary_dict;
    this._version = module._suzume_version;
    this._lastError = module._suzume_last_error;
    this._dictionaryWarningCount = module._suzume_dictionary_warning_count;
    this._dictionaryWarning = module._suzume_dictionary_warning;
    this.layouts = layouts ?? Suzume.loadCLayouts(module);
  }

  private static loadCLayouts(module: EmscriptenModule): CLayouts {
    const sizeofResult = module._suzume_sizeof_result;
    const sizeofMorpheme = module._suzume_sizeof_morpheme;
    const sizeofTags = module._suzume_sizeof_tags;
    const sizeofTagOptions = module._suzume_sizeof_tag_options;
    const sizeofExtendedOptions = module._suzume_sizeof_extended_options;
    const offsetofResult = module._suzume_offsetof_result;
    const offsetofMorpheme = module._suzume_offsetof_morpheme;
    const offsetofTags = module._suzume_offsetof_tags;
    const offsetofTagOptions = module._suzume_offsetof_tag_options;
    const offsetofExtendedOptions = module._suzume_offsetof_extended_options;

    return {
      result: {
        size: sizeofResult(),
        morphemes: offsetofResult(0),
        count: offsetofResult(1),
      },
      morpheme: {
        size: sizeofMorpheme(),
        surface: offsetofMorpheme(0),
        pos: offsetofMorpheme(1),
        baseForm: offsetofMorpheme(2),
        posJa: offsetofMorpheme(3),
        conjType: offsetofMorpheme(4),
        conjForm: offsetofMorpheme(5),
        extendedPos: offsetofMorpheme(6),
        start: offsetofMorpheme(7),
        end: offsetofMorpheme(8),
        isUserDict: offsetofMorpheme(9),
        isFormalNoun: offsetofMorpheme(10),
        isLowInfo: offsetofMorpheme(11),
        isUnknown: offsetofMorpheme(12),
        isFromDictionary: offsetofMorpheme(13),
        score: offsetofMorpheme(14),
      },
      tags: {
        size: sizeofTags(),
        tags: offsetofTags(0),
        pos: offsetofTags(1),
        count: offsetofTags(2),
      },
      tagOptions: {
        size: sizeofTagOptions(),
        posFilter: offsetofTagOptions(0),
        excludeBasic: offsetofTagOptions(1),
        useLemma: offsetofTagOptions(2),
        minLength: offsetofTagOptions(3),
        maxTags: offsetofTagOptions(4),
        excludeParticles: offsetofTagOptions(5),
        excludeAuxiliaries: offsetofTagOptions(6),
        excludeFormalNouns: offsetofTagOptions(7),
        excludeLowInfo: offsetofTagOptions(8),
        removeDuplicates: offsetofTagOptions(9),
        structSize: offsetofTagOptions(10),
      },
      extendedOptions: {
        size: sizeofExtendedOptions(),
        structSize: offsetofExtendedOptions(0),
        preserveVu: offsetofExtendedOptions(1),
        preserveCase: offsetofExtendedOptions(2),
        preserveSymbols: offsetofExtendedOptions(3),
        mode: offsetofExtendedOptions(4),
        lemmatize: offsetofExtendedOptions(5),
        mergeCompounds: offsetofExtendedOptions(6),
      },
    };
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
    const layouts = Suzume.loadCLayouts(module);

    let handle: number;

    if (
      options &&
      (options.preserveVu !== undefined ||
        options.preserveCase !== undefined ||
        options.preserveSymbols !== undefined ||
        options.mode !== undefined ||
        options.lemmatize !== undefined ||
        options.mergeCompounds !== undefined)
    ) {
      // Create with options
      const layout = layouts.extendedOptions;
      const OPTIONS_SIZE = layout.size;
      const optionsPtr = module._malloc(OPTIONS_SIZE);

      try {
        const heap = module.HEAPU32;
        // Zero the whole struct first so any field the C ABI may append later
        // defaults to zero instead of reading back uninitialized malloc bytes.
        new Uint8Array(heap.buffer).fill(0, optionsPtr, optionsPtr + OPTIONS_SIZE);
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
        heap[(optionsPtr + layout.structSize) >> 2] = OPTIONS_SIZE;
        // preserve_vu: default true
        heap[(optionsPtr + layout.preserveVu) >> 2] = options.preserveVu !== false ? 1 : 0;
        // preserve_case: default true
        heap[(optionsPtr + layout.preserveCase) >> 2] = options.preserveCase !== false ? 1 : 0;
        // preserve_symbols: default false
        heap[(optionsPtr + layout.preserveSymbols) >> 2] = options.preserveSymbols === true ? 1 : 0;
        heap[(optionsPtr + layout.mode) >> 2] = modeValue;
        heap[(optionsPtr + layout.lemmatize) >> 2] = options.lemmatize !== false ? 1 : 0;
        heap[(optionsPtr + layout.mergeCompounds) >> 2] = options.mergeCompounds === true ? 1 : 0;

        handle = module._suzume_create_with_extended_options(optionsPtr);
      } finally {
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

    return new Suzume(module, handle, layouts);
  }

  /**
   * Analyze Japanese text into morphemes
   *
   * @param text - UTF-8 encoded Japanese text
   * @returns Array of morphemes
   */
  analyze(text: string): Morpheme[] {
    this.ensureAlive();

    return this.withUtf8String(text, (textPtr) => {
      const resultPtr = this._analyze(this.handle, textPtr);

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

    return this.withUtf8String(text, (textPtr) => {
      if (options) {
        // Build pos_filter bitmask
        let posFilter = 0;
        if (options.pos) {
          const posMap: Record<string, number> = { noun: 1, verb: 2, adjective: 4, adverb: 8 };
          for (const pos of options.pos) {
            posFilter |= posMap[pos] ?? 0;
          }
        }

        const optionsPtr = this.module._malloc(this.layouts.tagOptions.size);

        try {
          const heapU32 = this.module.HEAPU32;
          const heapU8 = new Uint8Array(heapU32.buffer);
          const layout = this.layouts.tagOptions;

          heapU8[optionsPtr + layout.posFilter] = posFilter & 0xff;
          heapU32[(optionsPtr + layout.excludeBasic) >> 2] = options.excludeBasic ? 1 : 0;
          heapU32[(optionsPtr + layout.useLemma) >> 2] = options.useLemma !== false ? 1 : 0;
          heapU32[(optionsPtr + layout.minLength) >> 2] = options.minLength ?? 2;
          heapU32[(optionsPtr + layout.maxTags) >> 2] = options.maxTags ?? 0;
          heapU32[(optionsPtr + layout.excludeParticles) >> 2] =
            options.excludeParticles !== false ? 1 : 0;
          heapU32[(optionsPtr + layout.excludeAuxiliaries) >> 2] =
            options.excludeAuxiliaries !== false ? 1 : 0;
          heapU32[(optionsPtr + layout.excludeFormalNouns) >> 2] =
            options.excludeFormalNouns !== false ? 1 : 0;
          heapU32[(optionsPtr + layout.excludeLowInfo) >> 2] =
            options.excludeLowInfo !== false ? 1 : 0;
          heapU32[(optionsPtr + layout.removeDuplicates) >> 2] =
            options.removeDuplicates !== false ? 1 : 0;
          // Forward-compat marker: the malloc'd buffer is uninitialized, so set
          // the trailing size field to the full struct size the way the native
          // header documents (mirrors the extended-options path above).
          heapU32[(optionsPtr + layout.structSize) >> 2] = layout.size;

          return this.consumeTags(this._generateTagsWithOptions(this.handle, textPtr, optionsPtr));
        } finally {
          this.module._free(optionsPtr);
        }
      }

      return this.consumeTags(this._generateTags(this.handle, textPtr));
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

  // Parse suzume_result_t structure from WASM memory
  private parseResult(resultPtr: number): Morpheme[] {
    const HEAPU32 = this.module.HEAPU32;
    const HEAPF32 = new Float32Array(HEAPU32.buffer);
    const resultLayout = this.layouts.result;
    const morphemeLayout = this.layouts.morpheme;

    const morphemesPtr = HEAPU32[(resultPtr + resultLayout.morphemes) >> 2];
    const count = HEAPU32[(resultPtr + resultLayout.count) >> 2];

    const morphemes: Morpheme[] = [];

    for (let idx = 0; idx < count; idx++) {
      const morphPtr = morphemesPtr + idx * morphemeLayout.size;
      const surfacePtr = HEAPU32[(morphPtr + morphemeLayout.surface) >> 2];
      const posPtr = HEAPU32[(morphPtr + morphemeLayout.pos) >> 2];
      const baseFormPtr = HEAPU32[(morphPtr + morphemeLayout.baseForm) >> 2];
      const posJaPtr = HEAPU32[(morphPtr + morphemeLayout.posJa) >> 2];
      const conjTypePtr = HEAPU32[(morphPtr + morphemeLayout.conjType) >> 2];
      const conjFormPtr = HEAPU32[(morphPtr + morphemeLayout.conjForm) >> 2];
      const extendedPosPtr = HEAPU32[(morphPtr + morphemeLayout.extendedPos) >> 2];
      const start = HEAPU32[(morphPtr + morphemeLayout.start) >> 2];
      const end = HEAPU32[(morphPtr + morphemeLayout.end) >> 2];

      morphemes.push({
        surface: this.module.UTF8ToString(surfacePtr),
        pos: this.module.UTF8ToString(posPtr),
        baseForm: this.module.UTF8ToString(baseFormPtr),
        posJa: this.module.UTF8ToString(posJaPtr),
        conjType: conjTypePtr !== 0 ? this.module.UTF8ToString(conjTypePtr) : null,
        conjForm: conjFormPtr !== 0 ? this.module.UTF8ToString(conjFormPtr) : null,
        extendedPos: this.module.UTF8ToString(extendedPosPtr),
        start,
        end,
        isUserDict: HEAPU32[(morphPtr + morphemeLayout.isUserDict) >> 2] !== 0,
        isFormalNoun: HEAPU32[(morphPtr + morphemeLayout.isFormalNoun) >> 2] !== 0,
        isLowInfo: HEAPU32[(morphPtr + morphemeLayout.isLowInfo) >> 2] !== 0,
        isUnknown: HEAPU32[(morphPtr + morphemeLayout.isUnknown) >> 2] !== 0,
        isFromDictionary: HEAPU32[(morphPtr + morphemeLayout.isFromDictionary) >> 2] !== 0,
        score: HEAPF32[(morphPtr + morphemeLayout.score) >> 2],
      });
    }

    return morphemes;
  }

  // Parse suzume_tags_t structure from WASM memory
  private parseTags(tagsPtr: number): Tag[] {
    const HEAPU32 = this.module.HEAPU32;
    const layout = this.layouts.tags;

    const tagsArrayPtr = HEAPU32[(tagsPtr + layout.tags) >> 2];
    const posArrayPtr = HEAPU32[(tagsPtr + layout.pos) >> 2];
    const count = HEAPU32[(tagsPtr + layout.count) >> 2];

    const tags: Tag[] = [];

    for (let idx = 0; idx < count; idx++) {
      const tagPtr = HEAPU32[(tagsArrayPtr >> 2) + idx];
      const posPtr = HEAPU32[(posArrayPtr >> 2) + idx];
      tags.push({
        tag: this.module.UTF8ToString(tagPtr),
        pos: this.module.UTF8ToString(posPtr),
      });
    }

    return tags;
  }
}

// Default export
export default Suzume;
