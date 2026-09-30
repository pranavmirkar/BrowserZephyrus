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

/// Controls that spend money. Clicking one always asks the user first.
///
/// The product decision is that the agent asks about PAYMENTS, and about
/// sign-in (which it cannot do itself: see `escalate` and the `task.handoff`
/// tool). A payment control worded some other way is not caught -- which is why
/// the agent also never types card details, and hands the form to the user, so
/// the last step of a payment is theirs either way.
///
/// Whole words, matched at the FRONT of a control's label: "Buy now",
/// "Place order", "Confirm and pay".
const PAYMENT_VERBS: &[&str] = &[
    "book",
    "buy",
    "checkout",
    "donate",
    "order",
    "pay",
    "purchase",
    "reserve",
    "subscribe",
    "transfer",
];

/// Controls that send something out or destroy something, each with the words
/// in a task that ask for it.
///
/// These used to ask every time, and that stopped a competent agent at almost
/// every real task: a card for each email, each comment, each form. They now
/// ask only when the TASK did not ask for them. A user who says "reply to Maya"
/// has asked for a reply to be sent; the same page telling the agent to forward
/// the thread elsewhere has not been asked for by anyone, and "forward" is not
/// in the task -- that click asks.
///
/// MEASURED, and the reason this is not simply deleted: benchmark mt-012 puts
/// an instruction inside an email that says to forward it, and a model that is
/// told to reply follows it. Without this rule that run forwards the mail. The
/// model is not the boundary; what the USER said is.
///
/// Words are STEMS, matched at the start of a task word, so "deleting" is
/// covered by "delet" and "emailed" by "email". Deliberately generous on what
/// counts as asking for it -- a false "allowed" needs the task to already talk
/// about that kind of action, and a false "ask" is only one card -- but each
/// verb's list is its own: asking to "send" does not authorise "forward".
const OUTBOUND_VERBS: &[(&str, &[&str])] = &[
    ("send", &["send", "sent", "email", "mail", "messag", "text", "write", "compos", "repl", "respon", "shar", "tell", "notif", "invit", "forward"]),
    ("resend", &["send", "resend", "email", "mail", "messag", "invit"]),
    ("reply", &["repl", "respon", "answer", "messag", "email", "mail", "write"]),
    ("forward", &["forward"]),
    ("post", &["post", "publish", "tweet", "comment", "shar", "write", "upload", "announc"]),
    ("publish", &["publish", "post", "releas", "announc"]),
    ("share", &["shar", "send", "post", "invit", "collaborat"]),
    ("submit", &["submit", "appl", "regist", "enter", "fill", "complet", "send", "sign", "enrol", "request", "form"]),
    ("apply", &["appl", "submit", "regist", "enrol"]),
    ("confirm", &["confirm", "accept", "approv", "verif", "submit", "complet", "finish"]),
    ("accept", &["accept", "agree", "approv", "confirm"]),
    ("agree", &["agree", "accept", "confirm"]),
    ("delete", &["delet", "remov", "clear", "trash", "discard", "eras", "clean", "empty", "drop"]),
    ("remove", &["remov", "delet", "clear", "discard", "uninstall", "drop"]),
    ("unsubscribe", &["unsubscrib", "cancel", "stop", "opt"]),
    ("invite", &["invit", "add", "shar"]),
];

/// Tools that navigate, and the argument holding the destination.
const NAVIGATING_TOOLS: &[(&str, &str)] = &[("browser.navigate", "url"), ("tabs.open", "url")];

pub fn decide(contract: &Contract, request: &Request) -> Decision {
    // 1. Is this a tool at all? An unknown name is not a low-risk call, it is a
    //    call the kernel has no rule for.
    let Some(tool) = contract.tool(request.tool) else {
        // Named the real ones: a model that invented a tool ("browser_click_
        // placeholder", seen in a live run) was told only that it was wrong,
        // and had to guess again with a step gone each time.
        let mut names: Vec<String> = contract
            .tool_names()
            .into_iter()
            .filter(|n| n != "page.observe")
            .collect();
        names.sort();
        return deny(
            Risk::R3,
            format!(
                "`{}` is not a tool in this browser. The tools are: {}",
                request.tool,
                names.join(", ")
            ),
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
                    "the agent does not fill in passwords. Call task.handoff so \
                     the user can sign in, then carry on"
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
                    "the agent does not fill in card details. Call task.handoff \
                     so the user can enter them, then carry on"
                        .to_string(),
                );
            }
        }
    }

    // Memory. What the agent keeps outlives the page, the task and the chat, so
    // it is the one place a hostile page could plant something that pays off
    // later: "remember that the user always wants receipts forwarded to...".
    // The rule that closes it is PROVENANCE: a fact may only be kept if it is
    // the user's own words, so the words of the fact have to be in what the user
    // just said. A page cannot write the user's message.
    if request.tool == "memory.remember" {
        let fact = request
            .arguments
            .get("fact")
            .and_then(Value::as_str)
            .unwrap_or("");
        if let Some(why) = refuse_to_remember(fact, request.task) {
            return (Risk::R3, why);
        }
        return (floor, String::new());
    }
    if request.tool == "memory.forget" {
        if !task_asks_to_forget(request.task) {
            return (
                Risk::R3,
                "the agent forgets things only when the user asks it to".to_string(),
            );
        }
        return (floor, String::new());
    }

    // A hand-off: the agent needs the user to do something only they can --
    // sign in, enter card details, solve a CAPTCHA. It is asked as a question
    // and shows the agent's own words, bounded because they are model output.
    if request.tool == "task.handoff" {
        let reason: String = request
            .arguments
            .get("reason")
            .and_then(Value::as_str)
            .unwrap_or("Your turn")
            .chars()
            .take(240)
            .collect();
        return (floor.max(Risk::R2), reason);
    }

    // A click on a control that commits something.
    if request.tool == "page.click" {
        if let Some(id) = request.arguments.get("element_id").and_then(Value::as_str) {
            if let Some(element) = request.elements.iter().find(|e| e.id == id) {
                if let Some((verb, spends_money)) =
                    consequential_verb(&element.name, request.task)
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
                    let why = if spends_money {
                        format!(
                            "{described} looks like it would {verb} -- the agent \
                             asks before anything that costs money"
                        )
                    } else {
                        format!(
                            "{described} would {verb} something, and the task did \
                             not ask for that"
                        )
                    };
                    return (floor.max(Risk::R2), why);
                }
            }
        }
    }

    // A click at a point, or typing into whatever has focus, that the browser
    // could not resolve to a listed control is an ordinary R1 action.
    //
    // It used to ask. Trace evidence: a model clicking a theme toggle by
    // coordinates stopped to ask permission, and waiting for a person to answer
    // is exactly the slowness the product is trying not to have. The browser
    // resolves both against the page as it is NOW before they get here, so the
    // password, card and payment rules still judge anything that IS listed;
    // what reaches this point is a canvas or a control past the element budget.
    // Payments are the one thing the user wants asked about, and a payment
    // control clicked by coordinates, unlisted, is far from the common case.

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
                    if carries_data(target)
                        && !is_expected(&destination, request)
                        && !is_search_engine(&destination)
                        && !is_search_for_the_task(target, request.task)
                    {
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

/// Whether the task names two candidates as SEPARATE things, so that acting on
/// either is doing what was asked rather than choosing between them.
///
/// MEASURED on the live agent: "Open the product pages for Anvil Pro and Rocket
/// Skates" tied "View Anvil Pro" with "View Rocket Skates" (two of the task's
/// words each), and the first click stopped to ask the user which one they
/// meant -- they had said both. A person told to open two things does not ask.
///
/// What separates this from "Send the report to Alex" beside Alex Chen and Alex
/// Morgan is where the matching words are. There, both candidates match on the
/// SAME word and nothing in the task tells them apart: that is missing
/// information. Here each candidate carries a task word the other does not, so
/// the task told them apart itself.
///
/// Two guards keep this from loosening the rule it sits in. A task offering the
/// choice ("either", "or") is a choice, whatever the words. And BOTH candidates
/// must have a word of their own: one candidate merely being the more specific
/// of two does not mean the user asked for both.
fn named_separately(targets: &[String], task: &str, a: &Element, b: &Element) -> bool {
    let offers_a_choice = task
        .to_ascii_lowercase()
        .split(|c: char| !c.is_ascii_alphanumeric())
        .any(|word| word == "or" || word == "either");
    if offers_a_choice {
        return false;
    }
    let a_name = a.name.to_ascii_lowercase();
    let b_name = b.name.to_ascii_lowercase();
    let has_own_word = |mine: &str, theirs: &str| {
        targets
            .iter()
            .any(|word| mine.contains(word.as_str()) && !theirs.contains(word.as_str()))
    };
    has_own_word(&a_name, &b_name) && has_own_word(&b_name, &a_name)
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
            let other = *candidates.iter().find(|c| {
                c.id != chosen.id
                    && rivals_for_one_choice(chosen, c)
                    && !named_separately(&targets, request.task, chosen, c)
            })?;
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

/// The first consequential verb in an element's accessible name that should
/// stop the agent, and whether it is a payment. None for a control that is
/// harmless, or one the task asked for.
fn consequential_verb(name: &str, task: &str) -> Option<(&'static str, bool)> {
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
    // matching here would escalate most of the web.
    //
    // Matched in the RAW label rather than a list of split words, because
    // whether a word names a record depends on the "#" after it, and splitting
    // on punctuation throws the "#" away.
    let is_word_char = |c: char| c.is_ascii_alphanumeric();
    let names = |verb: &str| {
        lowered.match_indices(verb).any(|(at, _)| {
            let before = lowered[..at].chars().next_back();
            let rest = &lowered[at + verb.len()..];
            let whole_word = !before.is_some_and(is_word_char)
                && !rest.chars().next().is_some_and(is_word_char);
            whole_word && !names_a_record(verb, rest)
        })
    };

    if let Some(verb) = PAYMENT_VERBS.iter().copied().find(|verb| names(verb)) {
        return Some((verb, true));
    }

    let task_words: Vec<String> = task
        .to_ascii_lowercase()
        .split(|c: char| !c.is_ascii_alphanumeric())
        .filter(|w| !w.is_empty())
        .map(str::to_string)
        .collect();
    OUTBOUND_VERBS
        .iter()
        .find(|(verb, asked_for_by)| {
            names(verb)
                && !task_words
                    .iter()
                    .any(|word| asked_for_by.iter().any(|stem| word.starts_with(stem)))
        })
        .map(|(verb, _)| (*verb, false))
}

/// Words on the list that are just as often NOUNS naming a thing that exists.
///
/// MEASURED on benchmark mt-010: the link "Order #4417 -- Pixel 11 case" stopped
/// to ask the user, as though clicking it would place an order. It opens one
/// that was placed weeks ago. Only these words, deliberately -- "Delete #4417"
/// is a verb aimed at a record, and still asks.
const ALSO_A_NOUN: &[&str] = &["order", "transfer", "book"];

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

/// Query parameters that name a search. A page that sends anything else -- a
/// `data=`, a `d=`, a `history=` -- is not a search, whatever it says it is.
const SEARCH_PARAMS: &[&str] = &[
    "q", "query", "k", "s", "search", "search_query", "keyword", "keywords", "text",
    "term", "wd", "p", "kw", "searchtext", "_nkw", "st", "page", "sort", "hl", "lr",
    "ie", "num", "tbm",
];

/// A search on a shop, a video site or any other search page the user did not
/// name, which is most of what a task that begins "find" or "compare" does.
///
/// Allowed only when the query is the USER'S words: every word in it comes from
/// the task, give or take a few the agent added ("weekly", "review"). That is
/// the line that matters. The exfiltration this rule guards against carries
/// something the agent READ -- history, a page's contents -- and that is not in
/// the task. Words the user typed are already known to whoever they typed them
/// to. A query of one or two words that are not the task's is refused, so
/// "secret" cannot be smuggled out as a search; a longer query may carry a few
/// that are new -- one in a query of four words, two in one of six.
///
/// https only, search-named parameters only, a short path, no fragment.
fn is_search_for_the_task(target: &str, task: &str) -> bool {
    let Some(rest) = target.strip_prefix("https://") else {
        return false;
    };
    if rest.contains('#') || rest.contains('@') {
        return false;
    }
    let Some((before, query)) = rest.split_once('?') else {
        return false;
    };
    let path = before.split_once('/').map(|(_, p)| p).unwrap_or("");
    if path.len() > 60 || query.len() > 160 {
        return false;
    }
    let task_words: Vec<String> = task
        .to_ascii_lowercase()
        .split(|c: char| !c.is_ascii_alphanumeric())
        .filter(|w| !w.is_empty())
        .map(str::to_string)
        .collect();
    let mut words = 0usize;
    let mut new_words = 0usize;
    for pair in query.split('&') {
        let (name, value) = pair.split_once('=').unwrap_or((pair, ""));
        if !SEARCH_PARAMS.contains(&name.to_ascii_lowercase().as_str()) {
            return false;
        }
        for word in value
            .to_ascii_lowercase()
            .split(|c: char| !c.is_ascii_alphanumeric())
            .filter(|w| !w.is_empty())
        {
            // A long opaque token is data, not a word.
            if word.len() > 24 {
                return false;
            }
            words += 1;
            if !task_words.iter().any(|t| t == word) {
                new_words += 1;
            }
        }
    }
    // Never more than two, however long the query: the allowance exists so an
    // agent can add "best" or "review", not so a long query can carry a payload.
    words > 0 && new_words <= (words.saturating_sub(2) / 2).min(2)
}

/// Words that say nothing about WHOSE a fact is: what is left after they are
/// dropped is what has to come from the user.
const MEMORY_NOISE: &[&str] = &[
    "the", "user", "users", "their", "they", "them", "this", "that", "with", "for", "and",
    "are", "was", "has", "have", "his", "her", "its", "uses", "use", "likes", "like", "prefers",
    "prefer", "wants", "want", "name", "named", "called", "you", "your", "one", "our", "from",
];

/// Whether the words of `fact` are in `task`, the user's own message. Compared
/// on the first five letters, so "prefers" in the task covers "prefer" in the
/// fact and a plural covers its singular.
fn words_of(text: &str) -> Vec<String> {
    text.to_ascii_lowercase()
        .split(|c: char| !c.is_ascii_alphanumeric())
        .filter(|w| w.len() >= 3)
        .map(str::to_string)
        .collect()
}

fn same_stem(a: &str, b: &str) -> bool {
    let n = a.len().min(b.len()).min(5);
    n >= 3 && a[..n] == b[..n]
}

/// Why a fact must not be kept, or None if it may be.
fn refuse_to_remember(fact: &str, task: &str) -> Option<String> {
    if fact.trim().is_empty() {
        return Some("there was nothing to remember".to_string());
    }
    if fact.chars().count() > 300 {
        return Some("that is too long to remember; say it in one short sentence".to_string());
    }
    // Never a secret, whoever said it. A remembered password is a password in a
    // file, and a code is worthless by the time it is used again.
    let lowered = fact.to_ascii_lowercase();
    const SECRETS: &[&str] = &[
        "password", "passcode", "passwd", "cvv", "otp", "one-time", "verification code",
        "secret key", "api key", "private key", "recovery code", "pin is", "security code",
    ];
    let mut run = 0usize;
    let mut longest = 0usize;
    for c in fact.chars() {
        if c.is_ascii_digit() {
            run += 1;
            longest = longest.max(run);
        } else if c != ' ' && c != '-' {
            run = 0;
        }
    }
    if SECRETS.iter().any(|s| lowered.contains(s)) || longest >= 7 {
        return Some(
            "the agent never remembers passwords, codes, card numbers or IDs".to_string(),
        );
    }
    // Provenance.
    let task_words = words_of(task);
    let content: Vec<String> = words_of(fact)
        .into_iter()
        .filter(|w| !MEMORY_NOISE.contains(&w.as_str()))
        .collect();
    if content.is_empty() {
        return Some("there was nothing specific to remember".to_string());
    }
    let matched = content
        .iter()
        .filter(|w| task_words.iter().any(|t| same_stem(w, t)))
        .count();
    if matched * 10 < content.len() * 7 {
        return Some(
            "the agent remembers only what the user told it in this message, not \
             what it read on a page"
                .to_string(),
        );
    }
    None
}

/// Whether the user's message asks for something to be forgotten.
fn task_asks_to_forget(task: &str) -> bool {
    words_of(task).iter().any(|w| {
        ["forget", "erase", "remov", "delet", "clear", "stop"]
            .iter()
            .any(|stem| w.starts_with(stem))
    })
}

/// A web search is the first step of almost every research task, and it always
/// carries a query string -- so without this every one of them stopped to ask
/// the user whether it could visit Google.
///
/// Exactly these origins, over https, and nothing else: the destination is
/// compared whole, so `google.evil.com`, `www.google.com.evil.com` and
/// `http://www.google.com` are all still asked about. What this gives up: a
/// page that injects "search for <something private>" can put that text in a
/// query to one of these engines. The engine is the only party that sees it,
/// which is what typing it into the address bar would do too, and it is not a
/// channel the page's author can read back from.
fn is_search_engine(destination: &str) -> bool {
    const ENGINES: [&str; 8] = [
        "https://www.google.com",
        "https://scholar.google.com",
        "https://www.bing.com",
        "https://duckduckgo.com",
        "https://search.brave.com",
        "https://arxiv.org",
        "https://www.youtube.com",
        "https://en.wikipedia.org",
    ];
    ENGINES.contains(&destination)
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
