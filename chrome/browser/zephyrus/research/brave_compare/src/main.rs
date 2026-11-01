use adblock::lists::{FilterSet, ParseOptions};
use adblock::request::Request;
use adblock::Engine;
use std::time::Instant;
fn rss_kb() -> usize { std::fs::read_to_string("/proc/self/statm").unwrap().split_whitespace().nth(1).unwrap().parse::<usize>().unwrap() * 4 }
fn main() {
    let text = std::fs::read_to_string("all_lists.txt").unwrap();
    let corpus: Vec<(String, String, String, bool)> = std::fs::read_to_string("corpus.tsv").unwrap().lines().map(|l| { let p: Vec<&str> = l.split('\t').collect(); (p[0].into(), p[1].into(), p[2].into(), p[3] == "1") }).collect();
    let r0 = rss_kb();
    let t0 = Instant::now();
    let mut fs = FilterSet::new(false);
    fs.add_filter_list(text.clone(), ParseOptions::default());
    let engine = Engine::new_with_filter_set(fs);
    let build = t0.elapsed();
    let r1 = rss_kb();
    println!("adblock-rust 0.13 (Brave's engine, crates.io): parse+build {:.0} ms, RSS +{:.1} MB", build.as_secs_f64() * 1000.0, (r1 - r0) as f64 / 1024.0);
    let reqs: Vec<Request> = corpus.iter().filter_map(|(u, i, t, _)| Request::new(u, i, t, "GET").ok()).collect();
    println!("requests built: {} of {}", reqs.len(), corpus.len());
    for r in reqs.iter().take(2000) { let _ = engine.check_network_request(r); }
    let mut lat: Vec<f64> = Vec::new(); let mut blocked = 0usize; let mut decisions: Vec<bool> = Vec::new();
    for rep in 0..3 { for r in &reqs { let a = Instant::now(); let m = engine.check_network_request(r).should_block(); let us = a.elapsed().as_secs_f64() * 1e6; lat.push(us); if rep == 0 { decisions.push(m); blocked += m as usize; } } }
    lat.sort_by(|a, b| a.partial_cmp(b).unwrap());
    let mean = lat.iter().sum::<f64>() / lat.len() as f64;
    println!("match: mean {:.2} us  p50 {:.2}  p99 {:.2}  max {:.0} | blocked {} of {}", mean, lat[lat.len() / 2], lat[lat.len() * 99 / 100], lat[lat.len() - 1], blocked, reqs.len());
    // decisions out for agreement analysis
    std::fs::write("brave_decisions.txt", decisions.iter().map(|d| if *d { "1" } else { "0" }).collect::<Vec<_>>().join("\n")).unwrap();
    // serialized form: size, load time, memory
    let t1 = Instant::now(); let ser = engine.serialize(); let st = t1.elapsed();
    println!("serialize: {:.2} MB in {:.0} ms", ser.len() as f64 / 1e6, st.as_secs_f64() * 1000.0);
    drop(engine);
    let r2 = rss_kb(); let t2 = Instant::now(); let mut e2 = Engine::default(); let ok = e2.deserialize(&ser).is_ok(); let dt = t2.elapsed(); let r3 = rss_kb();
    println!("deserialize: ok={} {:.1} ms, RSS +{:.1} MB", ok, dt.as_secs_f64() * 1000.0, (r3 as i64 - r2 as i64) as f64 / 1024.0);
    let mut lat2: Vec<f64> = Vec::new(); for r in &reqs { let a = Instant::now(); let _ = e2.check_network_request(r); lat2.push(a.elapsed().as_secs_f64() * 1e6); } lat2.sort_by(|a, b| a.partial_cmp(b).unwrap());
    println!("match after deserialize: mean {:.2} us  p50 {:.2}  p99 {:.2}", lat2.iter().sum::<f64>() / lat2.len() as f64, lat2[lat2.len() / 2], lat2[lat2.len() * 99 / 100]);
}
