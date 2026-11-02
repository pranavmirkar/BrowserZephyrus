// Brave's engine (adblock-rust) on the SAME filter list and the SAME recorded
// requests as AdblockZIndexTest.HeadToHeadCorpus, on the same machine, so the two
// can be compared like for like.
//
//   cargo run --release --bin h2h -- <preprocessed list> <corpus.tsv> <out dir>
//
// Prints latency (matching only, and including building the Request object, which
// the Zephyrus numbers always include), memory, the serialised size, and writes
// brave.txt (one 0/1 decision per request) for the agreement analysis.
use adblock::lists::{FilterSet, ParseOptions};
use adblock::request::Request;
use adblock::Engine;
use std::time::Instant;

fn stats(name: &str, mut micros: Vec<f64>) {
    micros.sort_by(|a, b| a.partial_cmp(b).unwrap());
    let n = micros.len();
    let mean = micros.iter().sum::<f64>() / n as f64;
    println!(
        "  {:<26} mean {:6.2} us  p50 {:6.2}  p99 {:7.2}  p99.9 {:8.2}",
        name,
        mean,
        micros[n / 2],
        micros[n * 99 / 100],
        micros[n * 999 / 1000]
    );
}

fn working_set_mb() -> f64 {
    memory_stats::memory_stats()
        .map(|m| m.physical_mem as f64 / 1e6)
        .unwrap_or(0.0)
}

fn main() {
    let a: Vec<String> = std::env::args().collect();
    let list = std::fs::read_to_string(&a[1]).unwrap();
    let corpus = std::fs::read_to_string(&a[2]).unwrap();
    let out = &a[3];
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
        .collect();

    let m0 = working_set_mb();
    let t0 = Instant::now();
    let mut fs = FilterSet::new(false);
    fs.add_filter_list(list.clone(), ParseOptions::default());
    let engine = Engine::new_with_filter_set(fs);
    let build = t0.elapsed();
    let m1 = working_set_mb();
    println!(
        "\n=== Brave adblock-rust 0.13 on the same list and requests: {} requests ===",
        rows.len()
    );
    println!(
        "  parse + build {:.0} ms, working set +{:.1} MB",
        build.as_secs_f64() * 1000.0,
        m1 - m0
    );

    let reqs: Vec<Option<Request>> = rows
        .iter()
        .map(|(u, p, t)| Request::new(u, p, t, "GET").ok())
        .collect();
    let unparseable = reqs.iter().filter(|r| r.is_none()).count();

    // warm up, then three passes; matching only (Request already built)
    for r in reqs.iter().flatten() {
        let _ = engine.check_network_request(r);
    }
    let mut micros = Vec::new();
    let mut decisions = String::new();
    let mut blocked = 0usize;
    for pass in 0..3 {
        for r in &reqs {
            match r {
                Some(r) => {
                    let t = Instant::now();
                    let b = engine.check_network_request(r).should_block();
                    micros.push(t.elapsed().as_secs_f64() * 1e6);
                    if pass == 0 {
                        decisions.push_str(if b { "1\n" } else { "0\n" });
                        blocked += b as usize;
                    }
                }
                None => {
                    if pass == 0 {
                        decisions.push_str("0\n");
                    }
                }
            }
        }
    }
    stats("Brave (match only)", micros);
    println!(
        "  blocked {} of {} ({:.1}%), {} the engine could not parse",
        blocked,
        rows.len(),
        100.0 * blocked as f64 / rows.len() as f64,
        unparseable
    );
    // the same work the Zephyrus numbers do: build the Request, then match
    let mut micros = Vec::new();
    for _ in 0..3 {
        for (u, p, t) in &rows {
            let s = Instant::now();
            if let Ok(r) = Request::new(u, p, t, "GET") {
                let _ = engine.check_network_request(&r);
            }
            micros.push(s.elapsed().as_secs_f64() * 1e6);
        }
    }
    stats("Brave (Request + match)", micros);

    let t1 = Instant::now();
    let ser = engine.serialize();
    println!(
        "  serialised engine {:.2} MB in {:.0} ms",
        ser.len() as f64 / 1e6,
        t1.elapsed().as_secs_f64() * 1000.0
    );
    std::fs::write(format!("{}/brave.txt", out), decisions).unwrap();
}
