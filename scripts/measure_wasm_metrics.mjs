#!/usr/bin/env node

// Reproducible WASM performance and memory measurement.
//
// Run after `make wasm` from the repository root:
//   node scripts/measure_wasm_metrics.mjs --iterations=1000 --samples=5 --warmup=1
//   node scripts/measure_wasm_metrics.mjs --corpus=/path/to/corpus.txt
//
// The script intentionally measures the public JS API, including result
// decoding. It reports allocator instrumentation as unavailable rather than
// pretending that Node RSS or WASM linear memory is an allocation count.

import { readFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const repositoryRoot = resolve(import.meta.dirname, '..');
const wasmPath = resolve(repositoryRoot, 'bindings/wasm/dist/suzume.wasm');
const indexPath = resolve(repositoryRoot, 'bindings/wasm/dist/index.js');
const { Suzume } = await import(pathToFileURL(indexPath).href);

const defaults = {
  iterations: 1000,
  samples: 5,
  warmup: 1,
};

function parsePositiveInteger(value, option) {
  const parsed = Number.parseInt(value, 10);
  if (!Number.isSafeInteger(parsed) || parsed <= 0 || String(parsed) !== value) {
    throw new Error(`${option} must be a positive integer: ${value}`);
  }
  return parsed;
}

function parseNonNegativeInteger(value, option) {
  const parsed = Number.parseInt(value, 10);
  if (!Number.isSafeInteger(parsed) || parsed < 0 || String(parsed) !== value) {
    throw new Error(`${option} must be a non-negative integer: ${value}`);
  }
  return parsed;
}

function median(samples) {
  const ordered = [...samples].sort((left, right) => left - right);
  const middle = Math.floor(ordered.length / 2);
  return ordered.length % 2 === 1 ? ordered[middle] : (ordered[middle - 1] + ordered[middle]) / 2;
}

function wasmPages(instance) {
  return instance.wasmMemoryBytes() / 65536;
}

const options = { ...defaults, corpus: undefined };
for (const argument of process.argv.slice(2)) {
  if (argument.startsWith('--iterations=')) {
    options.iterations = parsePositiveInteger(argument.slice('--iterations='.length), '--iterations');
  } else if (argument.startsWith('--samples=')) {
    options.samples = parsePositiveInteger(argument.slice('--samples='.length), '--samples');
  } else if (argument.startsWith('--warmup=')) {
    options.warmup = parseNonNegativeInteger(argument.slice('--warmup='.length), '--warmup');
  } else if (argument.startsWith('--corpus=')) {
    options.corpus = resolve(argument.slice('--corpus='.length));
  } else {
    throw new Error(`Unknown option: ${argument}`);
  }
}

const texts = options.corpus
  ? (await readFile(options.corpus, 'utf8')).split(/\r?\n/u).filter((line) => line.length > 0)
  : ['東京でテストを行う。', 'りんごを食べる。', '１２３ＡＢＣを確認する。'];

if (texts.length === 0) {
  throw new Error('Corpus must contain at least one non-empty line');
}

const corpusBytes = texts.reduce((total, text) => total + Buffer.byteLength(text), 0);
const instantiateMs = [];
const firstAnalysisMs = [];
const steadyMs = [];
const initialPages = [];
const steadyPages = [];
let peakRssBytes = process.memoryUsage().rss;
let morphemeCount = 0;

for (let sample = 0; sample < options.samples; sample += 1) {
  const instantiateStart = performance.now();
  const instance = await Suzume.create({ wasmPath });
  instantiateMs.push(performance.now() - instantiateStart);
  initialPages.push(wasmPages(instance));

  const firstStart = performance.now();
  morphemeCount += instance.analyze(texts[0]).length;
  firstAnalysisMs.push(performance.now() - firstStart);

  for (let warmup = 0; warmup < options.warmup; warmup += 1) {
    for (const text of texts) {
      morphemeCount += instance.analyze(text).length;
    }
  }

  const steadyStart = performance.now();
  for (let iteration = 0; iteration < options.iterations; iteration += 1) {
    for (const text of texts) {
      morphemeCount += instance.analyze(text).length;
    }
  }
  steadyMs.push(performance.now() - steadyStart);
  steadyPages.push(wasmPages(instance));
  peakRssBytes = Math.max(peakRssBytes, process.memoryUsage().rss);
  instance.destroy();
}

const steadyMedian = median(steadyMs);
const steadyBytesPerSecond = (corpusBytes * options.iterations * 1000) / steadyMedian;
console.log(
  `WASM benchmark: ${options.iterations} steady iterations, ${options.samples} samples, ${options.warmup} warmup iterations, ${texts.length} texts, ${corpusBytes} bytes per corpus pass`,
);
console.log(`Instantiate median: ${median(instantiateMs).toFixed(3)} ms`);
console.log(`First analysis median: ${median(firstAnalysisMs).toFixed(3)} ms`);
console.log(`Steady median: ${steadyMedian.toFixed(3)} ms`);
console.log(`Steady throughput: ${Math.floor(steadyBytesPerSecond)} bytes/sec`);
console.log(`Steady per text: ${(steadyMedian / (options.iterations * texts.length)).toFixed(6)} ms`);
console.log(`WASM initial pages (median): ${median(initialPages)}`);
console.log(`WASM steady pages (median): ${median(steadyPages)}`);
console.log(`Node peak RSS: ${peakRssBytes} bytes`);
console.log('Allocation count: unavailable (requires an allocator-instrumented build)');

if (morphemeCount === 0) {
  throw new Error('Benchmark inputs produced no morphemes');
}
