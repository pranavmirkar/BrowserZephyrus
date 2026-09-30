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
//
// It also reads model replies, because a benchmark that parses them itself is
// grading a different browser. The Python port of the extractor drifted from
// this one more than once, and every drift invented model failures (or hid
// real ones) that nobody could see. So a request of
//
//   {"op":"extract","response":"<the model's raw reply>"}
//
// runs exactly what TaskLoop::OnProposed runs -- extract_call, then
// normalize_arguments -- and writes
//
//   {"found":true,"tool":"page.click","arguments":{...}}   or   {"found":false}
//
// `arguments` is whatever the kernel would hand to policy: usually an object,
// but a reply the kernel could not shape into one is passed back as it is so
// the benchmark can grade it as the schema failure it is.

#include <algorithm>
#include <iostream>
#include <optional>
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

// What TaskLoop::OnProposed does with a reply before policy sees it.
std::string Extract(const zephyrus::agent::Kernel& kernel,
                    const std::string& response) {
  const zephyrus::agent::ExtractedCall call =
      kernel.extract_call(::rust::Str(response));
  base::DictValue out;
  out.Set("found", call.found);
  if (!call.found) {
    return base::WriteJson(out).value_or("{}");
  }
  const std::string tool(call.tool);
  const std::string arguments(
      kernel.normalize_arguments(::rust::Str(tool), call.arguments_json));
  out.Set("tool", tool);
  // The kernel always hands back JSON; a parse failure here would mean it did
  // not, and saying so beats guessing at what it meant.
  std::optional<base::Value> parsed =
      base::JSONReader::Read(arguments, base::JSON_PARSE_RFC);
  if (parsed) {
    out.Set("arguments", std::move(*parsed));
  } else {
    out.Set("arguments_unparsed", arguments);
  }
  return base::WriteJson(out).value_or("{}");
}

// The kernel's provider adapters (ADR 0004), so the benchmark speaks to real
// providers with the code the browser runs. The benchmark plays the browser's
// part: it adds the key and sends the request.
std::string ProviderBuild(const zephyrus::agent::Kernel& kernel,
                          const base::DictValue& request) {
  const zephyrus::agent::ProviderRequest built = kernel.build_provider_request(
      TextOr(request, "kind"), TextOr(request, "model"),
      TextOr(request, "system"), TextOr(request, "user"),
      TextOr(request, "image_jpeg_base64"),
      static_cast<uint32_t>(request.FindInt("max_tokens").value_or(1024)),
      request.FindBool("force_tool").value_or(true), TextOr(request, "effort"));
  base::DictValue out;
  out.Set("error", std::string(built.error));
  out.Set("path", std::string(built.path));
  base::DictValue headers;
  for (const auto& header : built.headers) {
    headers.Set(std::string(header.name), std::string(header.value));
  }
  out.Set("headers", std::move(headers));
  out.Set("body", std::string(built.body));
  return base::WriteJson(out).value_or("{}");
}

std::string ProviderParse(const zephyrus::agent::Kernel& kernel,
                          const base::DictValue& request) {
  const int status = request.FindInt("status").value_or(0);
  const zephyrus::agent::ProviderReply reply = kernel.parse_provider_reply(
      TextOr(request, "kind"),
      static_cast<uint16_t>(status < 0 || status > 999 ? 0 : status),
      TextOr(request, "body"));
  base::DictValue out;
  out.Set("error", std::string(reply.error));
  out.Set("found", reply.found);
  out.Set("tool", std::string(reply.tool));
  out.Set("arguments_json", std::string(reply.arguments_json));
  out.Set("text", std::string(reply.text));
  base::DictValue usage;
  // Token counts fit an int for any single reply; clamp rather than wrap.
  auto clamp = [](uint64_t value) {
    return static_cast<int>(std::min<uint64_t>(value, 0x7fffffff));
  };
  usage.Set("input", clamp(reply.input_tokens));
  usage.Set("output", clamp(reply.output_tokens));
  usage.Set("cache_read", clamp(reply.cache_read_tokens));
  usage.Set("cache_write", clamp(reply.cache_write_tokens));
  out.Set("usage", std::move(usage));
  out.Set("stop", std::string(reply.stop));
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

    const std::string op = TextOr(request_json, "op");
    if (op == "extract") {
      std::cout << Extract(*kernel, TextOr(request_json, "response"))
                << std::endl;
      continue;
    }
    if (op == "provider_build") {
      std::cout << ProviderBuild(*kernel, request_json) << std::endl;
      continue;
    }
    if (op == "provider_parse") {
      std::cout << ProviderParse(*kernel, request_json) << std::endl;
      continue;
    }
    if (!op.empty() && op != "decide") {
      std::cout << Refuse("unknown op: " + op) << std::endl;
      continue;
    }

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
