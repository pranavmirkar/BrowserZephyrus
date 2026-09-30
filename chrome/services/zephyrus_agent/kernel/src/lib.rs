// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Zephyrus agent kernel.
//!
//! The kernel decides whether a tool call the model proposed may run. It is the
//! authorization boundary for the agent, and it is written in Rust and kept out
//! of the renderer for the reasons in
//! `chrome/browser/zephyrus/docs/adr/0001-agent-kernel-runs-in-its-own-process.md`.
//!
//! Two properties matter more than anything else here, and both are structural
//! rather than a matter of care:
//!
//! 1. **The contract is embedded, not supplied.** `tools.v1.json` is compiled in
//!    with `include_str!`. There is no API for handing the kernel a different
//!    contract, so no caller -- compromised or merely wrong -- can widen what
//!    the agent is allowed to do by passing a more permissive one.
//!
//! 2. **Page text is not an input.** The bridge below accepts a tool name,
//!    arguments, the user's task, the page URL, and the browser's own list of
//!    elements. It does not accept page content, because page content is where
//!    injections live and there is nothing useful the kernel can do with it.

// The crate is built with `allow_unsafe = true` because the code cxx generates
// for the bridge below needs it. That flag is crate-wide, so the two modules
// holding the actual decision logic forbid unsafe individually: the reason the
// crate relaxes the rule is the FFI glue, and nothing else gets to inherit it.
#[forbid(unsafe_code)]
mod contract;
#[forbid(unsafe_code)]
mod extraction;
#[forbid(unsafe_code)]
mod policy;
#[forbid(unsafe_code)]
mod providers;

#[cfg(test)]
mod tests;

use contract::Contract;
use policy::{Disposition as Verdict, Element, Request};

/// The frozen V1 tool contract, compiled in.
///
/// The same bytes the benchmark harness reads. `agent/benchmark/README.md` says
/// neither side may define its own copy; embedding the file is how that stops
/// being a promise and starts being a build error.
const CONTRACT_JSON: &str = include_str!(
    "../../../../browser/zephyrus/agent/schemas/tools.v1.json"
);

#[cxx::bridge(namespace = "zephyrus::agent")]
mod ffi {
    /// One interactive element, exactly as the browser described it in the
    /// Observation the model was shown.
    struct ObservedElement {
        id: String,
        role: String,
        name: String,
        /// What private thing the browser judged this to hold, or empty.
        /// A bare word: "password", "payment_card", "email", "phone",
        /// "personal_name".
        sensitivity: String,
    }

    /// What the browser should do with a proposed call.
    #[derive(Debug)]
    enum Disposition {
        /// Run it.
        Allow,
        /// Run it only after the user approves this specific call.
        Ask,
        /// Do not run it, and do not offer to.
        Deny,
    }

    /// A proposed tool call and the context needed to judge it.
    ///
    /// Note the absence of page text. That is deliberate; see the module docs.
    struct PolicyRequest {
        /// Tool name, e.g. `page.click`.
        tool: String,
        /// The model's arguments, as JSON. Parsed and checked here rather than
        /// trusted, so a malformed call is a decision the kernel makes and not
        /// a crash it suffers.
        arguments_json: String,
        /// The user's own words. The only free text here that is trusted.
        task: String,
        url: String,
        elements: Vec<ObservedElement>,
    }

    /// A tool call recovered from raw model output.
    struct ExtractedCall {
        /// False when nothing call-shaped could be found at all.
        found: bool,
        tool: String,
        /// Always valid JSON; `{}` when the model gave no arguments.
        arguments_json: String,
    }

    /// One non-secret HTTP header of a provider request.
    struct ProviderHeader {
        name: String,
        value: String,
    }

    /// A request for the browser to send to a cloud model provider. See
    /// `providers` and ADR 0004: no key, no host, just what to say.
    struct ProviderRequest {
        /// Empty on success; otherwise why no request could be built.
        error: String,
        /// Relative to the base URL the user configured for the provider.
        path: String,
        headers: Vec<ProviderHeader>,
        body: String,
    }

    /// A provider's reply, read.
    struct ProviderReply {
        /// Empty on success; otherwise words the panel can show.
        error: String,
        /// True when the model made a native tool call.
        found: bool,
        /// The CONTRACT name, mapped back from the wire name.
        tool: String,
        arguments_json: String,
        /// Calls the model made after the first in the same turn, in order:
        /// contract names and their arguments, the two lists the same length.
        more_tools: Vec<String>,
        more_arguments_json: Vec<String>,
        /// Text the model wrote. Read with extract_call when `found` is false.
        text: String,
        input_tokens: u64,
        output_tokens: u64,
        cache_read_tokens: u64,
        cache_write_tokens: u64,
        stop: String,
    }

    struct PolicyDecision {
        disposition: Disposition,
        /// Effective risk class: the tool's floor raised by what this specific
        /// call would do. One of R0, R1, R2, R3.
        risk: String,
        /// Shown to the user on Ask, logged on Deny. Empty when allowed.
        reason: String,
    }

    extern "Rust" {
        type Kernel;

        /// Build a kernel from the compiled-in contract.
        ///
        /// Never fails in a way the caller must handle at the call site, because
        /// this crosses into a no-exceptions C++ codebase. A kernel that failed
        /// to load reports `is_valid() == false` and denies everything, which is
        /// the only safe reading of "I do not know what the rules are".
        fn load_kernel() -> Box<Kernel>;

        fn is_valid(self: &Kernel) -> bool;
        fn last_error(self: &Kernel) -> String;
        fn contract_version(self: &Kernel) -> String;
        fn tool_count(self: &Kernel) -> usize;

        /// The tool list, formatted for the model's system prompt. Generated
        /// from the contract so the prompt cannot drift from what is enforced.
        fn prompt_listing(self: &Kernel) -> String;

        fn decide(self: &Kernel, request: &PolicyRequest) -> PolicyDecision;

        /// Wrap a bare argument in the property its tool expects. See
        /// Contract::normalize_arguments.
        fn normalize_arguments(
            self: &Kernel,
            tool: &str,
            arguments_json: &str,
        ) -> String;

        /// Recover a tool call from whatever the model emitted.
        ///
        /// A Kernel method, because reading a reply reliably needs to know which
        /// tool names are real. It was a free function on the reasoning that it
        /// "needs no contract" -- and without one it had to guess at the shape
        /// of a name, which threw away `browser.navigate https://...` for want
        /// of a bracket. A kernel that failed to load has no tools, so nothing
        /// matches and the caller is told no call was found, which is true.
        fn extract_call(self: &Kernel, response: &str) -> ExtractedCall;

        /// Build a cloud provider request for one step. `kind` is "anthropic",
        /// "openai" or "gemini".
        fn build_provider_request(
            self: &Kernel,
            kind: &str,
            model: &str,
            system: &str,
            user: &str,
            image_jpeg_base64: &str,
            max_tokens: u32,
            force_tool: bool,
            effort: &str,
        ) -> ProviderRequest;

        /// Read a cloud provider's HTTP reply.
        fn parse_provider_reply(
            self: &Kernel,
            kind: &str,
            status: u16,
            body: &str,
        ) -> ProviderReply;
    }
}

/// The kernel, or a record of why there isn't one.
pub struct Kernel {
    contract: Option<Contract>,
    /// The contract's tools as providers see them. Present exactly when
    /// `contract` is: a contract whose names cannot be put on the wire is a
    /// contract the kernel cannot use.
    tools: Option<providers::ToolTable>,
    error: String,
}

pub fn load_kernel() -> Box<Kernel> {
    let loaded = Contract::parse(CONTRACT_JSON).and_then(|contract| {
        providers::ToolTable::from_contract(CONTRACT_JSON).map(|tools| (contract, tools))
    });
    Box::new(match loaded {
        Ok((contract, tools)) => Kernel {
            contract: Some(contract),
            tools: Some(tools),
            error: String::new(),
        },
        Err(problem) => Kernel {
            contract: None,
            tools: None,
            error: problem,
        },
    })
}

impl Kernel {
    fn is_valid(&self) -> bool {
        self.contract.is_some()
    }

    fn last_error(&self) -> String {
        self.error.clone()
    }

    fn contract_version(&self) -> String {
        self.contract
            .as_ref()
            .map(|c| c.version.clone())
            .unwrap_or_default()
    }

    fn tool_count(&self) -> usize {
        self.contract.as_ref().map_or(0, Contract::tool_count)
    }

    fn prompt_listing(&self) -> String {
        self.contract
            .as_ref()
            .map(Contract::prompt_listing)
            .unwrap_or_default()
    }

    fn normalize_arguments(&self, tool: &str, arguments_json: &str) -> String {
        // No contract means no opinion about what the argument should be called,
        // so hand it back untouched and let the shape check speak.
        match &self.contract {
            Some(contract) => contract.normalize_arguments(tool, arguments_json),
            None => arguments_json.to_string(),
        }
    }

    fn decide(&self, request: &ffi::PolicyRequest) -> ffi::PolicyDecision {
        // No contract means no rules, and no rules means no.
        let Some(contract) = self.contract.as_ref() else {
            return refuse(format!("the agent is not available ({})", self.error));
        };

        // The model's arguments arrive as text and are parsed here. A parse
        // failure is a denial rather than an error return: it is a real answer
        // to the question being asked, and it keeps the FFI free of Result,
        // which cxx maps to exceptions that this codebase does not build with.
        let arguments = match serde_json::from_str(&request.arguments_json) {
            Ok(value) => value,
            Err(problem) => {
                return refuse(format!("the agent proposed something unreadable ({problem})"));
            }
        };

        let elements: Vec<Element> = request
            .elements
            .iter()
            .map(|e| Element {
                id: e.id.clone(),
                role: e.role.clone(),
                name: e.name.clone(),
                sensitivity: e.sensitivity.clone(),
            })
            .collect();

        let decision = policy::decide(
            contract,
            &Request {
                tool: &request.tool,
                arguments: &arguments,
                task: &request.task,
                page_url: &request.url,
                elements: &elements,
            },
        );

        ffi::PolicyDecision {
            disposition: match decision.disposition {
                Verdict::Allow => ffi::Disposition::Allow,
                Verdict::Ask => ffi::Disposition::Ask,
                Verdict::Deny => ffi::Disposition::Deny,
            },
            risk: decision.risk.as_str().to_string(),
            reason: decision.reason,
        }
    }
}

fn refuse(reason: String) -> ffi::PolicyDecision {
    ffi::PolicyDecision {
        disposition: ffi::Disposition::Deny,
        risk: contract::Risk::R3.as_str().to_string(),
        reason,
    }
}

/// See `extraction::extract_call`. This is the cxx-facing shape of it: cxx has
/// no Option, so absence is a flag rather than a missing value.
impl Kernel {
    pub fn extract_call(&self, response: &str) -> ffi::ExtractedCall {
        extract_call_with(self.contract.as_ref(), response)
    }
}

fn extract_call_with(contract: Option<&Contract>, response: &str) -> ffi::ExtractedCall {
    let known: Vec<String> = contract.map(Contract::tool_names).unwrap_or_default();
    match extraction::extract_call(response, &known) {
        Some(call) => ffi::ExtractedCall {
            found: true,
            // A model that has been calling tools natively writes the wire name
            // (`page_click`) when it falls back to prose. MEASURED: twice in one
            // run it did, and was told "page_click is not a tool in this
            // browser" for a tool that is. Map it back to the contract's name.
            tool: canonical_name(&known, call.name),
            arguments_json: call.arguments_json,
        },
        None => ffi::ExtractedCall {
            found: false,
            tool: String::new(),
            arguments_json: String::from("{}"),
        },
    }
}

/// `page_click` -> `page.click`, when that is what the contract has.
fn canonical_name(known: &[String], name: String) -> String {
    if known.iter().any(|k| *k == name) {
        return name;
    }
    if let Some((head, tail)) = name.split_once('_') {
        let dotted = format!("{head}.{tail}");
        if known.iter().any(|k| *k == dotted) {
            return dotted;
        }
    }
    name
}

/// See `providers`. The cxx-facing shape: no Option, no Result, so absence and
/// failure are fields.
impl Kernel {
    pub fn build_provider_request(
        &self,
        kind: &str,
        model: &str,
        system: &str,
        user: &str,
        image_jpeg_base64: &str,
        max_tokens: u32,
        force_tool: bool,
        effort: &str,
    ) -> ffi::ProviderRequest {
        let failed = |error: String| ffi::ProviderRequest {
            error,
            path: String::new(),
            headers: Vec::new(),
            body: String::new(),
        };
        let Some(tools) = self.tools.as_ref() else {
            return failed(format!("the agent is not available ({})", self.error));
        };
        let Some(kind) = providers::Kind::parse(kind) else {
            return failed(format!("`{kind}` is not a model provider this browser speaks"));
        };
        let turn = providers::Turn {
            kind,
            model,
            system,
            user,
            max_tokens,
            image_jpeg_base64,
            force_tool,
            effort,
        };
        match providers::build(tools, &turn) {
            Ok(request) => ffi::ProviderRequest {
                error: String::new(),
                path: request.path,
                headers: request
                    .headers
                    .into_iter()
                    .map(|(name, value)| ffi::ProviderHeader { name, value })
                    .collect(),
                body: request.body,
            },
            Err(problem) => failed(problem),
        }
    }

    pub fn parse_provider_reply(&self, kind: &str, status: u16, body: &str) -> ffi::ProviderReply {
        let mut out = ffi::ProviderReply {
            error: String::new(),
            found: false,
            tool: String::new(),
            arguments_json: String::from("{}"),
            more_tools: Vec::new(),
            more_arguments_json: Vec::new(),
            text: String::new(),
            input_tokens: 0,
            output_tokens: 0,
            cache_read_tokens: 0,
            cache_write_tokens: 0,
            stop: String::new(),
        };
        let Some(tools) = self.tools.as_ref() else {
            out.error = format!("the agent is not available ({})", self.error);
            return out;
        };
        let Some(kind) = providers::Kind::parse(kind) else {
            out.error = format!("`{kind}` is not a model provider this browser speaks");
            return out;
        };
        let reply = providers::parse(tools, kind, status, body);
        out.error = reply.error.unwrap_or_default();
        if let Some((tool, arguments_json)) = reply.tool {
            out.found = true;
            out.tool = tool;
            out.arguments_json = arguments_json;
        }
        for (tool, arguments_json) in reply.more {
            out.more_tools.push(tool);
            out.more_arguments_json.push(arguments_json);
        }
        out.text = reply.text;
        out.input_tokens = reply.usage.input;
        out.output_tokens = reply.usage.output;
        out.cache_read_tokens = reply.usage.cache_read;
        out.cache_write_tokens = reply.usage.cache_write;
        out.stop = reply.stop;
        out
    }
}
