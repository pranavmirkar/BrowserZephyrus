use adblock::lists::{parse_filter, ParseOptions, ParsedLine};
fn main() {
    let text = std::fs::read_to_string("parity/lines.tsv").unwrap();
    let mut out = String::new();
    for l in text.lines() {
        let rule = l.splitn(2, '\t').nth(1).unwrap_or("");
        let r = match parse_filter(rule, false, ParseOptions::default()) {
            Ok(ParsedLine::Network(_)) => "N".to_string(),
            Ok(ParsedLine::Cosmetic(_)) => "C".to_string(),
            Err(e) => format!("E:{:?}", e).chars().take(80).collect::<String>().replace('\t', " "),
        };
        out.push_str(&r); out.push('\n');
    }
    std::fs::write("parity/brave.txt", out).unwrap();
}
