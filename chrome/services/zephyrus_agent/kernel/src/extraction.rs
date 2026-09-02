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

use serde_json::Value;

/// A tool call recovered from model output.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ExtractedCall {
    pub name: String,
    /// Always valid JSON. `{}` when the model named a tool and gave nothing.
    pub arguments_json: String,
}

/// Recover the first plausible tool call from `response`, or None.
pub fn extract_call(response: &str) -> Option<ExtractedCall> {
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
    None
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
