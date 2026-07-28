import process from 'node:process';

import { Suzume } from '../bindings/wasm/dist/index.js';

let payload = '';
for await (const chunk of process.stdin) {
  payload += chunk;
}
const texts = JSON.parse(payload);
if (!Array.isArray(texts) || !texts.every((text) => typeof text === 'string')) {
  throw new TypeError('expected a JSON array of strings');
}

const analyzer = await Suzume.create();
try {
  const results = texts.map((text) => {
    const result = analyzer.analyzeWithNormalizedText(text);
    return {
      normalized_text: result.normalizedText,
      morphemes: result.morphemes.map((morpheme) => ({
        surface: morpheme.surface,
        pos: morpheme.pos,
        lemma: morpheme.baseForm,
        start: morpheme.start,
        end: morpheme.end,
      })),
    };
  });
  process.stdout.write(`${JSON.stringify(results)}\n`);
} finally {
  analyzer.destroy();
}
