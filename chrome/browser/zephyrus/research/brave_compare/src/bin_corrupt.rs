// What Brave's engine (adblock-rust) does when its stored blob is damaged or
// rewritten, so it can be set against Zephyrus's compiled index under the same
// treatment (AdblockZIndexTest.HostileBlobCampaign, driven by research/hostile_driver.py).
//
//   corrupt <preprocessed list> <corpus.tsv> <damage|hostile> <first case> <end case> <seed>
//
// Each case mutates one copy of the serialised engine, reloads it and runs 300
// recorded requests through it:
//   damage  anything in the file, the checksum NOT repaired (a bit-rotted or
//           half-written file);
//   hostile the payload only, with the checksum REPAIRED so it passes (somebody who
//           can write the file). Brave's documented threat model excludes this; it
//           is measured because the compiled index defends against it.
// One line per case: "CASE n" before it runs (so a crash names its case), then
// "OUT n <rejected|same|changed|panic>". A crash or hang is seen by the driver.
use adblock::lists::{FilterSet, ParseOptions};
use adblock::request::Request;
use adblock::Engine;
use std::io::Write;
use std::panic;

// "magic (4) + format (1) + seahash (8) + crate version (16)"; see data_format/mod.rs.
const HASH_OFFSET: usize = 5;
const HEADER: usize = 29;

struct Rng(u64);
impl Rng {
    fn new(seed: u64, case: u64) -> Rng {
        let mut r = Rng(seed.wrapping_mul(0x9E37_79B9_7F4A_7C15) ^ case.wrapping_mul(0xD6E8_FEB8_6659_FD93) ^ 0x1234_5678);
        for _ in 0..4 {
            r.next();
        }
        r
    }
    fn next(&mut self) -> u64 {
        self.0 ^= self.0 >> 12;
        self.0 ^= self.0 << 25;
        self.0 ^= self.0 >> 27;
        self.0.wrapping_mul(0x2545_F491_4F6C_DD1D)
    }
    fn below(&mut self, n: usize) -> usize {
        if n == 0 { 0 } else { (self.next() % n as u64) as usize }
    }
}

// The same recipe the C++ campaign uses, over bytes [lo, hi) of the file.
fn mutate(file: &mut [u8], lo: usize, hi: usize, rng: &mut Rng) {
    let span = hi - lo;
    match rng.below(4) {
        0 => {
            let at = lo + rng.below(span);
            file[at] ^= 1 << rng.below(8);
        }
        1 | 2 => {
            // 1-3 aligned 32-bit overwrites; kind 2 stays in the first 1% (the
            // directories and root tables live near the start).
            let window = if rng.below(2) == 0 { span } else { (span / 100).max(64).min(span) };
            for _ in 0..1 + rng.below(3) {
                let at = (lo + rng.below(window)) & !3;
                if at + 4 > hi || at < lo {
                    continue;
                }
                let value: u32 = match rng.below(8) {
                    0 => 0,
                    1 => 1,
                    2 => u32::MAX,
                    3 => 0x7FFF_FFFF,
                    4 => 0x8000_0000,
                    5 => span as u32,
                    6 => (span as u32).wrapping_sub(1),
                    _ => rng.next() as u32,
                };
                file[at..at + 4].copy_from_slice(&value.to_le_bytes());
            }
        }
        _ => {
            let at = lo + rng.below(span);
            let len = 16 + rng.below(4081);
            let end = (at + len).min(hi);
            for b in &mut file[at..end] {
                *b = 0;
            }
        }
    }
}

fn main() {
    let a: Vec<String> = std::env::args().collect();
    if a.len() < 7 {
        eprintln!("usage: corrupt <list> <corpus.tsv> <damage|hostile> <first> <end> <seed>");
        std::process::exit(2);
    }
    let list = std::fs::read_to_string(&a[1]).unwrap();
    let corpus = std::fs::read_to_string(&a[2]).unwrap();
    let hostile = a[3] == "hostile";
    let first: u64 = a[4].parse().unwrap();
    let end: u64 = a[5].parse().unwrap();
    let seed: u64 = a[6].parse().unwrap();

    let rows: Vec<(String, String, String)> = corpus
        .lines()
        .filter_map(|l| {
            let c: Vec<&str> = l.split('\t').collect();
            if c.len() < 3 {
                return None;
            }
            let t = if c[2] == "xhr" { "xmlhttprequest" } else { c[2] };
            Some((c[0].to_string(), c[1].to_string(), t.to_string()))
        })
        .take(300)
        .collect();
    let requests: Vec<Request> = rows
        .iter()
        .filter_map(|(u, p, t)| Request::new(u, p, t, "GET").ok())
        .collect();

    let mut fs = FilterSet::new(false);
    fs.add_filter_list(list, ParseOptions::default());
    let engine = Engine::new_with_filter_set(fs);
    let baseline: Vec<bool> = requests
        .iter()
        .map(|r| engine.check_network_request(r).should_block())
        .collect();
    let file = engine.serialize();
    println!("BASE size {} probes {}", file.len(), requests.len());
    {
        // Load time of the intact file, which is what the checks cost every start.
        let t0 = std::time::Instant::now();
        for _ in 0..20 {
            let mut e = Engine::default();
            e.deserialize(&file).unwrap();
        }
        println!("LOAD_MS {:.2}", t0.elapsed().as_secs_f64() * 1000.0 / 20.0);
    }

    // A panic is a result here, not a stop; keep its message out of the stream.
    panic::set_hook(Box::new(|_| {}));
    let stdout = std::io::stdout();
    for case in first..end {
        let mut out = stdout.lock();
        writeln!(out, "CASE {}", case).unwrap();
        out.flush().unwrap();
        let mut rng = Rng::new(seed, case);
        let mut mutated = file.clone();
        if hostile {
            mutate(&mut mutated, HEADER, file.len(), &mut rng);
            let hash = seahash::hash(&mutated[HEADER..]).to_le_bytes();
            mutated[HASH_OFFSET..HASH_OFFSET + 8].copy_from_slice(&hash);
        } else {
            mutate(&mut mutated, 0, file.len(), &mut rng);
        }
        if mutated == file {
            // The mutation wrote the bytes that were already there; nothing was damaged.
            writeln!(out, "OUT {} noop", case).unwrap();
            out.flush().unwrap();
            continue;
        }
        let result = panic::catch_unwind(panic::AssertUnwindSafe(|| {
            let mut e = Engine::default();
            if e.deserialize(&mutated).is_err() {
                return "rejected";
            }
            let mut changed = false;
            for (r, base) in requests.iter().zip(&baseline) {
                if e.check_network_request(r).should_block() != *base {
                    changed = true;
                }
            }
            if changed { "changed" } else { "same" }
        }));
        let result = result.unwrap_or("panic");
        if !hostile && (result == "same" || result == "changed") {
            // Damage that was NOT refused: where in the file it was.
            let at = mutated.iter().zip(&file).position(|(a, b)| a != b).unwrap_or(0);
            writeln!(out, "DIFF {} offset {}", case, at).unwrap();
        }
        writeln!(out, "OUT {} {}", case, result).unwrap();
        out.flush().unwrap();
    }
    println!("DONE");
}
