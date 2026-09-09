// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/local_vision_client.h"

#include <utility>

#include "base/base64.h"
#include "base/command_line.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "chrome/browser/zephyrus/agent/dev_model_client.h"
#include "net/base/load_flags.h"
#include "net/base/url_util.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_url_loader.h"

namespace zephyrus::agent {
namespace {

// A description is a sentence or two. Anything longer is the model narrating,
// and it would cost the reasoning model prompt space for no extra information.
constexpr size_t kMaxResponseBytes = 16 * 1024;

// How much of the description is kept.
//
// The response cap above is a network limit; this is a prompt one, and they are
// not the same job. What comes back here was written by a model reading a
// picture the PAGE controls, so its length is not really the local model's
// choice -- a page can put a wall of text on screen and get it read back. Two
// or three sentences is what the description is for; past that it is crowding
// out the accessibility tree, which is the channel that actually knows what is
// clickable.
constexpr size_t kMaxDescription = 400;

// What the local model is asked for.
//
// Written to get STRUCTURE rather than transcription. The accessibility tree
// already carries every label and value precisely, so a description that reads
// them back adds nothing and only risks repeating something private. What the
// tree cannot say is what a page LOOKS like: that a dialog is covering it, that
// a video is playing, that a region is an image of a form rather than a form.
//
// The instruction not to read out personal details is belt and braces. The
// picture was masked before it was encoded, so there should be nothing there to
// read -- but a model told to describe everything will describe everything.
constexpr char kDescribePrompt[] =
    "Describe this browser screenshot in two or three short sentences. "
    "Say what kind of page it is, what is visually prominent, and whether "
    "anything is covering the page such as a dialog, a cookie banner, an ad, "
    "or a video player. Do NOT transcribe text, and do not read out names, "
    "addresses, numbers or any personal detail. Black rectangles are redacted "
    "regions -- say that they are present and nothing more.";

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("zephyrus_agent_local_vision", R"(
      semantics {
        sender: "Zephyrus Agent local vision"
        description:
          "Sends a screenshot of the current page to a vision model running "
          "on the user's own machine, and receives a short text description. "
          "The description, not the image, is what the agent then reasons "
          "with. The image never leaves the device."
        trigger:
          "The agent takes a step while vision is enabled by command line."
        data:
          "A JPEG of the visible page, with regions classified as private "
          "painted over before encoding."
        destination: LOCAL
      }
      policy {
        cookies_allowed: NO
        setting: "Off unless started with the vision switches."
        policy_exception_justification: "Development only, loopback only."
      })");

}  // namespace

// static
std::unique_ptr<LocalVisionClient> LocalVisionClient::CreateIfConfigured(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory) {
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  if (!command_line.HasSwitch(kAgentVisionSwitch) ||
      !command_line.HasSwitch(kAgentModelEndpointSwitch) ||
      !command_line.HasSwitch(kAgentVisionModelSwitch)) {
    return nullptr;
  }

  const GURL endpoint(
      command_line.GetSwitchValueASCII(kAgentModelEndpointSwitch));
  const std::string model =
      command_line.GetSwitchValueASCII(kAgentVisionModelSwitch);

  // The same loopback rule as the reasoning model, and it matters more here.
  // What travels is a picture of whatever the user is looking at, so an
  // endpoint that is a string on a command line must not be able to turn a
  // debugging aid into a screen-sharing service. Checked here rather than
  // trusted to whoever wrote the flag.
  if (!endpoint.is_valid() || model.empty() || !net::IsLocalhost(endpoint)) {
    return nullptr;
  }

  return std::make_unique<LocalVisionClient>(endpoint, model,
                                             std::move(url_loader_factory));
}

LocalVisionClient::LocalVisionClient(
    GURL endpoint,
    std::string model,
    scoped_refptr<network::SharedURLLoaderFactory> factory)
    : endpoint_(std::move(endpoint)),
      model_(std::move(model)),
      url_loader_factory_(std::move(factory)) {}

LocalVisionClient::~LocalVisionClient() = default;

void LocalVisionClient::Describe(const std::vector<uint8_t>& jpeg,
                                 DescribeCallback callback) {
  if (jpeg.empty()) {
    std::move(callback).Run(std::string());
    return;
  }

  base::ListValue images;
  images.Append(base::Base64Encode(jpeg));

  base::DictValue user;
  user.Set("role", "user");
  user.Set("content", kDescribePrompt);
  user.Set("images", std::move(images));

  base::ListValue messages;
  messages.Append(std::move(user));

  base::DictValue options;
  // Same reason as the reasoning model: a browser that answers differently
  // each time you ask it the same thing is not debuggable.
  options.Set("temperature", 0);
  options.Set("seed", 7);

  base::DictValue body;
  body.Set("model", model_);
  body.Set("stream", false);
  body.Set("messages", std::move(messages));
  body.Set("options", std::move(options));

  std::string json;
  if (!base::JSONWriter::Write(body, &json)) {
    std::move(callback).Run(std::string());
    return;
  }

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = endpoint_.Resolve("/api/chat");
  request->method = "POST";
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->load_flags = net::LOAD_DISABLE_CACHE;

  auto loader =
      network::SimpleURLLoader::Create(std::move(request), kTrafficAnnotation);
  loader->AttachStringForUpload(json, "application/json");
  network::SimpleURLLoader* raw_loader = loader.get();

  const LoaderList::iterator handle =
      loaders_.insert(loaders_.end(), std::move(loader));

  raw_loader->DownloadToString(
      url_loader_factory_.get(),
      base::BindOnce(&LocalVisionClient::OnResponse, base::Unretained(this),
                     handle, std::move(callback)),
      kMaxResponseBytes);
}

void LocalVisionClient::OnResponse(LoaderList::iterator loader,
                                   DescribeCallback callback,
                                   std::optional<std::string> body) {
  loaders_.erase(loader);

  // Every failure is an empty description rather than an error. A page the
  // local model could not describe still has an accessibility tree, and losing
  // the whole step over a missing sentence would be the wrong trade.
  if (!body) {
    std::move(callback).Run(std::string());
    return;
  }

  std::optional<base::Value> parsed =
      base::JSONReader::Read(*body, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    std::move(callback).Run(std::string());
    return;
  }

  const std::string* content =
      parsed->GetDict().FindStringByDottedPath("message.content");
  if (!content) {
    std::move(callback).Run(std::string());
    return;
  }

  // Trimmed on a character boundary. A string cut through the middle of a UTF-8
  // sequence is one the JSON writer cannot encode, which would lose the whole
  // Observation rather than the tail of one sentence.
  std::string description = *content;
  if (description.size() > kMaxDescription) {
    size_t end = kMaxDescription;
    while (end > 0 &&
           (static_cast<unsigned char>(description[end]) & 0xC0) == 0x80) {
      --end;
    }
    description.resize(end);
  }
  std::move(callback).Run(std::move(description));
}

}  // namespace zephyrus::agent
