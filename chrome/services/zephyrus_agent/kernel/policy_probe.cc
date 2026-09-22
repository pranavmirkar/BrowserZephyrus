// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Lets the benchmark ask the REAL kernel what it would decide.
//
// The multi-step benchmark grades what a model proposed. That was the right
// first measure and it has a hole in it: two policy rules now exist that exist
// precisely to stop a proposal from happening -- the ambiguity rule and the
// invented-address rule -- and neither had any effect a task-level score could
// see. A fix whose benefit cannot be measured is a fix nobody can argue with,
// which is the opposite of what the benchmark is for.
//
// The alternative was to reimplement the policy in Python. That is the mistake
// this codebase has already made once with the extractor: two copies in two
// languages, drifting quietly, with the benchmark's copy the narrower one for
// an unknown length of time. So the benchmark asks the shipped kernel instead,
// through the same cxx bridge the browser uses.
//
// Protocol, deliberately the dullest thing that works: one JSON request per
// line on stdin, one JSON decision per line on stdout. No server, no port, no
// handshake. It reads
//
//   {"tool":"page.click","arguments":{...},"task":"...","url":"...",
//    "elements":[{"id":"e1","role":"link","name":"...","sensitivity":""}]}
//
// and writes
//
//   {"disposition":"Allow|Ask|Deny","risk":"R1","reason":"..."}

#include <iostream>
#include <string>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "chrome/services/zephyrus_agent/kernel/src/lib.rs.h"

namespace {

const char* DispositionName(zephyrus::agent::Disposition disposition) {
  switch (disposition) {
    case zephyrus::agent::Disposition::Allow:
      return "Allow";
    case zephyrus::agent::Disposition::Ask:
      return "Ask";
    case zephyrus::agent::Disposition::Deny:
      return "Deny";
  }
  return "Deny";
}

std::string TextOr(const base::DictValue& dict, const char* key) {
  const std::string* value = dict.FindString(key);
  return value ? *value : std::string();
}

// A line that is not a request gets a Deny rather than a crash or a silent
// skip. The benchmark would otherwise read a parse failure as the kernel
// having permitted something.
std::string Refuse(const std::string& why) {
  base::DictValue out;
  out.Set("disposition", "Deny");
  out.Set("risk", "R3");
  out.Set("reason", why);
  return base::WriteJson(out).value_or("{}");
}

}  // namespace

int main() {
  rust::Box<zephyrus::agent::Kernel> kernel = zephyrus::agent::load_kernel();
  if (!kernel->is_valid()) {
    std::cerr << "kernel did not load: " << std::string(kernel->last_error())
              << "\n";
    return 2;
  }

  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.empty()) {
      continue;
    }
    std::optional<base::DictValue> parsed =
        base::JSONReader::ReadDict(line, base::JSON_PARSE_RFC);
    if (!parsed) {
      std::cout << Refuse("the request was not a JSON object") << std::endl;
      continue;
    }
    const base::DictValue& request_json = *parsed;

    zephyrus::agent::PolicyRequest request;
    request.tool = rust::String(TextOr(request_json, "tool"));
    request.task = rust::String(TextOr(request_json, "task"));
    request.url = rust::String(TextOr(request_json, "url"));

    // Arguments arrive as an object and the kernel takes them as text, because
    // the kernel parses them itself -- it does not trust a caller's idea of
    // what they say.
    const base::Value* arguments = request_json.Find("arguments");
    request.arguments_json = rust::String(
        arguments ? base::WriteJson(*arguments).value_or("{}") : "{}");

    if (const base::ListValue* elements = request_json.FindList("elements")) {
      for (const base::Value& entry : *elements) {
        if (!entry.is_dict()) {
          continue;
        }
        const base::DictValue& element_json = entry.GetDict();
        zephyrus::agent::ObservedElement element;
        element.id = rust::String(TextOr(element_json, "id"));
        element.role = rust::String(TextOr(element_json, "role"));
        element.name = rust::String(TextOr(element_json, "name"));
        element.sensitivity = rust::String(TextOr(element_json, "sensitivity"));
        request.elements.push_back(std::move(element));
      }
    }

    zephyrus::agent::PolicyDecision decision = kernel->decide(request);
    base::DictValue out;
    out.Set("disposition", DispositionName(decision.disposition));
    out.Set("risk", std::string(decision.risk));
    out.Set("reason", std::string(decision.reason));
    std::cout << base::WriteJson(out).value_or("{}") << std::endl;
  }
  return 0;
}
