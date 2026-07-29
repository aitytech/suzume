import process from 'node:process';

import { Suzume } from '../bindings/wasm/dist/index.js';

let payload = '';
for await (const chunk of process.stdin) {
  payload += chunk;
}
const cases = JSON.parse(payload);
if (!cases || !Array.isArray(cases.analysis) || !Array.isArray(cases.tags)) {
  throw new TypeError('expected { analysis: [...], tags: [...] }');
}

function extendedOptions(options) {
  return {
    ...(options.mode === undefined ? {} : { mode: options.mode }),
    ...(options.preserve_vu === undefined ? {} : { preserveVu: options.preserve_vu }),
    ...(options.preserve_case === undefined ? {} : { preserveCase: options.preserve_case }),
    ...(options.preserve_symbols === undefined ? {} : { preserveSymbols: options.preserve_symbols }),
    ...(options.lemmatize === undefined ? {} : { lemmatize: options.lemmatize }),
    ...(options.merge_compounds === undefined ? {} : { mergeCompounds: options.merge_compounds }),
  };
}

function tagOptions(options) {
  return {
    ...(options.pos_filter === undefined ? {} : { posFilter: options.pos_filter }),
    ...(options.exclude_basic === undefined ? {} : { excludeBasic: options.exclude_basic }),
    ...(options.use_lemma === undefined ? {} : { useLemma: options.use_lemma }),
    ...(options.min_length === undefined ? {} : { minLength: options.min_length }),
    ...(options.max_tags === undefined ? {} : { maxTags: options.max_tags }),
    ...(options.exclude_particles === undefined ? {} : { excludeParticles: options.exclude_particles }),
    ...(options.exclude_auxiliaries === undefined ? {} : { excludeAuxiliaries: options.exclude_auxiliaries }),
    ...(options.exclude_formal_nouns === undefined ? {} : { excludeFormalNouns: options.exclude_formal_nouns }),
    ...(options.exclude_low_info === undefined ? {} : { excludeLowInfo: options.exclude_low_info }),
    ...(options.remove_duplicates === undefined ? {} : { removeDuplicates: options.remove_duplicates }),
  };
}

function morphemeRecord(morpheme) {
  return {
    surface: morpheme.surface,
    pos: morpheme.pos,
    lemma: morpheme.baseForm,
    conj_type: morpheme.conjType,
    conj_form: morpheme.conjForm,
    extended_pos: morpheme.extendedPos,
    start: morpheme.start,
    end: morpheme.end,
    flags: {
      user_dict: morpheme.isUserDict,
      formal_noun: morpheme.isFormalNoun,
      low_info: morpheme.isLowInfo,
      unknown: morpheme.isUnknown,
      from_dictionary: morpheme.isFromDictionary,
      conjugatable: morpheme.conjForm !== null,
    },
    score: morpheme.score,
  };
}

const analysis = [];
for (const testCase of cases.analysis) {
  const analyzer = await Suzume.create(extendedOptions(testCase.options));
  try {
    const result = analyzer.analyzeWithNormalizedText(testCase.text);
    analysis.push({
      normalized_text: result.normalizedText,
      morphemes: result.morphemes.map(morphemeRecord),
    });
  } finally {
    analyzer.destroy();
  }
}

const tags = [];
for (const testCase of cases.tags) {
  const analyzer = await Suzume.create();
  try {
    tags.push(analyzer.generateTags(testCase.text, tagOptions(testCase.options)));
  } finally {
    analyzer.destroy();
  }
}

process.stdout.write(`${JSON.stringify({ analysis, tags })}\n`);
