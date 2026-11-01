use adblock::lists::{FilterSet, ParseOptions};
use adblock::request::Request;
use adblock::Engine;
use std::time::Instant;
fn main() {
    let a: Vec<String> = std::env::args().collect();
    let text = std::fs::read_to_string(&a[1]).unwrap();
    let mut fs = FilterSet::new(false);
    fs.add_filter_list(text, ParseOptions::default());
    let engine = Engine::new_with_filter_set(fs);
    let rows: Vec<Vec<String>> = std::fs::read_to_string(&a[2]).unwrap().lines().map(|l| l.split('\t').map(|s| s.to_string()).collect()).collect();
    let reqs: Vec<Option<Request>> = rows.iter().map(|c| Request::new(&c[1], &c[2], &c[3], &c[8]).ok()).collect();
    for r in reqs.iter().flatten() { let _ = engine.check_network_request(r); }
    let mut lat: Vec<f64> = vec![]; let mut dec: Vec<u8> = vec![]; let mut page: std::collections::BTreeMap<String, f64> = Default::default();
    for (c, r) in rows.iter().zip(reqs.iter()) {
        match r { Some(r) => { let t = Instant::now(); let b = engine.check_network_request(r).should_block(); let us = t.elapsed().as_secs_f64() * 1e6; lat.push(us); dec.push(b as u8); *page.entry(c[0].clone()).or_default() += us; } None => dec.push(0) }
    }
    lat.sort_by(|a, b| a.partial_cmp(b).unwrap());
    println!("Brave adblock-rust on real traffic: {} requests ({} unparseable by it): mean {:.2} us  p50 {:.2}  p99 {:.2}  max {:.0} | blocked {} ({:.1}%)", rows.len(), rows.len() - lat.len(), lat.iter().sum::<f64>() / lat.len() as f64, lat[lat.len() / 2], lat[lat.len() * 99 / 100], lat[lat.len() - 1], dec.iter().map(|&d| d as usize).sum::<usize>(), 100.0 * dec.iter().map(|&d| d as f64).sum::<f64>() / dec.len() as f64);
    println!("per page: {:.2} ms of matching on average", page.values().sum::<f64>() / page.len() as f64 / 1000.0);
    std::fs::write(&a[3], dec.iter().map(|d| d.to_string()).collect::<Vec<_>>().join("\n")).unwrap();
}
