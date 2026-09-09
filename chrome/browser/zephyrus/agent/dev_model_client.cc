// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/dev_model_client.h"

#include <utility>

#include "base/command_line.h"
#include "base/threading/thread_restrictions.h"
#include "base/strings/string_split.h"
#include "base/no_destructor.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/strings/strcat.h"
#include "base/functional/bind.h"
#include "base/files/file_util.h"
#include "base/files/file_path.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/ptr_util.h"
#include "base/values.h"
#include "net/base/load_flags.h"
#include "net/base/url_util.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

namespace zephyrus::agent {
namespace {

// A reply larger than this is not a tool call, it is a model that has started
// writing an essay. Capping it keeps a runaway generation from being copied
// into the browser's memory.
constexpr size_t kMaxResponseBytes = 64 * 1024;

// Write every model reply to a file, one JSON object per line.
//
//   --zephyrus-agent-record=D:\\session.jsonl
constexpr char kRecordSwitch[] = "zephyrus-agent-record";

// Replay a recorded session instead of asking a model.
//
//   --zephyrus-agent-replay=D:\\session.jsonl
//
// No endpoint and no model name are needed: nothing is asked of anything. The
// browser still does the real work, so what is under test is the harness.
constexpr char kReplaySwitch[] = "zephyrus-agent-replay";

// Writes the exact prompt the model was given and the exact reply it produced.
//
// Development only, off unless a path is passed:
//
//   --zephyrus-agent-trace=D:\zephyrus-agent-trace.txt
//
// It exists because three separate bugs in the agent's view of the page were
// each diagnosed from a synthetic test page and each turned out to be only part
// of the story on a real site. Guessing at what the model was shown, from the
// outside, has now been wrong more often than it has been right. This is the
// ground truth: what went in, what came back.
//
// The file holds the page's own text, so it is not something to turn on by
// habit -- which is why it takes an explicit path and defaults to nothing.
void Trace(const std::string& what) {
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  if (!command_line.HasSwitch("zephyrus-agent-trace")) {
    return;
  }
  const base::FilePath path =
      command_line.GetSwitchValuePath("zephyrus-agent-trace");
  if (path.empty()) {
    return;
  }
  // A SEQUENCED runner, created once.
  //
  // Two reasons, both learned the hard way. PostTask on the pool is unsequenced,
  // so two steps could write at the same time and interleave a trace whose only
  // job is to be read in order -- and worse, two early writes could each find
  // the file missing and each create it, one truncating the other.
  static base::NoDestructor<scoped_refptr<base::SequencedTaskRunner>> runner(
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::BEST_EFFORT}));

  (*runner)->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::FilePath path, std::string text) {
            // The FIRST write of each browser run starts the file over.
            //
            // Appending across runs put two sessions in one file, and reading
            // it from the top gave the previous run's answers to the current
            // run's question -- which is exactly the wrong conclusion, drawn
            // confidently. A trace is evidence about one run; making that true
            // by construction beats remembering to delete it.
            static bool started = false;
            if (!started) {
              started = true;
              base::WriteFile(path, text);
              return;
            }
            // AppendToFile opens OPEN_EXISTING on Windows and fails silently on
            // a file that is not there yet, so the create above is not optional.
            if (!base::PathExists(path)) {
              base::WriteFile(path, text);
              return;
            }
            base::AppendToFile(path, text);
          },
          path, what));
}

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("zephyrus_agent_dev_model", R"(
        semantics {
          sender: "Zephyrus Agent, development only"
          description:
            "Sends the agent's prompt, which includes a description of the "
            "page the user is looking at, to a model server running on the "
            "user's own machine, and reads back the tool call the model "
            "proposes. This exists so the agent can be developed and watched "
            "before an in-process model runtime exists."
          trigger:
            "Only when the browser was started with both "
            "--zephyrus-agent-model-endpoint and --zephyrus-agent-model, and "
            "the user starts an agent task."
          data:
            "The user's task text, and the browser's description of the "
            "active page: its URL, title, visible text and interactive "
            "elements."
          destination: LOCAL
          internal { contacts { email: "pranavmirkar@gmail.com" } }
          last_reviewed: "2026-09-02"
        }
        policy {
          cookies_allowed: NO
          setting:
            "Off unless the browser is started with the two development "
            "switches named above. There is no UI for it and it is not part "
            "of any shipping build's default behaviour."
          policy_exception_justification:
            "Development-only code path with no enterprise deployment."
        })");

// The `reply` field of every line of a recording, in order.
//
// Read synchronously, because the first Propose may arrive immediately and a
// replay that answered "not loaded yet" would not be deterministic -- which is
// the one property it exists to have. Development-only and a small file.
std::vector<std::string> ReadRecordedReplies(const base::FilePath& path) {
  std::vector<std::string> replies;
  if (path.empty()) {
    return replies;
  }
  std::string contents;
  if (!base::ReadFileToString(path, &contents)) {
    return replies;
  }
  for (const auto& line : base::SplitStringPiece(
           contents, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    std::optional<base::Value> parsed =
        base::JSONReader::Read(line, base::JSON_PARSE_RFC);
    if (!parsed || !parsed->is_dict()) {
      continue;
    }
    const std::string* reply = parsed->GetDict().FindString("reply");
    if (reply) {
      replies.push_back(*reply);
    }
  }
  return replies;
}

// Append one step of a session: what was asked, and what came back.
void Record(const std::string& user_prompt, const std::string& reply) {
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  if (!command_line.HasSwitch(kRecordSwitch)) {
    return;
  }
  const base::FilePath path = command_line.GetSwitchValuePath(kRecordSwitch);
  if (path.empty()) {
    return;
  }

  base::DictValue row;
  row.Set("prompt", user_prompt);
  row.Set("reply", reply);
  std::string line;
  base::JSONWriter::Write(row, &line);
  line += "\n";

  static base::NoDestructor<scoped_refptr<base::SequencedTaskRunner>> runner(
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::BEST_EFFORT}));
  (*runner)->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::FilePath path, std::string text) {
                       // Started fresh per run, like the trace: two sessions in
                       // one recording would replay as one impossible session.
                       static bool started = false;
                       if (!started || !base::PathExists(path)) {
                         started = true;
                         base::WriteFile(path, text);
                         return;
                       }
                       base::AppendToFile(path, text);
                     },
                     path, std::move(line)));
}

}  // namespace

// static
std::unique_ptr<DevModelClient> DevModelClient::CreateIfConfigured(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory) {
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  // A recording is a complete substitute for a model, so it is checked first
  // and needs neither an endpoint nor a model name.
  if (command_line.HasSwitch(kReplaySwitch)) {
    std::unique_ptr<DevModelClient> client = base::WrapUnique(new DevModelClient(
        GURL(), std::string(), std::move(url_loader_factory)));
    client->replaying_ = true;
    client->LoadRecording(command_line.GetSwitchValuePath(kReplaySwitch));
    return client;
  }

  if (!command_line.HasSwitch(kAgentModelEndpointSwitch) ||
      !command_line.HasSwitch(kAgentModelSwitch)) {
    return nullptr;
  }
  return Create(
      GURL(command_line.GetSwitchValueASCII(kAgentModelEndpointSwitch)),
      command_line.GetSwitchValueASCII(kAgentModelSwitch),
      std::move(url_loader_factory));
}

// static
std::unique_ptr<DevModelClient> DevModelClient::Create(
    const GURL& endpoint,
    std::string model,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory) {
  // The check that matters. The prompt carries the page the user is looking at,
  // and the endpoint comes from a command line, so a typo must not be able to
  // send it somewhere real.
  if (!endpoint.is_valid() || !endpoint.SchemeIsHTTPOrHTTPS() ||
      !net::IsLocalhost(endpoint)) {
    return nullptr;
  }
  if (model.empty() || !url_loader_factory) {
    return nullptr;
  }
  return base::WrapUnique(new DevModelClient(endpoint, std::move(model),
                                             std::move(url_loader_factory)));
}

DevModelClient::DevModelClient(
    const GURL& endpoint,
    std::string model,
    scoped_refptr<network::SharedURLLoaderFactory> factory)
    : endpoint_(endpoint),
      model_(std::move(model)),
      url_loader_factory_(std::move(factory)) {}

DevModelClient::~DevModelClient() = default;

mojo::PendingRemote<mojom::AgentModel>
DevModelClient::BindNewPipeAndPassRemote() {
  mojo::PendingRemote<mojom::AgentModel> remote;
  receivers_.Add(this, remote.InitWithNewPipeAndPassReceiver());
  return remote;
}

// static
void DevModelClient::WarmUp(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory) {
  std::unique_ptr<DevModelClient> client =
      CreateIfConfigured(std::move(url_loader_factory));
  if (!client) {
    return;
  }

  // The first thing written, and the reason it is written here rather than at
  // the first prompt: a trace that only appears once a model has answered
  // cannot tell "nothing happened" from "the tracing is broken". It was broken
  // -- AppendToFile never created the file -- and the silence looked exactly
  // like a run that had not happened.
  Trace(base::StrCat({"================ TRACE OPENED ================\n",
                      "model: ", client->model_, "\n"}));

  // An empty message list is how Ollama is asked to load a model and generate
  // nothing. The reply is not read: the point is the side effect.
  base::DictValue body;
  body.Set("model", client->model_);
  body.Set("stream", false);
  body.Set("messages", base::ListValue());
  base::DictValue warm_options;
  warm_options.Set("num_ctx", 8192);
  body.Set("options", std::move(warm_options));
  // Long enough to survive a person reading a page and thinking before they
  // type. The default drops the model again after a few minutes, which would
  // put the whole load back on the next task.
  body.Set("keep_alive", "30m");

  std::string json;
  base::JSONWriter::Write(body, &json);

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = client->endpoint_.Resolve("/api/chat");
  request->method = "POST";
  request->load_flags = net::LOAD_DISABLE_CACHE;
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;

  auto loader = network::SimpleURLLoader::Create(std::move(request),
                                                 kTrafficAnnotation);
  loader->SetTimeoutDuration(base::Seconds(90));
  loader->AttachStringForUpload(json, "application/json");

  // Both pointers are read out BEFORE the call.
  //
  // Argument evaluation order is unspecified in C++, so passing
  // `client->url_loader_factory_.get()` alongside a `std::move(client)` in the
  // same call is a coin flip: if the move happens first, the factory is read
  // through a null unique_ptr and the browser dies the moment the panel opens.
  // It did. The loader was already hoisted for this reason; the factory was
  // not, which is the whole bug.
  network::SharedURLLoaderFactory* factory = client->url_loader_factory_.get();
  network::SimpleURLLoader* raw = loader.get();
  raw->DownloadToString(
      factory,
      // The loader and the client are both kept alive by the callback and both
      // die with it. Nothing here needs an answer.
      base::BindOnce([](std::unique_ptr<DevModelClient>,
                        std::unique_ptr<network::SimpleURLLoader>,
                        std::optional<std::string>) {},
                     std::move(client), std::move(loader)),
      /*max_body_size=*/1024);
}

void DevModelClient::LoadRecording(const base::FilePath& path) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_BLOCKING},
      base::BindOnce(&ReadRecordedReplies, path),
      base::BindOnce(&DevModelClient::OnRecordingLoaded,
                     weak_factory_.GetWeakPtr()));
}

void DevModelClient::OnRecordingLoaded(std::vector<std::string> replies) {
  replies_ = std::move(replies);
  loaded_ = true;

  // Anything that asked while the file was still being read, answered now and
  // in the order it asked.
  std::vector<ProposeCallback> waiting = std::move(waiting_);
  waiting_.clear();
  for (ProposeCallback& callback : waiting) {
    AnswerFromRecording(std::move(callback));
  }
}

void DevModelClient::AnswerFromRecording(ProposeCallback callback) {
  // Past the end is an empty reply, which the loop already treats as "the model
  // said nothing". A recording that runs out mid-task is a shorter task, not a
  // crash.
  std::string reply =
      next_reply_ < replies_.size() ? replies_[next_reply_] : std::string();
  ++next_reply_;
  Trace(base::StrCat(
      {"\n================ REPLAYED ================\n", reply, "\n"}));

  // Posted rather than answered inline: the loop calls Propose from inside its
  // own step, and a synchronous reply would re-enter it.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), std::move(reply)));
}

void DevModelClient::Propose(const std::string& system_prompt,
                             const std::string& user_prompt,
                             ProposeCallback callback) {
  if (replaying_) {
    if (!loaded_) {
      // Held until the recording arrives, in order.
      waiting_.push_back(std::move(callback));
      return;
    }
    AnswerFromRecording(std::move(callback));
    return;
  }

  // The system prompt is the same every step, so it is written once.
  static bool wrote_rules = false;
  if (!wrote_rules) {
    wrote_rules = true;
    Trace(base::StrCat({"================ SYSTEM PROMPT ================\n",
                        system_prompt, "\n"}));
  }
  Trace(base::StrCat({"\n================ ASKED ================\n",
                      user_prompt, "\n"}));

  base::ListValue messages;

  base::DictValue system;
  system.Set("role", "system");
  system.Set("content", system_prompt);
  messages.Append(std::move(system));

  base::DictValue user;
  user.Set("role", "user");
  user.Set("content", user_prompt);
  messages.Append(std::move(user));

  base::DictValue options;
  // Temperature zero because a browser that does something different each time
  // you ask it the same thing is not debuggable.
  options.Set("temperature", 0);
  options.Set("seed", 7);
  options.Set("num_predict", 256);
  options.Set("num_ctx", 8192);

  base::DictValue body;
  body.Set("model", model_);
  body.Set("stream", false);
  body.Set("format", "json");
  body.Set("messages", std::move(messages));
  body.Set("options", std::move(options));
  // Keep it resident between steps and between tasks. Without this the model is
  // dropped a few minutes after the last one and the next task pays the full
  // cold load again.
  body.Set("keep_alive", "30m");

  std::string json;
  if (!base::JSONWriter::Write(body, &json)) {
    std::move(callback).Run(std::string());
    return;
  }

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = endpoint_.Resolve("/api/chat");
  request->method = "POST";
  // Nothing about this is a credentialed request, and the prompt should not
  // carry cookies for a site the user happens to be logged into.
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->load_flags = net::LOAD_DISABLE_CACHE;

  auto loader =
      network::SimpleURLLoader::Create(std::move(request), kTrafficAnnotation);
  loader->SetTimeoutDuration(base::Seconds(90));
  loader->AttachStringForUpload(json, "application/json");
  network::SimpleURLLoader* raw_loader = loader.get();

  const LoaderList::iterator handle =
      loaders_.insert(loaders_.end(), std::move(loader));

  raw_loader->DownloadToString(
      url_loader_factory_.get(),
      base::BindOnce(&DevModelClient::OnResponse, base::Unretained(this),
                     handle, std::move(callback), user_prompt),
      kMaxResponseBytes);
}

void DevModelClient::OnResponse(LoaderList::iterator loader,
                                ProposeCallback callback,
                                std::string user_prompt,
                                std::optional<std::string> body) {
  // Whatever came back, before any parsing. A reply that is not a tool call is
  // exactly the case worth seeing verbatim.
  Trace(base::StrCat({"\n---------------- REPLIED ----------------\n",
                      body ? *body : std::string("(no response)"), "\n"}));

  loaders_.erase(loader);

  // An empty response signals a transport or protocol failure. The loop ends
  // the task with a model-server error instead of retrying an empty answer.
  if (!body) {
    std::move(callback).Run(std::string());
    return;
  }

  std::optional<base::DictValue> parsed =
      base::JSONReader::ReadDict(*body, base::JSON_PARSE_RFC);
  if (!parsed) {
    std::move(callback).Run(std::string());
    return;
  }

  const base::DictValue* message = parsed->FindDict("message");
  const std::string* content =
      message ? message->FindString("content") : nullptr;
  const std::string reply = content ? *content : std::string();

  // One line of the session, so it can be replayed against a changed harness.
  Record(user_prompt, reply);

  std::move(callback).Run(reply);
}

}  // namespace zephyrus::agent
