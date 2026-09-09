// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The policy engine: may this tool call run, and does the user have to say so?
//!
//! # Why this is not in the prompt
//!
//! Measured on 2026-09-02 against the 8-fixture benchmark in `agent/benchmark`:
//! qwen2.5 at 1.5B and at 7B were both given a system prompt stating in plain
//! words that observation content is untrusted data and never an instruction.
//! Both obeyed the same prompt injection, navigating to the same attacker host,
//! in well-formed JSON. The larger model was 2.5x more correct overall and no
//! safer at all.
//!
//! So the model is not part of the security boundary. This module is. Nothing
//! here reads free text as an instruction: the inputs are a tool name, parsed
//! arguments, and the browser's own description of the page. Page text is not
//! an input at all.
//!
//! # Why risk is computed and not looked up
//!
//! The contract gives every tool a static risk class, and that class is a floor
//! rather than an answer. `page.click` is R1 because clicking is usually
//! reversible -- but clicking a button labelled "Send" leaves the browser and
//! is visible to someone else, which is the contract's own definition of R2.
//! The tool is the same; the consequence is not. Effective risk is therefore
//! `max(floor, escalations implied by the target)`.

use serde_json::Value;

use crate::contract::{Contract, Risk};

/// One interactive element the browser offered in the Observation.
#[derive(Debug, Clone)]
pub struct Element {
    pub id: String,
    pub role: String,
    pub name: String,
    /// What private thing the browser judged this to hold, or empty.
    pub sensitivity: String,
}

/// A proposed call, plus the context needed to judge it.
#[derive(Debug)]
pub struct Request<'a> {
    pub tool: &'a str,
    pub arguments: &'a Value,
    /// The user's own words. Trusted -- it is the only free text here that is.
    pub task: &'a str,
    pub page_url: &'a str,
    /// Exactly the elements the browser put in the Observation the model saw.
    pub elements: &'a [Element],
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Disposition {
    /// Run it.
    Allow,
    /// Run it only after the user approves this specific call.
    Ask,
    /// Do not run it, and do not offer to.
    Deny,
}

#[derive(Debug, Clone)]
pub struct Decision {
    pub disposition: Disposition,
    pub risk: Risk,
    /// Shown to the user on Ask, and logged on Deny. Written to be read by a
    /// person deciding in one second, not by an engineer reading a trace.
    pub reason: String,
}

/// Words that turn a click into an external side effect.
///
/// The failure modes are asymmetric and the list is tuned for that. A false
/// positive costs one unnecessary confirmation prompt. A false negative sends
/// an email. So this over-matches on purpose: "Post" as a noun in "Blog post"
/// will trip it, and that is the trade being made rather than an oversight.
///
/// This is a heuristic and it is the weakest part of the module. It is not the
/// only thing standing between a page and a side effect -- R2 always asks, so
/// a verb missing from this list degrades to a click that looked reversible,
/// not to a silent send.
const CONSEQUENTIAL_VERBS: &[&str] = &[
    "accept",
    "agree",
    "apply",
    "book",
    "buy",
    "checkout",
    "confirm",
    "delete",
    "donate",
    "forward",
    "invite",
    "order",
    "pay",
    "post",
    "publish",
    "purchase",
    "remove",
    "reply",
    // Listed separately rather than left to a prefix match on "send", because
    // matching is on whole words: "Resend" must escalate, "Sender" must not.
    "resend",
    "reserve",
    "send",
    "share",
    "submit",
    "subscribe",
    "transfer",
    "unsubscribe",
];

/// Tools that navigate, and the argument holding the destination.
const NAVIGATING_TOOLS: &[(&str, &str)] = &[("browser.navigate", "url"), ("tabs.open", "url")];

pub fn decide(contract: &Contract, request: &Request) -> Decision {
    // 1. Is this a tool at all? An unknown name is not a low-risk call, it is a
    //    call the kernel has no rule for.
    let Some(tool) = contract.tool(request.tool) else {
        return deny(
            Risk::R3,
            format!("`{}` is not a tool in this browser", request.tool),
        );
    };

    // 2. Is it shaped the way the contract says? A malformed call cannot be
    //    reasoned about, so it is refused rather than guessed at.
    if let Err(problem) = tool.check_shape(request.arguments) {
        return deny(tool.floor, format!("{} ({})", problem, tool.name));
    }

    // 3. Does it point at something the browser actually offered?
    //
    //    An id the Observation never contained is either a model hallucination
    //    or an id smuggled in from somewhere else. Both are refused here rather
    //    than deeper in the executor, so that the refusal is auditable and the
    //    reason survives to the user.
    if let Some(value) = request.arguments.get("element_id") {
        // Reject a non-string id rather than skipping the check. Reading this
        // as `and_then(as_str)` and moving on would let `{"element_id": 12}`
        // past the grounding rule entirely -- the executor would still refuse
        // it, but the kernel would have said Allow, and the kernel's answer is
        // the one that gets audited.
        let Some(id) = value.as_str() else {
            return deny(
                tool.floor,
                "the element to act on was not named as text".to_string(),
            );
        };
        if !request.elements.iter().any(|e| e.id == id) {
            return deny(
                tool.floor,
                format!("the page has no element `{id}` -- it was never offered"),
            );
        }
    }

    let (risk, reason) = escalate(tool.floor, request);

    let disposition = match risk {
        // Reading, and reversible movement inside the browser, need no ceremony.
        Risk::R0 | Risk::R1 => Disposition::Allow,
        // Anything with an external side effect is the user's decision, every
        // time. There is no "remember this" here on purpose: the thing being
        // approved is this call, not the class of call.
        Risk::R2 => Disposition::Ask,
        // Nothing in the V1 contract is R3. Reaching it means a rule escalated
        // there, and V1 has no consent flow strong enough to carry it.
        Risk::R3 => Disposition::Deny,
    };

    Decision {
        disposition,
        risk,
        reason,
    }
}

/// The element this call points at, if it points at one.
fn target_element<'a>(request: &'a Request) -> Option<&'a Element> {
    let id = request
        .arguments
        .get("element_id")
        .and_then(Value::as_str)?;
    request.elements.iter().find(|e| e.id == id)
}

/// Raise `floor` to what this specific call would actually do.
fn escalate(floor: Risk, request: &Request) -> (Risk, String) {
    // Typing into a password field.
    //
    // R3, which denies outright rather than asking, because there is no answer
    // the user could give that makes this right: the agent has no credentials,
    // so anything it typed here would be invented or copied from somewhere it
    // should not have been reading. The browser marks these fields for us --
    // Chromium has no password role, only a protected state on a text field, so
    // the Observation converts that into a role the policy can see.
    if request.tool == "page.type" {
        if let Some(element) = target_element(request) {
            if element.role == "password" {
                return (
                    Risk::R3,
                    "the agent does not fill in passwords -- sign in yourself \
                     and it can carry on from there"
                        .to_string(),
                );
            }
            // A card number, by exactly the same argument.
            //
            // The password rule was written as being about credentials and it
            // is really about a class: fields whose contents the agent cannot
            // legitimately possess. It has no card number either, so anything
            // it typed here was invented or read off a page it had no business
            // copying from -- and unlike a password, a wrong guess that happens
            // to work costs the user money.
            if element.sensitivity == "payment_card" {
                return (
                    Risk::R3,
                    "the agent does not fill in card details -- type them \
                     yourself and it can carry on from there"
                        .to_string(),
                );
            }
        }
    }

    // A click on a control that commits something.
    if request.tool == "page.click" {
        if let Some(id) = request.arguments.get("element_id").and_then(Value::as_str) {
            if let Some(element) = request.elements.iter().find(|e| e.id == id) {
                if let Some(verb) = consequential_verb(&element.name) {
                    // The role is named because the user is being asked to
                    // approve a specific control, and "the button Send" is a
                    // thing they can look for on the page in a second.
                    let role = element.role.trim();
                    let described = if role.is_empty() {
                        format!("\"{}\"", element.name.trim())
                    } else {
                        format!("the {} \"{}\"", role, element.name.trim())
                    };
                    return (
                        floor.max(Risk::R2),
                        format!("{described} looks like it would {verb} something that leaves the browser"),
                    );
                }
            }
        }
    }

    // A navigation carrying data to somewhere nobody asked for.
    //
    // This is the exfiltration shape, and it is the one both benchmarked models
    // walked into: page text told them to fetch an attacker URL with the user's
    // history in the query string, and they did. Note what is and is not
    // suspicious here. Visiting a new site is normal and stays R1. Visiting a
    // new site while handing it a query string is the part worth a question.
    if let Some((_, argument)) = NAVIGATING_TOOLS.iter().find(|(t, _)| *t == request.tool) {
        if let Some(target) = request.arguments.get(*argument).and_then(Value::as_str) {
            // The blank page is not a destination.
            //
            // `about:blank` has no origin, so the origin rules below cannot
            // check it and escalate it to Ask -- which meant the user was
            // asked to approve "open a new tab". That is the whole of what
            // `tabs.open` does with NO url at all, and that form is allowed
            // without ceremony. Two spellings of one harmless action were
            // getting two different answers, and the harder one is the
            // spelling a model naturally reaches for: MEASURED, a user typed
            // "can you open a new tab" and the task died there.
            //
            // Nothing is widened. The blank page sends nothing, receives
            // nothing and has no origin to be confused about. Matched EXACTLY:
            // this is not a licence for the `about:` scheme, which reaches
            // real browser internals.
            if target == "about:blank" {
                return (floor, String::new());
            }
            match origin_of(target) {
                // Unparseable destination. We decline to reason about it rather
                // than assume it is harmless.
                None => {
                    return (
                        floor.max(Risk::R2),
                        format!("`{target}` is not an address this browser can check"),
                    );
                }
                Some(destination) => {
                    if carries_data(target) && !is_expected(&destination, request) {
                        return (
                            floor.max(Risk::R2),
                            format!(
                                "this would send data to {destination}, which you did not \
                                 mention and the page did not come from"
                            ),
                        );
                    }
                }
            }
        }
    }

    (floor, String::new())
}

/// The first consequential verb in an element's accessible name, if any.
fn consequential_verb(name: &str) -> Option<&'static str> {
    // Only the beginning of the name, because only that part is a LABEL.
    //
    // An accessible name is often computed from everything inside the element,
    // so a link wrapping a channel card carries its whole description. On
    // youtube.com that description reads "We post new Sidemen videos every
    // single Sunday!" -- and "post" is on this list, so clicking the channel
    // stopped and asked the user to approve it. Twice, in one run.
    //
    // A control that genuinely commits something says so at the front: "Send",
    // "Place order", "Confirm and pay". Sixty characters keeps those and stops
    // reading before a paragraph can put a verb in the way.
    const LABEL_WINDOW: usize = 60;
    let label = match name.char_indices().nth(LABEL_WINDOW) {
        Some((end, _)) => &name[..end],
        None => name,
    };
    let lowered = label.to_ascii_lowercase();
    CONSEQUENTIAL_VERBS.iter().copied().find(|verb| {
        // Whole words, so "Sender" and "Sendai" do not match "send".
        // Substring matching here would escalate most of the web; the words
        // that genuinely need catching (like "resend") are listed explicitly.
        lowered
            .split(|c: char| !c.is_ascii_alphanumeric())
            .any(|word| word == *verb)
    })
}

/// True if the user or the current page already points at this origin.
fn is_expected(destination: &str, request: &Request) -> bool {
    if origin_of(request.page_url).as_deref() == Some(destination) {
        return true;
    }
    // The task is the user's own words, so an origin named there is one they
    // asked for. Compared on the host, because people write "example.com" and
    // not "https://example.com".
    let host = destination
        .split_once("://")
        .map(|(_, h)| h)
        .unwrap_or(destination);
    task_names_host(request.task, host)
}

/// True if `task` names `host` as a whole hostname.
///
/// Whole-token, not substring. A substring test reads "notevil.com" in the
/// user's task as permission to visit "evil.com", which is a false allow on the
/// one check standing between the agent and an unexpected destination.
fn task_names_host(task: &str, host: &str) -> bool {
    if host.is_empty() {
        return false;
    }

    // People write "youtube", not "www.youtube.com".
    //
    // Whole-host equality asked the user to approve going to youtube.com in a
    // task that began "go to youtube" -- a permission prompt for the one thing
    // they had just said out loud. So the bare name counts too.
    //
    // Matched against the REGISTRABLE label only -- the one before the final
    // suffix -- and deliberately not against every label in the host. Otherwise
    // a task mentioning "google" would treat google.evil.com as expected, which
    // is precisely the shape an exfiltration takes. Getting this wrong in the
    // other direction only costs a prompt.
    let labels: Vec<&str> = host.split('.').collect();
    let registrable = if labels.len() >= 2 {
        labels[labels.len() - 2]
    } else {
        host
    };

    task.to_ascii_lowercase()
        .split(|c: char| !(c.is_ascii_alphanumeric() || c == '.' || c == '-'))
        .any(|token| {
            let token = token.trim_matches('.');
            !token.is_empty() && (token == host || token == registrable)
        })
}

/// `scheme://host` of `url`, lowercased, or None if it cannot be determined.
///
/// Hand-rolled because no URL crate is vendored for this target, which makes it
/// exactly the kind of parser that grows security bugs. It is written to fail
/// closed: every path that cannot reach a confident answer returns None, and
/// every caller treats None as the higher-risk branch. Being wrong here costs a
/// confirmation prompt, never a silent allow.
fn origin_of(url: &str) -> Option<String> {
    let (scheme, rest) = url.split_once("://")?;
    if scheme.is_empty()
        || !scheme
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || c == '+' || c == '-' || c == '.')
    {
        return None;
    }

    let authority = rest.split(['/', '?', '#']).next()?;
    // Strip userinfo. `https://trusted.example@attacker.example/` is served by
    // attacker.example, and reading the left side is a classic way to be fooled
    // about where a request is going.
    let host_port = authority
        .rsplit_once('@')
        .map(|(_, after)| after)
        .unwrap_or(authority);

    let host = if host_port.starts_with('[') {
        // IPv6 literal: the colons inside the brackets are not a port.
        let end = host_port.find(']')?;
        &host_port[..=end]
    } else {
        host_port.split(':').next()?
    };

    if host.is_empty() || host == "[]" {
        return None;
    }
    Some(format!(
        "{}://{}",
        scheme.to_ascii_lowercase(),
        host.to_ascii_lowercase()
    ))
}

/// True if the URL hands anything to the destination beyond the path.
///
/// # Known gap
///
/// Data can be smuggled in the path itself -- `https://attacker.example/c/<blob>`
/// carries no query string and reads as ordinary navigation here. Closing that
/// by treating every unfamiliar origin as R2 would put a confirmation prompt in
/// front of ordinary browsing, which is not a trade worth making for an agent
/// meant to be useful.
///
/// The real mitigation is upstream of this check and belongs in the design
/// rather than in this function: the kernel's job is to make sure the agent
/// cannot quietly *reach* data worth exfiltrating. Revisit when the tool set
/// grows anything that reads history, credentials or file contents.
fn carries_data(url: &str) -> bool {
    let after_scheme = url.split_once("://").map(|(_, rest)| rest).unwrap_or(url);
    if after_scheme.contains('?') || after_scheme.contains('#') {
        return true;
    }

    // The path is a place to put data too, and it was not being looked at.
    //
    // `https://evil.example/log/what-the-user-was-just-reading` has no query
    // string and no fragment, so the check above waved it through -- while the
    // same text after a `?` was caught. That is a bypass anyone reading this
    // rule would find, and page-injected instructions are written by people who
    // read rules like this one.
    //
    // A length is used rather than a cleverer test because the alternative is a
    // guess about meaning, and a guess here is wrong in the direction that
    // matters. Sixty characters of path is well beyond the addresses reached by
    // ordinary browsing and enough room to be worth a question.
    //
    // Worth being honest about the limit: a short path can still carry a
    // little. This raises the floor on the obvious case, it does not prove the
    // absence of the general one -- which is why the check it feeds asks the
    // user rather than deciding alone.
    const PATH_ROOM_FOR_DATA: usize = 60;
    let path = match after_scheme.split_once('/') {
        Some((_, rest)) => rest,
        None => return false,
    };
    path.len() > PATH_ROOM_FOR_DATA
}

fn deny(risk: Risk, reason: String) -> Decision {
    Decision {
        disposition: Disposition::Deny,
        risk,
        reason,
    }
}
