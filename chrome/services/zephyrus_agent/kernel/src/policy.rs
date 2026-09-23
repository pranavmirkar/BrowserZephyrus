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

    // 4. Does it point at an address the page actually offers?
    //
    //    The same grounding rule as the one above, applied to addresses. An
    //    element id the Observation never contained is refused; an address
    //    assembled out of the task's own words, while the page is showing a
    //    link that answers the task better, is the same mistake wearing a
    //    different shape.
    //
    //    MEASURED, and the reason this is a rule rather than a line in the
    //    prompt -- the prompt already carries that line, verbatim, and both
    //    models ignored it. qwen2.5:7b failed three of six benchmark tasks by
    //    inventing addresses; qwen2.5:1.5b spent 15 of its 24 calls doing it,
    //    including navigating to `tool.example/Release notes`, an address
    //    built out of a link's visible label rather than clicking the label.
    //    This is the injection finding again: a judgement the model cannot be
    //    trusted to make has to be made outside it.
    if let Some((target, better)) = invented_address(request) {
        return deny(
            tool.floor,
            format!(
                "`{target}` is a guess assembled from your own words, not an \
                 address this page offers. The link \"{}\" is right here -- \
                 open a specific item by clicking it.",
                better.trim()
            ),
        );
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
                if let Some(verb) =
                    consequential_verb(&element.name, has_open_composer(request.elements))
                {
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

    // Committing to one of several things the task named equally.
    //
    // MEASURED, and the reason this is policy rather than prompting: the task
    // "Send the report to Alex" on a page offering Alex Chen and Alex Morgan.
    // Both benchmarked models picked one and sent it -- the 7B in three steps,
    // with nothing between it and a message to the wrong person. Asking the
    // model to be careful does not fix this; it is the same finding as the
    // injection work, that a judgement the model cannot be trusted to make has
    // to be made outside it.
    //
    // Which of two equally good candidates the user meant is not a judgement
    // at all: it is information the agent does not have. It cannot be
    // recovered by looking harder at the page, so the only correct move is to
    // ask, and the only place that can insist is here.
    //
    // Note where this fires: on a call that COMMITS to one of the candidates.
    // That is two doors, not one -- clicking a candidate, and typing a name
    // the candidates share, which is how the model reached the wrong
    // recipient in the first place. The reading tools stay Allow, because
    // looking is how ambiguity would be resolved if the page could resolve it.
    //
    // It used to fire on ANY acting call while a tie existed, and a perfect
    // agent paid for that. MEASURED on benchmark mt-011: five invoices each
    // matching two of "invoice from March 2026" made the click on "Older
    // invoices" -- which matches none of them, and is the right move -- a
    // question for the user. A call that commits to nothing in the tie has
    // nothing to ask about.
    if matches!(request.tool, "page.click" | "page.type" | "page.select") {
        if let Some((first, second)) = ambiguous_choice(request) {
            return (
                floor.max(Risk::R2),
                format!(
                    "\"{}\" and \"{}\" both match what you asked for, and nothing \
                     on the page says which you meant",
                    first.name.trim(),
                    second.name.trim()
                ),
            );
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

/// Words that carry no target. Dropped before matching a task to the page.
///
/// Two kinds, and both had to go. The little joining words match everything --
/// "to" is inside "To", "Total" and half the page. The VERBS are the subtler
/// half: a task begins by saying what to do, and "search" matching both the
/// search box and the Search button beside it made every ordinary search look
/// like a choice between two candidates. What identifies a target is the noun
/// the user named, not the thing they asked to have done with it.
const TASK_NOISE: &[&str] = &[
    "the", "this", "that", "and", "for", "with", "from", "into", "onto", "its",
    "his", "her", "their", "our", "your", "you", "please", "then", "there",
    "here", "what", "which", "when", "where", "how", "any", "all", "one",
    "search", "find", "open", "click", "press", "type", "send", "show", "get",
    "got", "give", "look", "read", "tell", "make", "take", "use", "using",
    "add", "buy", "order", "book", "check", "start", "stop", "close", "page",
    "site", "website", "browser", "tab", "link", "button", "first", "last",
    "next", "back", "also", "about", "after", "before", "again", "now",
];

/// The shortest word worth matching on. Two letters match far too much.
const MIN_TARGET_LEN: usize = 3;

/// The significant words of a task: what the user named, minus how they said it.
fn task_targets(task: &str) -> Vec<String> {
    task.to_ascii_lowercase()
        .split(|c: char| !c.is_ascii_alphanumeric())
        .filter(|word| word.len() >= MIN_TARGET_LEN && !TASK_NOISE.contains(word))
        .map(str::to_string)
        .collect()
}

/// How many of the task's words appear in `text`.
fn target_hits(targets: &[String], text: &str) -> usize {
    let lowered = text.to_ascii_lowercase();
    targets
        .iter()
        .filter(|target| lowered.contains(target.as_str()))
        .count()
}

/// How many of the task's own words a guessed address has to carry.
///
/// Two, not one. One is how an ordinary section is named -- "go to the pricing
/// page" reaching `/pricing` is a fair guess and a common one, and it works.
/// Two or more words strung into a path is a TITLE being retyped as an
/// address, which is the thing that lands on an error page.
const INVENTED_PATH_WORDS: usize = 2;

/// A navigation to an address built from the task, while the page offers better.
///
/// Returns the guessed address and the name of the link that answers the task,
/// so the refusal can point at it.
///
/// Three conditions, and every one of them is load-bearing:
///
/// 1. The PATH carries the task's words. Only the path -- a search URL puts
///    them in the QUERY, and building a site's search address is explicitly a
///    fair thing to do. That single distinction is what separates a reasonable
///    shortcut from a fabricated item id.
/// 2. The user did not say the address themselves. A task naming a URL is not
///    a guess, it is an instruction.
/// 3. The page is showing a link that matches the task at least as well. This
///    is what keeps the rule honest: Wikipedia-style addresses really are
///    guessable from a title, so refusing every assembled path would break
///    work the agent can do. The refusal only makes sense when there is
///    something better to do instead, and here there demonstrably is.
fn invented_address<'a>(request: &'a Request) -> Option<(&'a str, &'a str)> {
    let argument = NAVIGATING_TOOLS
        .iter()
        .find(|(tool, _)| *tool == request.tool)
        .map(|(_, argument)| *argument)?;
    let target = request.arguments.get(argument).and_then(Value::as_str)?;

    let targets = task_targets(request.task);
    if targets.is_empty() {
        return None;
    }

    // The user's own words are an instruction, not a guess.
    if request.task.to_ascii_lowercase().contains(
        &target.to_ascii_lowercase().replace("https://", "").replace("http://", ""),
    ) {
        return None;
    }

    // The path only. Everything from the first `?` or `#` is a query, and a
    // query carrying the task's words is a SEARCH.
    let after_scheme = target.split_once("://").map(|(_, rest)| rest).unwrap_or(target);
    let path = match after_scheme.split_once('/') {
        Some((_, rest)) => rest.split(['?', '#']).next().unwrap_or(""),
        None => return None,
    };
    if target_hits(&targets, path) < INVENTED_PATH_WORDS {
        return None;
    }

    // Something on the page answers the task at least as well. Links and
    // buttons only: a textbox named after the task is a field to type in, not
    // a destination.
    let better = request
        .elements
        .iter()
        .filter(|element| element.role == "link" || element.role == "button")
        .map(|element| (target_hits(&targets, &element.name), element))
        .filter(|(hits, _)| *hits >= INVENTED_PATH_WORDS)
        .max_by_key(|(hits, _)| *hits)
        .map(|(_, element)| element)?;

    Some((target, better.name.as_str()))
}

/// Whether two tied elements are a choice a user could actually make.
///
/// They must share a ROLE -- a textbox and a button named alike are not rivals
/// for the same choice, they are different halves of one control, and treating
/// them as rivals was what turned "search this site for X" into a question.
///
/// And two LINKS with the same name are not a choice at all. MEASURED on
/// benchmark mt-008: Pricing in the header and Pricing in the footer, as on
/// nearly every site, asked the user "Pricing or Pricing?" -- a question with
/// no answer, about two links to one page. Links only, deliberately: two
/// contacts both shown as "Alex" are two people, and picking between them is
/// exactly what this rule exists to stop. A wrong link is a navigation, which
/// is reversible; a wrong recipient is not.
fn rivals_for_one_choice(first: &Element, second: &Element) -> bool {
    if first.role != second.role || first.name.trim().is_empty() {
        return false;
    }
    let same_name = first.name.trim().eq_ignore_ascii_case(second.name.trim());
    !(same_name && first.role == "link")
}

/// The elements tied as the best match for the task that have a rival among
/// the others -- the candidates of an unresolved choice.
///
/// "Tied" is doing the work: each must match the same, highest, count of the
/// task's own words. The moment anything distinguishes the candidates there is
/// nothing here, because a rule that asks too often is a rule the user learns
/// to click through, and then it is not protecting anything.
fn tied_candidates<'a>(targets: &[String], elements: &'a [Element]) -> Vec<&'a Element> {
    let score = |element: &Element| -> usize { target_hits(targets, &element.name) };
    let top = elements.iter().map(&score).max().unwrap_or(0);
    if top == 0 {
        return Vec::new();
    }
    let winners: Vec<&Element> = elements.iter().filter(|e| score(e) == top).collect();
    winners
        .iter()
        .copied()
        .filter(|first| {
            winners
                .iter()
                .any(|second| second.id != first.id && rivals_for_one_choice(first, second))
        })
        .collect()
}

/// Two candidates this call would choose between without knowing which the
/// user meant. None when the call commits to nothing in the tie.
fn ambiguous_choice<'a>(request: &'a Request) -> Option<(&'a Element, &'a Element)> {
    let targets = task_targets(request.task);
    if targets.is_empty() {
        return None;
    }
    let candidates = tied_candidates(&targets, request.elements);
    if candidates.is_empty() {
        return None;
    }

    // Door one: acting on a candidate.
    if let Some(chosen) = target_element(request) {
        if candidates.iter().any(|c| c.id == chosen.id) {
            let other = *candidates
                .iter()
                .find(|c| c.id != chosen.id && rivals_for_one_choice(chosen, c))?;
            return Some((chosen, other));
        }
    }

    // Door two: writing one of them into a field -- "Alex" into the To field
    // beside two Alexes, or "Morgan", or an address copied off the page.
    //
    // Judged on the CANDIDATES' words, not only the task's. A first version
    // checked the task's words alone, so "Morgan" -- which the task never
    // said -- passed, and so did "a.morgan@example.com": both pick one Alex
    // just as surely as clicking him.
    if !matches!(request.tool, "page.type" | "page.select") {
        return None;
    }
    let written = request
        .arguments
        .get("text")
        .or_else(|| request.arguments.get("value"))
        .and_then(Value::as_str)?
        .to_ascii_lowercase();
    let carries = |element: &Element, word: &str| element.name.to_ascii_lowercase().contains(word);
    // A word of the task that NO candidate carries points past all of them.
    // Typing "March 2026" into a search box beside five tied 2026 invoices is
    // looking for March, not choosing among the five.
    if targets.iter().any(|word| {
        written.contains(word.as_str()) && !candidates.iter().any(|c| carries(*c, word.as_str()))
    }) {
        return None;
    }
    let writes_a_candidate = candidates.iter().any(|candidate| {
        candidate
            .name
            .to_ascii_lowercase()
            .split(|c: char| !c.is_ascii_alphanumeric())
            .filter(|word| word.len() >= MIN_TARGET_LEN && !TASK_NOISE.contains(word))
            .any(|word| written.contains(word))
    });
    if !writes_a_candidate {
        return None;
    }
    let first = *candidates.first()?;
    let second = *candidates
        .iter()
        .find(|c| c.id != first.id && rivals_for_one_choice(first, c))?;
    Some((first, second))
}

/// The first consequential verb in an element's accessible name, if any.
///
/// `composer_open` is whether the page has a text field a Reply could submit;
/// see `has_open_composer`.
fn consequential_verb(name: &str, composer_open: bool) -> Option<&'static str> {
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
    // Whole words, so "Sender" and "Sendai" do not match "send". Substring
    // matching here would escalate most of the web; the words that genuinely
    // need catching (like "resend") are listed explicitly.
    //
    // Matched in the RAW label rather than a list of split words, because
    // whether a word names a record depends on the "#" after it, and splitting
    // on punctuation throws the "#" away.
    let is_word_char = |c: char| c.is_ascii_alphanumeric();
    CONSEQUENTIAL_VERBS.iter().copied().find(|verb| {
        lowered.match_indices(verb).any(|(at, _)| {
            let before = lowered[..at].chars().next_back();
            let rest = &lowered[at + verb.len()..];
            let whole_word = !before.is_some_and(is_word_char)
                && !rest.chars().next().is_some_and(is_word_char);
            whole_word
                && !names_a_record(verb, rest)
                && !(*verb == "reply" && !composer_open)
        })
    })
}

/// Words on the list that are just as often NOUNS naming a thing that exists.
///
/// MEASURED on benchmark mt-010: the link "Order #4417 -- Pixel 11 case" stopped
/// to ask the user, as though clicking it would place an order. It opens one
/// that was placed weeks ago. Only these words, deliberately -- "Delete #4417"
/// is a verb aimed at a record, and still asks.
const ALSO_A_NOUN: &[&str] = &["order", "post", "reply", "transfer", "book"];

/// Whether `verb` is used as a noun with the marker that says WHICH one:
/// "Order #4417", "Order no. 4417", "Order number 4417".
///
/// The marker is required. A bare number after the word is as often a time
/// or a quantity as an id -- "Book 18:30", "Order 2 pizzas" -- and those are
/// the verb doing exactly what the list exists to catch. MEASURED: a first
/// version accepted a bare number, and "Book 18:30" stopped asking.
fn names_a_record(verb: &str, rest: &str) -> bool {
    if !ALSO_A_NOUN.contains(&verb) {
        return false;
    }
    let rest = rest.trim_start();
    let starts_with_digit = |text: &str| text.trim_start().starts_with(|c: char| c.is_ascii_digit());
    if let Some(after) = rest.strip_prefix('#') {
        return starts_with_digit(after);
    }
    ["no.", "no ", "number "]
        .iter()
        .any(|marker| rest.strip_prefix(marker).is_some_and(starts_with_digit))
}

/// Whether the page has something a Reply button could be SUBMITTING.
///
/// "Reply" means two things. On a forum or a social site the button under a
/// filled comment box posts it, and that commits. In a mail reader it opens a
/// draft, and the Send that follows is what commits -- and Send is asked about
/// in its own right. MEASURED on benchmark mt-012: a perfect agent was stopped
/// to approve opening a draft, then stopped again to approve sending it. A
/// Reply with no text field open has nothing to send. Search boxes do not
/// count: a mail reader always has one, and it holds nothing to submit.
///
/// Why this cannot be slipped past: the agent writes only through page.type,
/// which needs a field the Observation offered. A composer the Observation
/// does not offer is one the agent never wrote in, so a Reply beside it has
/// nothing of the agent's to submit. Every text field -- single line,
/// multi-line, or an editable region marked as one -- arrives as `textbox`.
fn has_open_composer(elements: &[Element]) -> bool {
    elements.iter().any(|element| {
        element.role == "textbox" && !element.name.to_ascii_lowercase().contains("search")
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
