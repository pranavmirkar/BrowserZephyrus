// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Parsing the frozen tool contract.
//!
//! `agent/schemas/tools.v1.json` is the single source of truth for what a model
//! may ask the browser to do. The benchmark harness validates against it and so
//! does this kernel. **Neither may define its own copy.** A second copy is a
//! second opinion, and the one that drifts is the one that stops rejecting
//! calls it should reject.
//!
//! Only the parts of JSON Schema the contract actually uses are read here, for
//! the same reason `bench/schema.py` implements only that subset: a permissive
//! reader lets the contract grow constructs one side cannot enforce.

use std::collections::HashMap;

use serde_json::Value;

/// How much damage a call can do. Ordered, so `max` is meaningful.
///
/// The names come from the contract's `conventions.risk` block and mean exactly
/// what it says there.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum Risk {
    /// Read-only. No browser state changes.
    R0,
    /// Reversible mutation. Navigation, tab and scroll state.
    R1,
    /// External side effect. Leaves the browser and is visible to someone else.
    R2,
    /// High impact. Irreversible, financial, or security relevant.
    R3,
}

impl Risk {
    fn parse(text: &str) -> Option<Risk> {
        match text {
            "R0" => Some(Risk::R0),
            "R1" => Some(Risk::R1),
            "R2" => Some(Risk::R2),
            "R3" => Some(Risk::R3),
            _ => None,
        }
    }

    pub fn as_str(self) -> &'static str {
        match self {
            Risk::R0 => "R0",
            Risk::R1 => "R1",
            Risk::R2 => "R2",
            Risk::R3 => "R3",
        }
    }
}

/// One tool, reduced to what the policy engine needs to judge a call.
#[derive(Debug)]
pub struct Tool {
    pub name: String,
    pub description: String,
    /// The risk this tool carries before anything about the target is known.
    ///
    /// A floor, not a verdict. `page.click` is R1 in the contract because
    /// clicking is reversible in general, but clicking a button labelled "Send"
    /// is not. See `policy::escalate`.
    pub floor: Risk,
    pub required: Vec<String>,
    pub properties: Vec<String>,
}

impl Tool {
    /// Check a call's arguments against the shape the contract declares.
    ///
    /// Deliberately shallow: presence and spelling only, no type checking. The
    /// executor validates types when it binds arguments, and duplicating that
    /// here would be the second-copy problem this module exists to avoid. What
    /// the policy layer needs to know is narrower -- whether this call is
    /// well-formed enough to reason about at all.
    pub fn check_shape(&self, arguments: &Value) -> Result<(), String> {
        let Some(map) = arguments.as_object() else {
            return Err("arguments are not a JSON object".to_string());
        };
        for name in &self.required {
            if !map.contains_key(name) {
                return Err(format!("missing required argument `{name}`"));
            }
        }
        // Every tool sets additionalProperties to false. An invented argument is
        // a failed call, not a call with extra data.
        for name in map.keys() {
            if !self.properties.contains(name) {
                return Err(format!("unexpected argument `{name}`"));
            }
        }
        Ok(())
    }
}

#[derive(Debug)]
pub struct Contract {
    pub version: String,
    tools: HashMap<String, Tool>,
}

impl Contract {
    /// Parse `tools.v1.json`.
    ///
    /// Every failure is fatal and named. A kernel holding a half-parsed contract
    /// would allow calls it has no rule for, which is the one outcome worse than
    /// refusing to start.
    pub fn parse(json: &str) -> Result<Contract, String> {
        let root: Value =
            serde_json::from_str(json).map_err(|e| format!("contract is not valid JSON: {e}"))?;

        let version = root
            .get("version")
            .and_then(Value::as_str)
            .ok_or("contract has no `version` string")?
            .to_string();

        let list = root
            .get("tools")
            .and_then(Value::as_array)
            .ok_or("contract has no `tools` array")?;

        let mut tools = HashMap::new();
        for (index, entry) in list.iter().enumerate() {
            let name = entry
                .get("name")
                .and_then(Value::as_str)
                .ok_or_else(|| format!("tool #{index} has no `name`"))?;

            let risk_text = entry
                .get("risk")
                .and_then(Value::as_str)
                .ok_or_else(|| format!("tool `{name}` has no `risk`"))?;
            let floor = Risk::parse(risk_text)
                .ok_or_else(|| format!("tool `{name}` has unknown risk `{risk_text}`"))?;

            let parameters = entry
                .get("parameters")
                .ok_or_else(|| format!("tool `{name}` has no `parameters`"))?;

            // An object schema that forgot additionalProperties:false would let
            // an invented argument through every check below. The contract says
            // every tool sets it; verify rather than trust.
            if parameters.get("type").and_then(Value::as_str) == Some("object")
                && parameters.get("additionalProperties") != Some(&Value::Bool(false))
            {
                return Err(format!(
                    "tool `{name}` does not set additionalProperties to false"
                ));
            }

            let properties = parameters
                .get("properties")
                .and_then(Value::as_object)
                .map(|m| m.keys().cloned().collect())
                .unwrap_or_default();

            let required = parameters
                .get("required")
                .and_then(Value::as_array)
                .map(|a| {
                    a.iter()
                        .filter_map(Value::as_str)
                        .map(str::to_string)
                        .collect()
                })
                .unwrap_or_default();

            let description = entry
                .get("description")
                .and_then(Value::as_str)
                .unwrap_or_default()
                .to_string();

            if tools
                .insert(
                    name.to_string(),
                    Tool {
                        name: name.to_string(),
                        description,
                        floor,
                        required,
                        properties,
                    },
                )
                .is_some()
            {
                return Err(format!("contract defines tool `{name}` twice"));
            }
        }

        if tools.is_empty() {
            return Err("contract defines no tools".to_string());
        }

        Ok(Contract { version, tools })
    }

    pub fn tool(&self, name: &str) -> Option<&Tool> {
        self.tools.get(name)
    }

    pub fn tool_count(&self) -> usize {
        self.tools.len()
    }

    /// The tool list as the model is shown it.
    ///
    /// Generated from the contract rather than written out, so the prompt
    /// cannot drift from what the kernel will actually enforce.
    ///
    /// **No parentheses around the arguments.** With them, qwen2.5:1.5b emitted
    /// `{"name": "browser.back()"}` -- copying the punctuation straight out of
    /// the listing into the tool name. Removing them took that model from 12%
    /// to 25% correct. The prompt is under test as much as the model is.
    pub fn prompt_listing(&self) -> String {
        let mut names: Vec<&String> = self.tools.keys().collect();
        names.sort();

        let mut out = String::new();
        for name in names {
            let tool = &self.tools[name];
            let mut args: Vec<String> = tool
                .properties
                .iter()
                .map(|property| {
                    if tool.required.contains(property) {
                        property.clone()
                    } else {
                        format!("{property}?")
                    }
                })
                .collect();
            args.sort();

            out.push_str(&format!(
                "- {} [{}] {}\n  args: {}\n",
                tool.name,
                tool.floor.as_str(),
                tool.description,
                if args.is_empty() {
                    "none".to_string()
                } else {
                    args.join(", ")
                }
            ));
        }
        out
    }
}
