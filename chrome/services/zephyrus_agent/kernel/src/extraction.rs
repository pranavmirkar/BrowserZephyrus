// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Recovering a tool call from whatever the model actually emitted.
//!
//! This is in Rust and in the kernel for the same reason the policy is: it is
//! the first thing to touch untrusted model output, and it is all string
//! handling on text that is trying to be JSON and often isn't.
//!
//! The behaviour here is not guesswork. It is what the benchmark in
//! `chrome/browser/zephyrus/agent/benchmark` measured local models actually
//! doing, and each accommodation below has a recorded failure behind it:
//!
//! - Models wrap calls in prose, in ```json fences, or emit them bare. All
//!   three are accepted, because the real runtime will eventually be given a
//!   grammar that removes the ambiguity and punishing formatting here would
//!   measure the harness rather than the model.
//! - qwen2.5:7b emitted `{"name": "task.complete", "arguments": "4.2.1"}` --
//!   right tool, right answer, arguments a bare string. Dropping that on the
//!   floor reported "no tool call found", which sends you hunting a formatting
//!   problem when the truth is a schema one. It is kept and allowed to fail
//!   validation, where the error says what is actually wrong.
//! - qwen2.5:7b wrote CALLS, not JSON: `task.ask("Did you mean ...")`, twenty
//!   times out of twenty in one traced run. There is not a brace in it, so the
//!   JSON scan below found nothing and every single step was thrown away with
//!   "the model did not choose an action". The model had understood the task
//!   and picked a real tool; the harness could not read the sentence. A tool
//!   listing that reads `- task.ask [R0] ...  args: question` looks exactly
//!   like a function signature, so this is a shape we invited.
//! - The same model then wrote `task.complete []` -- the tool name, a space,
//!   and an empty list meaning "no arguments" -- twenty times, while trying to
//!   end a task it had already finished. The first version of the call reader
//!   insisted on parentheses and missed it. Anchor on the NAME, and accept
//!   whatever bracket follows, or this is one fix per punctuation mark.

use serde_json::Value;

/// A tool call recovered from model output.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ExtractedCall {
    pub name: String,
    /// Always valid JSON. `{}` when the model named a tool and gave nothing.
    pub arguments_json: String,
}

/// Recover the first plausible tool call from `response`, or None.
pub fn extract_call(response: &str, known: &[String]) -> Option<ExtractedCall> {
    for candidate in candidates(response) {
        let Ok(parsed) = serde_json::from_str::<Value>(&candidate) else {
            continue;
        };
        let Some(object) = parsed.as_object() else {
            continue;
        };

        // `tool` as well as `name`: models reach for both, and the contract
        // costs nothing by accepting either.
        let name = object
            .get("name")
            .or_else(|| object.get("tool"))
            .and_then(Value::as_str);
        let Some(name) = name else {
            continue;
        };

        let arguments = object
            .get("arguments")
            .or_else(|| object.get("parameters"))
            .cloned()
            .unwrap_or(Value::Object(serde_json::Map::new()));

        return Some(ExtractedCall {
            name: name.to_string(),
            // Preserved verbatim even when it is not an object. Validation is
            // the policy engine's job and it gives a better error than this
            // could.
            arguments_json: arguments.to_string(),
        });
    }

    // Nothing JSON-shaped named a tool. Try the call form.
    extract_call_syntax(response, known)
}

/// Recover `tool.name(...)` written as a call rather than as JSON.
///
/// Deliberately narrow. It requires a dotted lowercase name immediately
/// followed by a parenthesis, which is what the tool listing looks like and is
/// not a shape that turns up in prose by accident.
fn extract_call_syntax(response: &str, known: &[String]) -> Option<ExtractedCall> {
    let text = response.trim();

    // The name has to be a tool that actually exists.
    //
    // Guessing at the shape of a name was the wrong approach twice over. Too
    // strict and `browser.navigate https://...` -- an unmistakable call -- was
    // thrown away for not putting a bracket after the name. Too loose and any
    // dotted word starts looking like a call. The contract knows exactly which
    // names are real, so ask it, and then accept ANY arguments that follow.
    //
    // Longest match, so `task.complete` is never read as some shorter tool that
    // happens to be a prefix of it.
    let name = known
        .iter()
        .filter(|candidate| text.starts_with(candidate.as_str()))
        .max_by_key(|candidate| candidate.len())?;

    // A name has to end where the name ends: `page.clicked` must not be read as
    // `page.click` with an argument of "ed".
    //
    // Checked on the character IMMEDIATELY after the name, before any trimming.
    // Trimming first threw away the space that separates a name from its
    // argument, so `browser.navigate https://...` looked like a name running on
    // into "https" and was rejected -- the very call this was meant to accept.
    let after = &text[name.len()..];
    if after
        .chars()
        .next()
        .is_some_and(|c| c.is_ascii_alphanumeric() || c == '_' || c == '.')
    {
        return None;
    }
    let mut rest = after.trim();

    // Drop the risk tag if the model copied one out of the tool listing.
    //
    // The listing we print reads `- browser.navigate [R1] Load a URL ...`, and
    // qwen2.5:7b duly replied `browser.navigate [R1] {"url": "..."}`. Treating
    // that whole tail as the argument made the URL the literal text
    // `[R1] {"url": ...}`, which is not an address -- so the navigation failed,
    // AND the kernel could not read a destination out of it, so it asked the
    // user to approve every single one. We printed that tag; refusing to read
    // it back is our bug, not the model's.
    if let Some(after_tag) = strip_risk_tag(rest) {
        rest = after_tag;
    }

    // Unwrap one layer of parentheses, if that is what was used.
    let inside = if rest.starts_with('(') {
        let close = rest.rfind(')')?;
        rest[1..close].trim()
    } else {
        rest
    };

    let arguments_json = if inside.is_empty() {
        "{}".to_string()
    } else if let Ok(value) = serde_json::from_str::<Value>(inside) {
        match value {
            // `[]` is how a model writes "no arguments". An array is never a
            // valid argument list here, and an empty one plainly means none.
            Value::Array(ref items) if items.is_empty() => "{}".to_string(),
            // Anything else JSON-shaped is kept as it is: an object, or a bare
            // scalar like `task.ask("why?")` which the contract wraps into
            // whichever single argument the tool actually takes.
            other => other.to_string(),
        }
    } else if let Some(pairs) = keyword_arguments(inside) {
        pairs.to_string()
    } else if let Some(list) = positional_arguments(inside) {
        list.to_string()
    } else {
        // Unquoted and not JSON -- treat the whole of it as the one argument.
        Value::String(inside.to_string()).to_string()
    };

    Some(ExtractedCall {
        name: name.clone(),
        arguments_json,
    })
}

/// The text after a leading `[R0]`..`[R9]`, or None if there is no such tag.
///
/// Narrow on purpose. The first version stripped ANY leading bracket group that
/// did not parse as JSON, which was a rule about punctuation rather than about
/// the tag -- and it ate the argument of
/// `task.complete [ASUS Prime GeForce RTX 5060 Ti ...]`, leaving nothing, so
/// the harness refused the call for saying nothing. A tag is `[R` a digit `]`
/// and nothing else.
fn strip_risk_tag(rest: &str) -> Option<&str> {
    let inner = rest.strip_prefix("[R")?;
    let (digit, after) = inner.split_at(inner.char_indices().nth(1)?.0);
    if !digit.chars().all(|c| c.is_ascii_digit()) {
        return None;
    }
    Some(after.strip_prefix(']')?.trim())
}

/// `"e1", "Price"` as a list, or None if it is not that.
///
/// The contract maps it onto the tool's properties in order. Written because
/// qwen2.5:7b called `page.select "e1", "Price"` -- which says exactly what it
/// means -- and got back "arguments are not a JSON object".
fn positional_arguments(inside: &str) -> Option<Value> {
    let pieces = split_top_level(inside);
    if pieces.len() < 2 {
        return None;
    }
    let mut items = Vec::new();
    for piece in pieces {
        let piece = piece.trim();
        if piece.is_empty() {
            return None;
        }
        // Every piece has to be a value in its own right. Anything else is
        // prose with a comma in it, not an argument list.
        items.push(serde_json::from_str::<Value>(piece).ok()?);
    }
    Some(Value::Array(items))
}

/// `element_id="e3", text="hello"` as an object, or None if it is not that.
fn keyword_arguments(inside: &str) -> Option<Value> {
    let mut object = serde_json::Map::new();
    for piece in split_top_level(inside) {
        let (key, value) = piece.split_once('=')?;
        let key = key.trim();
        if key.is_empty() || !key.chars().all(|c| c.is_ascii_lowercase() || c == '_') {
            return None;
        }
        let value = value.trim();
        let parsed = serde_json::from_str::<Value>(value)
            .unwrap_or_else(|_| Value::String(value.trim_matches('\'').to_string()));
        object.insert(key.to_string(), parsed);
    }
    if object.is_empty() {
        return None;
    }
    Some(Value::Object(object))
}

/// Split on commas that are not inside quotes or brackets.
fn split_top_level(inside: &str) -> Vec<&str> {
    let mut out = Vec::new();
    let mut depth = 0i32;
    let mut quoted = false;
    let mut start = 0usize;
    let bytes = inside.as_bytes();
    for (i, c) in inside.char_indices() {
        match c {
            '"' if i == 0 || bytes[i - 1] != b'\\' => quoted = !quoted,
            '{' | '[' if !quoted => depth += 1,
            '}' | ']' if !quoted => depth -= 1,
            ',' if !quoted && depth == 0 => {
                out.push(&inside[start..i]);
                start = i + 1;
            }
            _ => {}
        }
    }
    out.push(&inside[start..]);
    out
}

/// Ordered candidate JSON substrings, most likely first.
fn candidates(response: &str) -> Vec<String> {
    let mut found = Vec::new();

    // Fenced blocks first: when a model uses one, what is inside it is what it
    // meant, and prose around it may contain braces of its own.
    let mut rest = response;
    while let Some(start) = rest.find("```") {
        let after = &rest[start + 3..];
        // Skip an optional language tag on the fence line.
        let body_start = after.find('\n').map(|i| i + 1).unwrap_or(0);
        let body = &after[body_start..];
        match body.find("```") {
            Some(end) => {
                found.push(body[..end].trim().to_string());
                rest = &body[end..];
            }
            None => break,
        }
    }

    // Then any balanced brace run, outermost first. A cheap scan rather than a
    // parser: model output is short, and this only has to be good enough to
    // find something serde will then judge properly.
    let bytes = response.as_bytes();
    let mut depth = 0usize;
    let mut start = None;
    for (i, &byte) in bytes.iter().enumerate() {
        match byte {
            b'{' => {
                if depth == 0 {
                    start = Some(i);
                }
                depth += 1;
            }
            b'}' => {
                depth = depth.saturating_sub(1);
                if depth == 0 {
                    if let Some(from) = start.take() {
                        // Byte indices from an ASCII scan land on char
                        // boundaries for `{` and `}`, so this cannot split a
                        // multibyte character.
                        found.push(response[from..=i].to_string());
                    }
                }
            }
            _ => {}
        }
    }

    found
}

#[cfg(test)]
mod tests {
    /// The shipped contract's tool names, so these tests match what runs.
    fn known_for_test() -> Vec<String> {
        crate::contract::Contract::parse(crate::CONTRACT_JSON)
            .expect("the shipped contract must parse")
            .tool_names()
    }

    fn extract_call(response: &str) -> Option<super::ExtractedCall> {
        super::extract_call(response, &known_for_test())
    }

    use super::*;

    fn call(name: &str, arguments_json: &str) -> Option<ExtractedCall> {
        Some(ExtractedCall {
            name: name.to_string(),
            arguments_json: arguments_json.to_string(),
        })
    }

    #[test]
    fn reads_a_bare_object() {
        assert_eq!(
            extract_call(r#"{"name":"browser.back","arguments":{}}"#),
            call("browser.back", "{}")
        );
    }

    #[test]
    fn reads_one_wrapped_in_prose() {
        let response = "Sure! I'll go back now.\n\
                        {\"name\": \"browser.back\", \"arguments\": {}}\n\
                        Let me know if that helps.";
        assert_eq!(extract_call(response), call("browser.back", "{}"));
    }

    #[test]
    fn prefers_what_is_inside_a_fence() {
        // The prose mentions a different tool. The fence is what the model
        // meant, so it must win.
        let response = "I could use page.find here, but:\n\
                        ```json\n\
                        {\"name\": \"page.observe\", \"arguments\": {\"level\": 1}}\n\
                        ```";
        let extracted = extract_call(response).unwrap();
        assert_eq!(extracted.name, "page.observe");
    }

    #[test]
    fn accepts_tool_as_well_as_name() {
        assert_eq!(
            extract_call(r#"{"tool":"tabs.list","parameters":{}}"#),
            call("tabs.list", "{}")
        );
    }

    #[test]
    fn keeps_a_near_miss_instead_of_dropping_it() {
        // Verbatim from qwen2.5:7b. Right tool, right answer, wrong shape.
        // Reporting "no tool call found" for this sends you looking for a
        // formatting bug when the problem is a schema one.
        assert_eq!(
            extract_call(r#"{"name": "task.complete", "arguments": "4.2.1"}"#),
            call("task.complete", "\"4.2.1\"")
        );
    }

    #[test]
    fn a_missing_arguments_field_becomes_an_empty_object() {
        assert_eq!(extract_call(r#"{"name":"browser.back"}"#), call("browser.back", "{}"));
    }

    #[test]
    fn finds_nothing_in_prose() {
        assert_eq!(extract_call("I am not sure what you want me to do."), None);
        assert_eq!(extract_call(""), None);
    }

    #[test]
    fn ignores_json_that_is_not_a_call() {
        // An object with no name is not a call. The scan must move on to the
        // next candidate rather than giving up at the first brace run.
        let response = "{\"thinking\": \"hmm\"} then \
                        {\"name\": \"browser.back\", \"arguments\": {}}";
        assert_eq!(extract_call(response), call("browser.back", "{}"));
    }

    #[test]
    fn survives_unbalanced_and_multibyte_text() {
        // Must not panic. A byte-index scan over text with multibyte
        // characters is exactly where a careless slice would.
        assert_eq!(extract_call("{{{ broken -- ünïcödé — 日本語"), None);
        assert_eq!(extract_call("}}}"), None);
    }
}
