// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Speaking to cloud model providers: building each one's request and reading
//! each one's reply.
//!
//! Here and not in the browser because a provider's reply is model output, and
//! parsing untrusted model output is what this sandboxed crate is for. See
//! `chrome/browser/zephyrus/docs/adr/0004-cloud-models-are-spoken-to-by-the-kernel.md`.
//!
//! The kernel never holds a key and never chooses a host. A request is a path,
//! a few non-secret headers and a body; the browser adds the key and sends it to
//! the endpoint the user configured, after checking the path and headers against
//! its own list.
//!
//! Every step of the loop is one self-contained turn (the loop rebuilds its
//! prompt, history included, each time), so a request is always one system
//! prompt and one user message, plus the tool definitions. The tool call is
//! forced where the provider allows it: a step that is not a tool call is a
//! wasted step.

use serde_json::{json, Map, Value};

/// The request formats we speak. Many services speak one of these: the
/// OpenAI-compatible format also covers OpenRouter, DeepSeek, vLLM and Ollama.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Kind {
    Anthropic,
    OpenAiCompatible,
    Gemini,
}

impl Kind {
    pub fn parse(name: &str) -> Option<Kind> {
        match name {
            "anthropic" => Some(Kind::Anthropic),
            "openai" => Some(Kind::OpenAiCompatible),
            "gemini" => Some(Kind::Gemini),
            _ => None,
        }
    }
}

/// One tool as a provider is told about it.
#[derive(Debug, Clone)]
pub struct WireTool {
    /// The contract's name, e.g. `page.click`.
    pub name: String,
    /// The name on the wire, e.g. `page_click`. Anthropic and OpenAI allow
    /// letters, digits, `_` and `-` only.
    pub wire: String,
    pub description: String,
    /// The contract's JSON Schema for the arguments.
    pub parameters: Value,
}

/// The contract's tools, shaped for providers. Built once when the kernel loads.
#[derive(Debug, Clone)]
pub struct ToolTable {
    tools: Vec<WireTool>,
}

impl ToolTable {
    /// Every failure is fatal and named, like the contract's own parse: a wire
    /// name that maps back to two tools would let a provider's reply mean
    /// either.
    pub fn from_contract(contract_json: &str) -> Result<ToolTable, String> {
        let root: Value = serde_json::from_str(contract_json)
            .map_err(|e| format!("contract is not valid JSON: {e}"))?;
        let list = root
            .get("tools")
            .and_then(Value::as_array)
            .ok_or("contract has no `tools` list")?;
        let mut tools: Vec<WireTool> = Vec::new();
        for entry in list {
            let name = entry
                .get("name")
                .and_then(Value::as_str)
                .ok_or("a tool has no name")?
                .to_string();
            let wire = wire_name(&name);
            if !is_wire_safe(&wire) {
                return Err(format!("tool `{name}` has no provider-safe name"));
            }
            if tools.iter().any(|t| t.wire == wire) {
                return Err(format!("two tools share the wire name `{wire}`"));
            }
            tools.push(WireTool {
                wire,
                description: entry
                    .get("description")
                    .and_then(Value::as_str)
                    .unwrap_or_default()
                    .to_string(),
                parameters: entry
                    .get("parameters")
                    .cloned()
                    .unwrap_or_else(|| json!({"type": "object"})),
                name,
            });
        }
        Ok(ToolTable { tools })
    }

    /// The contract name for a name a provider sent back, or None.
    ///
    /// Only an exact wire name maps. A provider that echoes the contract's own
    /// dotted name is accepted too, because some OpenAI-compatible servers do
    /// not enforce the name rule and models copy what the prompt shows them.
    pub fn contract_name(&self, from_wire: &str) -> Option<&str> {
        self.tools
            .iter()
            .find(|t| t.wire == from_wire || t.name == from_wire)
            .map(|t| t.name.as_str())
    }

    pub fn tools(&self) -> &[WireTool] {
        &self.tools
    }
}

fn wire_name(name: &str) -> String {
    name.replace('.', "_")
}

fn is_wire_safe(name: &str) -> bool {
    !name.is_empty()
        && name.len() <= 64
        && name
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || c == '_' || c == '-')
}

/// A model name is spliced into Gemini's URL path, so it must not be able to
/// walk out of it. Provider model ids are letters, digits and `.-_:/`, and `/`
/// is refused here: no Gemini model id needs one, and it is how a path escapes.
fn is_path_safe_model(model: &str) -> bool {
    !model.is_empty()
        && model.len() <= 128
        && !model.contains("..")
        && model
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || matches!(c, '.' | '-' | '_' | ':'))
}

/// What to ask.
pub struct Turn<'a> {
    pub kind: Kind,
    pub model: &'a str,
    pub system: &'a str,
    pub user: &'a str,
    /// Enough for one tool call with a sentence of argument; a long answer in
    /// task.complete is the largest thing a step produces.
    pub max_tokens: u32,
    /// A JPEG screenshot of the page, base64, or empty. Sent as an image
    /// alongside the prompt; element ids are drawn on it.
    pub image_jpeg_base64: &'a str,
    /// Whether to insist on a tool call. Some OpenAI-compatible servers reject
    /// `tool_choice: "required"`; for those the loop reads the text instead.
    pub force_tool: bool,
    /// How hard the model should think per step: "low", "medium" and so on, or
    /// empty for the provider's default. Only Anthropic's current models take
    /// it, and the loop only sets it for those.
    pub effort: &'a str,
}

/// A request for the browser to send. `path` is relative to the base URL the
/// user configured for the provider.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct HttpRequest {
    pub path: String,
    /// Non-secret headers only. The browser adds authentication itself and
    /// refuses anything not on its own list.
    pub headers: Vec<(String, String)>,
    pub body: String,
}

pub fn build(table: &ToolTable, turn: &Turn) -> Result<HttpRequest, String> {
    let mut headers = vec![("content-type".to_string(), "application/json".to_string())];
    let (path, body) = match turn.kind {
        Kind::Anthropic => {
            headers.push(("anthropic-version".to_string(), "2023-06-01".to_string()));
            let tools: Vec<Value> = table
                .tools()
                .iter()
                .map(|t| {
                    json!({
                        "name": t.wire,
                        "description": t.description,
                        "input_schema": t.parameters,
                    })
                })
                .collect();
            let mut body = json!({
                "model": turn.model,
                "max_tokens": turn.max_tokens,
                // The rules and the tool listing are identical on every step,
                // so they are cached; the per-step prompt follows them.
                "system": [{
                    "type": "text",
                    "text": turn.system,
                    "cache_control": {"type": "ephemeral"},
                }],
                "messages": [{"role": "user", "content": if turn.image_jpeg_base64.is_empty() {
                    json!(turn.user)
                } else {
                    json!([
                        {"type": "image", "source": {"type": "base64",
                            "media_type": "image/jpeg", "data": turn.image_jpeg_base64}},
                        {"type": "text", "text": turn.user},
                    ])
                }}],
                "tools": tools,
            });
            // `auto`, never a forced `any`: Claude Opus 5.5 and Fable 5.1
            // answer a forced tool_choice with HTTP 400, and on every current
            // Claude model thinking is on, which forced tool use does not
            // combine with. The prompt already asks for exactly one tool call,
            // and a reply that is prose is read by the extractor like any
            // other. Several calls per turn are allowed: see MAX_CALLS.
            let _ = turn.force_tool;
            body["tool_choice"] = json!({"type": "auto"});
            // How hard the model thinks per step. Thinking is always on for the
            // current models and is most of a step's latency; a browser step is
            // mostly "what is the next click", and `low` is markedly faster and
            // consolidates its calls. Empty leaves the provider's default, which
            // is what a model that does not take the field must get.
            if !turn.effort.is_empty() {
                body["output_config"] = json!({"effort": turn.effort});
            }
            ("/v1/messages".to_string(), body)
        }
        Kind::OpenAiCompatible => {
            let tools: Vec<Value> = table
                .tools()
                .iter()
                .map(|t| {
                    json!({
                        "type": "function",
                        "function": {
                            "name": t.wire,
                            "description": t.description,
                            "parameters": t.parameters,
                        },
                    })
                })
                .collect();
            let mut body = json!({
                "model": turn.model,
                "messages": [
                    {"role": "system", "content": turn.system},
                    {"role": "user", "content": if turn.image_jpeg_base64.is_empty() {
                        json!(turn.user)
                    } else {
                        json!([
                            {"type": "image_url", "image_url": {"url": format!(
                                "data:image/jpeg;base64,{}", turn.image_jpeg_base64)}},
                            {"type": "text", "text": turn.user},
                        ])
                    }},
                ],
                "tools": tools,
                // Several calls per turn, run in order by the loop; see
                // MAX_CALLS for why that is safe.
                "parallel_tool_calls": true,
            });
            if turn.force_tool {
                body["tool_choice"] = json!("required");
            }
            // No max_tokens: OpenAI's newer models reject the field in favour of
            // max_completion_tokens, and older compatible servers reject that
            // one. A tool call is short either way.
            ("/chat/completions".to_string(), body)
        }
        Kind::Gemini => {
            if !is_path_safe_model(turn.model) {
                return Err(format!("`{}` is not a model name", turn.model));
            }
            let declarations: Vec<Value> = table
                .tools()
                .iter()
                .map(|t| {
                    json!({
                        "name": t.wire,
                        "description": t.description,
                        "parameters": gemini_schema(&t.parameters),
                    })
                })
                .collect();
            let mut body = json!({
                "systemInstruction": {"parts": [{"text": turn.system}]},
                "contents": [{"role": "user", "parts": if turn.image_jpeg_base64.is_empty() {
                    json!([{"text": turn.user}])
                } else {
                    json!([
                        {"inline_data": {"mime_type": "image/jpeg",
                            "data": turn.image_jpeg_base64}},
                        {"text": turn.user},
                    ])
                }}],
                "tools": [{"functionDeclarations": declarations}],
                "generationConfig": {"maxOutputTokens": turn.max_tokens},
            });
            if turn.force_tool {
                body["toolConfig"] = json!({"functionCallingConfig": {"mode": "ANY"}});
            }
            (format!("/v1beta/models/{}:generateContent", turn.model), body)
        }
    };
    Ok(HttpRequest {
        path,
        headers,
        body: body.to_string(),
    })
}

/// The subset of JSON Schema Gemini's function declarations accept.
///
/// It rejects `additionalProperties` and several validation keywords outright.
/// Dropping them costs nothing: the kernel checks every call against the full
/// contract regardless of what the provider was told.
fn gemini_schema(schema: &Value) -> Value {
    match schema {
        Value::Object(map) => {
            let mut out = Map::new();
            for (key, value) in map {
                match key.as_str() {
                    "type" | "description" | "enum" | "required" | "minimum" | "maximum" => {
                        out.insert(key.clone(), value.clone());
                    }
                    "properties" => {
                        let mut properties = Map::new();
                        if let Value::Object(fields) = value {
                            for (name, field) in fields {
                                properties.insert(name.clone(), gemini_schema(field));
                            }
                        }
                        out.insert(key.clone(), Value::Object(properties));
                    }
                    "items" => {
                        out.insert(key.clone(), gemini_schema(value));
                    }
                    _ => {}
                }
            }
            Value::Object(out)
        }
        other => other.clone(),
    }
}

/// What came back.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Reply {
    /// The contract name and the arguments as JSON, when the model made a
    /// native tool call.
    pub tool: Option<(String, String)>,
    /// Calls after the first, in the order the model made them. At most
    /// MAX_CALLS - 1; anything beyond that is dropped here.
    pub more: Vec<(String, String)>,
    /// Any text the model wrote. When there is no native call, the loop reads a
    /// call from this with the ordinary extractor, as it does for local models.
    pub text: String,
    pub usage: Usage,
    /// The provider's own reason for stopping, verbatim.
    pub stop: String,
    /// Set when the provider refused or failed, in words the panel can show.
    pub error: Option<String>,
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Usage {
    pub input: u64,
    pub output: u64,
    pub cache_read: u64,
    pub cache_write: u64,
}

/// Read a provider's HTTP reply. Never panics on anything a network can send.
pub fn parse(table: &ToolTable, kind: Kind, status: u16, body: &str) -> Reply {
    let Ok(root) = serde_json::from_str::<Value>(body) else {
        return Reply {
            error: Some(if (200..300).contains(&status) {
                "the model provider sent a reply that is not JSON".to_string()
            } else {
                format!("the model provider answered HTTP {status}")
            }),
            ..Reply::default()
        };
    };
    if !(200..300).contains(&status) {
        return Reply {
            error: Some(provider_error(status, &root)),
            ..Reply::default()
        };
    }
    match kind {
        Kind::Anthropic => parse_anthropic(table, &root),
        Kind::OpenAiCompatible => parse_openai(table, &root),
        Kind::Gemini => parse_gemini(table, &root),
    }
}

/// The provider's own message, which is usually the useful part: "invalid
/// x-api-key", "model not found", "rate limit exceeded". Bounded, because it is
/// shown to the user and a provider decides how long it is.
fn provider_error(status: u16, root: &Value) -> String {
    let message = root
        .pointer("/error/message")
        .or_else(|| root.pointer("/message"))
        .and_then(Value::as_str)
        .unwrap_or("");
    let message: String = message.chars().take(300).collect();
    // Out of credit is not a fault in the task and should not read like one. The
    // provider's own wording ("Your credit balance is too low to access the
    // Anthropic API") sounds like the browser is broken to someone who did not
    // set it up, so say what it is and what can be done about it.
    let lowered = message.to_ascii_lowercase();
    if status == 402
        || lowered.contains("credit balance")
        || lowered.contains("insufficient_quota")
        || lowered.contains("exceeded your current quota")
        || lowered.contains("billing")
    {
        return "the AI service has run out of credit, so this task could not run. Nothing is wrong                 with your task. Add your own API key in the agent settings (the gear), or try again                 once the credit is topped up"
            .to_string();
    }
    if message.is_empty() {
        format!("the model provider answered HTTP {status}")
    } else {
        format!("the model provider answered HTTP {status}: {message}")
    }
}

fn number(value: Option<&Value>) -> u64 {
    value.and_then(Value::as_u64).unwrap_or(0)
}

/// The most calls one turn may make.
///
/// A turn may carry several calls because a person does not look at the
/// screen between clicking a field, typing, and pressing Enter, and a model
/// that must take a whole round trip for each spent most of a real Notion run
/// doing exactly that. It is safe because nothing about each call changes:
/// every one is judged by policy on its own, a call aimed at an element checks
/// the element is still there before acting, and the first call that fails or
/// needs approval ends the batch. Bounded, so one reply cannot become a script.
pub const MAX_CALLS: usize = 8;

fn add_call(reply: &mut Reply, call: Option<(String, String)>) {
    let Some(call) = call else { return };
    if reply.tool.is_none() {
        reply.tool = Some(call);
    } else if reply.more.len() + 1 < MAX_CALLS {
        reply.more.push(call);
    }
}

fn tool_call(table: &ToolTable, wire: &str, arguments: Value) -> Option<(String, String)> {
    // A name the contract does not have is still reported, under the name the
    // model used, so policy can refuse it by name rather than the call
    // vanishing into "no tool call".
    let name = table.contract_name(wire).unwrap_or(wire).to_string();
    Some((name, arguments.to_string()))
}

fn parse_anthropic(table: &ToolTable, root: &Value) -> Reply {
    let mut reply = Reply {
        stop: root
            .get("stop_reason")
            .and_then(Value::as_str)
            .unwrap_or_default()
            .to_string(),
        usage: Usage {
            input: number(root.pointer("/usage/input_tokens")),
            output: number(root.pointer("/usage/output_tokens")),
            cache_read: number(root.pointer("/usage/cache_read_input_tokens")),
            cache_write: number(root.pointer("/usage/cache_creation_input_tokens")),
        },
        ..Reply::default()
    };
    for block in root
        .get("content")
        .and_then(Value::as_array)
        .into_iter()
        .flatten()
    {
        match block.get("type").and_then(Value::as_str) {
            Some("text") => {
                if let Some(text) = block.get("text").and_then(Value::as_str) {
                    reply.text.push_str(text);
                }
            }
            Some("tool_use") => {
                if let Some(wire) = block.get("name").and_then(Value::as_str) {
                    let input = block.get("input").cloned().unwrap_or_else(|| json!({}));
                    let call = tool_call(table, wire, input);
                    add_call(&mut reply, call);
                }
            }
            _ => {}
        }
    }
    if reply.stop == "refusal" && reply.tool.is_none() {
        reply.error = Some("the model declined this step".to_string());
    }
    // Thinking counts against max_tokens. A reply cut off before it chose an
    // action is a limit to raise, not the model saying nothing, and it should
    // say so rather than read as an empty answer.
    if reply.stop == "max_tokens" && reply.tool.is_none() {
        reply.error = Some(
            "the model ran out of room before choosing an action (max_tokens)".to_string(),
        );
    }
    reply
}

fn parse_openai(table: &ToolTable, root: &Value) -> Reply {
    let choice = root.pointer("/choices/0");
    let mut reply = Reply {
        stop: choice
            .and_then(|c| c.get("finish_reason"))
            .and_then(Value::as_str)
            .unwrap_or_default()
            .to_string(),
        text: choice
            .and_then(|c| c.pointer("/message/content"))
            .and_then(Value::as_str)
            .unwrap_or_default()
            .to_string(),
        usage: Usage {
            input: number(root.pointer("/usage/prompt_tokens")),
            output: number(root.pointer("/usage/completion_tokens")),
            cache_read: number(root.pointer("/usage/prompt_tokens_details/cached_tokens")),
            cache_write: 0,
        },
        ..Reply::default()
    };
    for call in choice
        .and_then(|c| c.pointer("/message/tool_calls"))
        .and_then(Value::as_array)
        .into_iter()
        .flatten()
        .filter_map(|c| c.get("function"))
    {
        if let Some(wire) = call.get("name").and_then(Value::as_str) {
            // `arguments` is a JSON STRING in this format. A string that is not
            // JSON is kept as the text it is, so validation can say so; turning
            // it into `{}` would run the tool with no arguments at all.
            let arguments = match call.get("arguments") {
                Some(Value::String(text)) if text.trim().is_empty() => json!({}),
                Some(Value::String(text)) => serde_json::from_str::<Value>(text)
                    .unwrap_or_else(|_| Value::String(text.clone())),
                Some(other) => other.clone(),
                None => json!({}),
            };
            let call = tool_call(table, wire, arguments);
            add_call(&mut reply, call);
        }
    }
    // Input tokens here INCLUDE the cached ones; report them apart so cost is
    // not counted twice.
    reply.usage.input = reply.usage.input.saturating_sub(reply.usage.cache_read);
    reply
}

fn parse_gemini(table: &ToolTable, root: &Value) -> Reply {
    let candidate = root.pointer("/candidates/0");
    let mut reply = Reply {
        stop: candidate
            .and_then(|c| c.get("finishReason"))
            .and_then(Value::as_str)
            .unwrap_or_default()
            .to_string(),
        usage: Usage {
            input: number(root.pointer("/usageMetadata/promptTokenCount")),
            output: number(root.pointer("/usageMetadata/candidatesTokenCount")),
            cache_read: number(root.pointer("/usageMetadata/cachedContentTokenCount")),
            cache_write: 0,
        },
        ..Reply::default()
    };
    reply.usage.input = reply.usage.input.saturating_sub(reply.usage.cache_read);
    for part in candidate
        .and_then(|c| c.pointer("/content/parts"))
        .and_then(Value::as_array)
        .into_iter()
        .flatten()
    {
        if let Some(text) = part.get("text").and_then(Value::as_str) {
            reply.text.push_str(text);
        }
        if let Some(call) = part.get("functionCall") {
            if let Some(wire) = call.get("name").and_then(Value::as_str) {
                let arguments = call.get("args").cloned().unwrap_or_else(|| json!({}));
                let call = tool_call(table, wire, arguments);
                add_call(&mut reply, call);
            }
        }
    }
    if candidate.is_none() {
        // Gemini answers a blocked prompt with no candidates and a reason.
        let why = root
            .pointer("/promptFeedback/blockReason")
            .and_then(Value::as_str)
            .unwrap_or("no candidates");
        reply.error = Some(format!("the model provider returned nothing ({why})"));
    }
    reply
}

#[cfg(test)]
mod tests {
    use super::*;

    fn table() -> ToolTable {
        ToolTable::from_contract(crate::CONTRACT_JSON).expect("the shipped contract must map")
    }

    fn turn(kind: Kind) -> Turn<'static> {
        Turn {
            kind,
            model: "some-model",
            system: "RULES",
            user: "PAGE AND TASK",
            max_tokens: 512,
            image_jpeg_base64: "",
            force_tool: true,
            effort: "",
        }
    }

    #[test]
    fn effort_is_sent_only_when_asked_for_and_only_to_anthropic() {
        let mut low = turn(Kind::Anthropic);
        low.effort = "low";
        assert_eq!(body_of(&low)["output_config"]["effort"], "low");
        assert!(body_of(&turn(Kind::Anthropic)).get("output_config").is_none());
        let mut other = turn(Kind::OpenAiCompatible);
        other.effort = "low";
        assert!(body_of(&other).get("output_config").is_none());
    }

    #[test]
    fn a_screenshot_goes_as_an_image_in_each_providers_shape() {
        let mut anthropic = turn(Kind::Anthropic);
        anthropic.image_jpeg_base64 = "QUJD";
        let body = body_of(&anthropic);
        assert_eq!(body["messages"][0]["content"][0]["type"], "image");
        assert_eq!(body["messages"][0]["content"][0]["source"]["data"], "QUJD");
        assert_eq!(body["messages"][0]["content"][1]["text"], "PAGE AND TASK");

        let mut openai = turn(Kind::OpenAiCompatible);
        openai.image_jpeg_base64 = "QUJD";
        let body = body_of(&openai);
        assert_eq!(body["messages"][1]["content"][0]["image_url"]["url"],
                   "data:image/jpeg;base64,QUJD");

        let mut gemini = turn(Kind::Gemini);
        gemini.image_jpeg_base64 = "QUJD";
        let body = body_of(&gemini);
        assert_eq!(body["contents"][0]["parts"][0]["inline_data"]["data"], "QUJD");

        // No image, no image block: text-only models keep working.
        assert!(body_of(&turn(Kind::OpenAiCompatible))["messages"][1]["content"].is_string());
    }

    fn body(request: &HttpRequest) -> Value {
        serde_json::from_str(&request.body).expect("the body is JSON")
    }

    #[test]
    fn every_tool_gets_a_distinct_safe_wire_name_that_maps_back() {
        let table = table();
        assert_eq!(table.tools().len(), 26);
        for tool in table.tools() {
            assert!(is_wire_safe(&tool.wire), "{}", tool.wire);
            assert_eq!(table.contract_name(&tool.wire), Some(tool.name.as_str()));
        }
        assert_eq!(table.contract_name("page_click"), Some("page.click"));
        assert_eq!(table.contract_name("page.click"), Some("page.click"));
        assert_eq!(table.contract_name("page_clicked"), None);
    }

    #[test]
    fn anthropic_request_never_forces_a_tool_and_caches_the_rules() {
        let request = build(&table(), &turn(Kind::Anthropic)).unwrap();
        assert_eq!(request.path, "/v1/messages");
        assert!(request.headers.contains(&("anthropic-version".into(), "2023-06-01".into())));
        let body = body(&request);
        // Never forced: Opus 5.5 / Fable 5.1 reject it with a 400.
        assert_eq!(body["tool_choice"]["type"], "auto");
        assert!(body["tool_choice"].get("disable_parallel_tool_use").is_none());
        assert_eq!(body["system"][0]["text"], "RULES");
        assert_eq!(body["system"][0]["cache_control"]["type"], "ephemeral");
        assert_eq!(body["messages"][0]["content"], "PAGE AND TASK");
        assert_eq!(body["tools"].as_array().unwrap().len(), 26);
        assert!(body["tools"][0]["input_schema"].is_object());
    }

    #[test]
    fn no_request_carries_anything_secret_looking() {
        for kind in [Kind::Anthropic, Kind::OpenAiCompatible, Kind::Gemini] {
            let request = build(&table(), &turn(kind)).unwrap();
            for (name, _) in &request.headers {
                let name = name.to_ascii_lowercase();
                assert!(!name.contains("auth") && !name.contains("key"), "{name}");
            }
        }
    }

    #[test]
    fn openai_request_requires_one_call() {
        let request = build(&table(), &turn(Kind::OpenAiCompatible)).unwrap();
        assert_eq!(request.path, "/chat/completions");
        let body = body(&request);
        assert_eq!(body["tool_choice"], "required");
        assert_eq!(body["parallel_tool_calls"], true);
        assert_eq!(body["messages"][0]["role"], "system");
        assert_eq!(body["tools"][0]["type"], "function");
        let mut relaxed = turn(Kind::OpenAiCompatible);
        relaxed.force_tool = false;
        assert!(body_of(&relaxed).get("tool_choice").is_none());
    }

    fn body_of(turn: &Turn) -> Value {
        body(&build(&table(), turn).unwrap())
    }

    #[test]
    fn gemini_request_strips_what_gemini_rejects() {
        let request = build(&table(), &turn(Kind::Gemini)).unwrap();
        assert_eq!(request.path, "/v1beta/models/some-model:generateContent");
        let text = request.body;
        assert!(!text.contains("additionalProperties"));
        assert!(!text.contains("minLength"));
        let body: Value = serde_json::from_str(&text).unwrap();
        assert_eq!(body["toolConfig"]["functionCallingConfig"]["mode"], "ANY");
        let declarations = body["tools"][0]["functionDeclarations"].as_array().unwrap();
        assert_eq!(declarations.len(), 26);
    }

    #[test]
    fn a_gemini_model_name_cannot_walk_out_of_the_path() {
        for bad in ["../../v1/files", "a/b", "", "x?key=1", "m#frag", "a b"] {
            let mut t = turn(Kind::Gemini);
            t.model = bad;
            assert!(build(&table(), &t).is_err(), "{bad:?} was accepted");
        }
        let mut ok = turn(Kind::Gemini);
        ok.model = "gemini-2.5-pro";
        assert!(build(&table(), &ok).is_ok());
    }

    #[test]
    fn reads_an_anthropic_tool_call_and_usage() {
        let reply = parse(&table(), Kind::Anthropic, 200, r#"{
            "content": [
                {"type": "text", "text": "Opening it."},
                {"type": "tool_use", "id": "t1", "name": "page_click", "input": {"element_id": "e3"}}
            ],
            "stop_reason": "tool_use",
            "usage": {"input_tokens": 120, "output_tokens": 30,
                      "cache_read_input_tokens": 2000, "cache_creation_input_tokens": 0}
        }"#);
        assert_eq!(reply.tool, Some(("page.click".into(), r#"{"element_id":"e3"}"#.into())));
        assert_eq!(reply.text, "Opening it.");
        assert_eq!(reply.usage, Usage { input: 120, output: 30, cache_read: 2000, cache_write: 0 });
        assert_eq!(reply.error, None);
    }

    #[test]
    fn reads_an_openai_call_whose_arguments_are_a_string() {
        let reply = parse(&table(), Kind::OpenAiCompatible, 200, r#"{
            "choices": [{"finish_reason": "tool_calls", "message": {"content": null,
                "tool_calls": [{"id": "c1", "type": "function",
                    "function": {"name": "browser_navigate", "arguments": "{\"url\":\"https://example.com/\"}"}}]}}],
            "usage": {"prompt_tokens": 1500, "completion_tokens": 20,
                      "prompt_tokens_details": {"cached_tokens": 1000}}
        }"#);
        assert_eq!(
            reply.tool,
            Some(("browser.navigate".into(), r#"{"url":"https://example.com/"}"#.into()))
        );
        assert_eq!(reply.usage.input, 500, "cached tokens must not be counted twice");
        assert_eq!(reply.usage.cache_read, 1000);
    }

    #[test]
    fn broken_openai_arguments_are_kept_for_validation_not_emptied() {
        let reply = parse(&table(), Kind::OpenAiCompatible, 200, r#"{
            "choices": [{"message": {"tool_calls": [{"function":
                {"name": "page_type", "arguments": "{\"element_id\": \"e2\""}}]}}]}"#);
        let (_, arguments) = reply.tool.unwrap();
        assert!(arguments.starts_with('"'), "should stay a string: {arguments}");
    }

    #[test]
    fn reads_a_gemini_function_call() {
        let reply = parse(&table(), Kind::Gemini, 200, r#"{
            "candidates": [{"finishReason": "STOP", "content": {"role": "model", "parts": [
                {"functionCall": {"name": "task_complete", "args": {"answer": "9pm"}}}]}}],
            "usageMetadata": {"promptTokenCount": 800, "candidatesTokenCount": 12}
        }"#);
        assert_eq!(reply.tool, Some(("task.complete".into(), r#"{"answer":"9pm"}"#.into())));
        assert_eq!(reply.usage.input, 800);
    }

    #[test]
    fn text_without_a_call_is_left_for_the_extractor() {
        let reply = parse(&table(), Kind::OpenAiCompatible, 200, r#"{
            "choices": [{"message": {"content": "{\"name\":\"browser.back\",\"arguments\":{}}"}}]}"#);
        assert_eq!(reply.tool, None);
        assert!(reply.text.contains("browser.back"));
    }

    #[test]
    fn an_unknown_tool_keeps_its_name_so_policy_can_refuse_it() {
        let reply = parse(&table(), Kind::Anthropic, 200, r#"{"content": [
            {"type": "tool_use", "name": "shell_exec", "input": {"cmd": "rm"}}]}"#);
        assert_eq!(reply.tool.unwrap().0, "shell_exec");
    }

    #[test]
    fn running_out_of_credit_is_said_plainly() {
        let reply = parse(&table(), Kind::Anthropic, 400, r#"{"type":"error",
            "error":{"type":"invalid_request_error","message":"Your credit balance is too low to access the Anthropic API. Please go to Plans & Billing to upgrade or purchase credits."}}"#);
        let error = reply.error.unwrap();
        assert!(error.contains("run out of credit"), "{error}");
        assert!(!error.contains("Anthropic API"), "the raw provider text leaked: {error}");
        let quota = parse(&table(), Kind::OpenAiCompatible, 429, r#"{"error":{"message":"You exceeded your current quota, please check your plan and billing details."}}"#);
        assert!(quota.error.unwrap().contains("run out of credit"));
    }

    #[test]
    fn provider_errors_are_named_and_bounded() {
        let reply = parse(&table(), Kind::Anthropic, 401, r#"{"type":"error",
            "error":{"type":"authentication_error","message":"invalid x-api-key"}}"#);
        assert_eq!(
            reply.error.as_deref(),
            Some("the model provider answered HTTP 401: invalid x-api-key")
        );
        let long = format!(r#"{{"error":{{"message":"{}"}}}}"#, "x".repeat(5000));
        let reply = parse(&table(), Kind::OpenAiCompatible, 429, &long);
        assert!(reply.error.unwrap().len() < 400);
        let reply = parse(&table(), Kind::Gemini, 502, "<html>bad gateway</html>");
        assert_eq!(reply.error.as_deref(), Some("the model provider answered HTTP 502"));
    }

    #[test]
    fn a_reply_cut_off_by_max_tokens_says_so() {
        // Thinking counts against max_tokens on current Claude models; running
        // out before a tool call must not read as an empty answer.
        let reply = parse(&table(), Kind::Anthropic, 200, r#"{"content": [
            {"type": "thinking", "thinking": ""}], "stop_reason": "max_tokens",
            "usage": {"input_tokens": 900, "output_tokens": 1024}}"#);
        assert!(reply.error.unwrap().contains("max_tokens"));
        assert_eq!(reply.usage.output, 1024, "spend is still counted");
    }

    #[test]
    fn a_blocked_gemini_prompt_is_an_error_not_silence() {
        let reply = parse(&table(), Kind::Gemini, 200,
            r#"{"promptFeedback": {"blockReason": "SAFETY"}}"#);
        assert_eq!(reply.error.as_deref(), Some("the model provider returned nothing (SAFETY)"));
    }

    #[test]
    fn nothing_a_network_sends_makes_it_panic() {
        let bodies = ["", "null", "[]", "{}", "{\"content\": 5}", "{\"choices\": [null]}",
            "{\"candidates\": [{\"content\": {\"parts\": [null, 7, {\"functionCall\": 3}]}}]}",
            "{\"choices\":[{\"message\":{\"tool_calls\":[{\"function\":{\"name\":7}}]}}]}"];
        for kind in [Kind::Anthropic, Kind::OpenAiCompatible, Kind::Gemini] {
            for body in bodies {
                let _ = parse(&table(), kind, 200, body);
            }
        }
    }
    #[test]
    fn several_calls_in_one_turn_are_kept_in_order_and_bounded() {
        let table = table();
        let anthropic = parse(&table, Kind::Anthropic, 200, r#"{"content": [
            {"type": "text", "text": "Typing the list."},
            {"type": "tool_use", "id": "a", "name": "page_click", "input": {"element_id": "e3"}},
            {"type": "tool_use", "id": "b", "name": "page_type", "input": {"text": "Milk"}},
            {"type": "tool_use", "id": "c", "name": "page_press", "input": {"key": "Enter"}}],
            "stop_reason": "tool_use"}"#);
        assert_eq!(anthropic.tool.as_ref().unwrap().0, "page.click");
        let rest: Vec<&str> = anthropic.more.iter().map(|(t, _)| t.as_str()).collect();
        assert_eq!(rest, ["page.type", "page.press"]);

        let openai = parse(&table, Kind::OpenAiCompatible, 200, r#"{"choices": [{"message": {
            "tool_calls": [
              {"function": {"name": "page_type", "arguments": "{\"text\":\"a\"}"}},
              {"function": {"name": "page_press", "arguments": "{\"key\":\"Enter\"}"}}]}}]}"#);
        assert_eq!(openai.tool.as_ref().unwrap().0, "page.type");
        assert_eq!(openai.more, vec![("page.press".to_string(), r#"{"key":"Enter"}"#.to_string())]);

        let gemini = parse(&table, Kind::Gemini, 200, r#"{"candidates": [{"content": {"parts": [
            {"functionCall": {"name": "page_press", "args": {"key": "Tab"}}},
            {"functionCall": {"name": "task_complete", "args": {"answer": "done"}}}]}}]}"#);
        assert_eq!(gemini.tool.as_ref().unwrap().0, "page.press");
        assert_eq!(gemini.more[0].0, "task.complete");

        // Twenty calls in one reply is a script, not a turn.
        let many: Vec<String> = (0..20)
            .map(|i| format!(r#"{{"type": "tool_use", "id": "t{i}", "name": "page_press", "input": {{"key": "Tab"}}}}"#))
            .collect();
        let body = format!(r#"{{"content": [{}]}}"#, many.join(","));
        let bounded = parse(&table, Kind::Anthropic, 200, &body);
        assert_eq!(1 + bounded.more.len(), MAX_CALLS);
    }
}
