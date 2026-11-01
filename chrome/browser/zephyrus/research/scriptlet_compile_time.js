// How much V8 work does the scriptlet runtime cost a document that gets any scriptlet at all?
// Extracts kScriptletLibrary from the real source, then times compile with and without a code cache.
//   node scriptlet_compile_time.js ../adblock/adblock_scriptlet_engine.cc
const vm = require('vm'), fs = require('fs');
const src = fs.readFileSync(process.argv[2], 'utf8');
const m = /constexpr char kScriptletLibrary\[\] = R"JS\(([\s\S]*?)\)JS";/.exec(src);
if (!m) { console.error('kScriptletLibrary not found'); process.exit(2); }
const lib = m[1], wrapped = '(function(){\n' + lib + '\n})();';
const time = (f, n) => { const a = process.hrtime.bigint(); for (let i = 0; i < n; i++) f(i); return Number(process.hrtime.bigint() - a) / 1e6 / n; };
const cold = time(i => new vm.Script(wrapped + '//' + i, { filename: 'sl' + i }), 200);   // distinct source each time: no in-isolate compilation cache
const run = time(i => { const sc = new vm.Script(wrapped + '//r' + i, { filename: 'r' + i }); try { sc.runInContext(vm.createContext({ window: {}, document: {}, console })); } catch (e) {} }, 50);
const cache = new vm.Script(wrapped, { produceCachedData: true }).createCachedData();
const cached = time(i => new vm.Script(wrapped, { cachedData: cache, filename: 'c' + i }), 200);
const used = (lib.match(/'[a-z][a-z0-9.-]*'\s*:\s*[A-Za-z]+,/g) || []).length;
console.log(`runtime library ${lib.length} bytes (~${used} registered names)`);
console.log(`compile, no cache      : ${cold.toFixed(2)} ms per document`);
console.log(`compile + first run    : ${run.toFixed(2)} ms per document`);
console.log(`compile with code cache: ${cached.toFixed(2)} ms per document (cache blob ${cache.length} bytes)`);
