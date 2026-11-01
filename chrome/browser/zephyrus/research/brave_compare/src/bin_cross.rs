use adblock::lists::{FilterSet, ParseOptions};
use adblock::request::Request;
use adblock::Engine;
fn main() {
    let text = std::fs::read_to_string("all_lists.txt").unwrap();
    let mut fs = FilterSet::new(true);
    fs.add_filter_list(text, ParseOptions::default());
    let engine = Engine::new_with_filter_set(fs);
    let (mut agree, mut z_only, mut b_only, mut skipped) = (0usize, 0usize, 0usize, 0usize);
    let mut samples_z: Vec<String> = vec![]; let mut samples_b: Vec<String> = vec![];
    for line in std::fs::read_to_string("probes.tsv").unwrap().lines() {
        let p: Vec<&str> = line.split('\t').collect(); if p.len() < 4 { continue; }
        let z = p[3] == "1";
        let req = match Request::new(p[0], p[1], p[2], "GET") { Ok(r) => r, Err(_) => { skipped += 1; continue } };
        let r = engine.check_network_request(&req); let b = r.should_block();
        if z == b { agree += 1; } else if z { z_only += 1; if samples_z.len() < 400 { samples_z.push(format!("{}\t{}\t{}\t{:?}", p[0], p[1], p[2], r.exception.as_ref().map(|e| e.raw_line.clone().unwrap_or_default().chars().take(90).collect::<String>()))); } }
        else { b_only += 1; if samples_b.len() < 400 { samples_b.push(format!("{}\t{}\t{}\t{:?}", p[0], p[1], p[2], r.filter.as_ref().map(|e| e.raw_line.clone().unwrap_or_default().chars().take(90).collect::<String>()))); } }
    }
    println!("probes: agree {}  | Zephyrus blocks, Brave allows {}  | Brave blocks, Zephyrus allows {}  | unparseable by Brave {}", agree, z_only, b_only, skipped);
    std::fs::write("mismatch_zeph_only.tsv", samples_z.join("\n")).unwrap(); std::fs::write("mismatch_brave_only.tsv", samples_b.join("\n")).unwrap();
}
