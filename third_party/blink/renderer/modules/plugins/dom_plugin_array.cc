/*
 *  Copyright (C) 2008 Nokia Corporation and/or its subsidiary(-ies)
 *  Copyright (C) 2008 Apple Inc. All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *  MA 02110-1301 USA
 */

#include "third_party/blink/renderer/modules/plugins/dom_plugin_array.h"

#include "build/build_config.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/navigator.h"
#include "third_party/blink/renderer/core/frame/zephyrus_fingerprint_seed.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/core/page/plugin_data.h"
#include "third_party/blink/renderer/modules/plugins/dom_mime_type_array.h"
#include "third_party/blink/renderer/modules/plugins/navigator_plugins.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/wtf/text/atomic_string.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"

namespace blink {

namespace {
DOMPlugin* MakeFakePlugin(String plugin_name, LocalDOMWindow* window) {
  String description = "Portable Document Format";
  String filename = "internal-pdf-viewer";
  auto* plugin_info =
      MakeGarbageCollected<PluginInfo>(plugin_name, filename, description,
                                       /*background_color=*/Color::kTransparent,
                                       /*may_use_external_handler=*/false);
  Vector<String> extensions{"pdf"};
  for (const char* mime_type : {"application/pdf", "text/pdf"}) {
    auto* mime_info = MakeGarbageCollected<MimeClassInfo>(
        mime_type, description, *plugin_info, extensions);
    plugin_info->AddMimeType(mime_info);
  }
  return MakeGarbageCollected<DOMPlugin>(window, *plugin_info);
}
}  // namespace

DOMPluginArray::DOMPluginArray(LocalDOMWindow* window) : window_(window) {
  if (IsPdfViewerAvailable()) {
    // See crbug.com/1164635 and https://github.com/whatwg/html/pull/6738.
    // To reduce fingerprinting and make plugins/mimetypes more
    // interoperable, this is the spec'd, hard-coded list of plugins:
    Vector<String> plugins{"PDF Viewer", "Chrome PDF Viewer",
                           "Chromium PDF Viewer", "Microsoft Edge PDF Viewer",
                           "WebKit built-in PDF"};
    // Zephyrus: the five entries are one PDF viewer under five names, and the
    // list is the same for every Chrome user, so it carries no information and
    // cannot vary. A fingerprinting test that asks "does this browser answer
    // differently on two sites?" therefore saw nothing here. Keep the first
    // name always (a page that looks for "PDF Viewer" still finds it) and let a
    // per-site seed pick which of the other four are listed. Same viewer, same
    // mime types; only the number of aliases moves.
    uint32_t keep = 0xf;
    if (std::optional<std::array<uint8_t, 32>> seed =
            ZephyrusSeedForSurface(window, kZephyrusFpNavigator)) {
      constexpr uint64_t kPluginsTag = 0x706c75672d763031ULL;
      keep = 1 + ZephyrusSurfaceValue(*seed, kPluginsTag) % 15;  // 1..15
    }
    for (wtf_size_t i = 0; i < plugins.size(); ++i) {
      if (i == 0 || (keep & (1u << (i - 1)))) {
        dom_plugins_.push_back(MakeFakePlugin(plugins[i], window));
      }
    }
  }
}

void DOMPluginArray::Trace(Visitor* visitor) const {
  visitor->Trace(window_);
  visitor->Trace(dom_plugins_);
  ScriptWrappable::Trace(visitor);
}

unsigned DOMPluginArray::length() const {
  return dom_plugins_.size();
}

DOMPlugin* DOMPluginArray::item(unsigned index) {
  if (index >= dom_plugins_.size()) {
    return nullptr;
  }
  return dom_plugins_[index].Get();
}

DOMPlugin* DOMPluginArray::namedItem(const AtomicString& property_name) {
  for (const auto& plugin : dom_plugins_) {
    if (plugin->name() == property_name) {
      return plugin.Get();
    }
  }
  return nullptr;
}

void DOMPluginArray::NamedPropertyEnumerator(Vector<String>& property_names,
                                             ExceptionState&) const {
  property_names.ReserveInitialCapacity(dom_plugins_.size());
  for (const auto& plugin : dom_plugins_) {
    property_names.UncheckedAppend(plugin->name());
  }
}

bool DOMPluginArray::NamedPropertyQuery(const AtomicString& property_name,
                                        ExceptionState& exception_state) const {
  Vector<String> properties;
  NamedPropertyEnumerator(properties, exception_state);
  return properties.Contains(property_name);
}

void DOMPluginArray::refresh(bool reload) {
  if (!window_) {
    return;
  }
  if (PluginData* data = GetPluginData()) {
    data->ResetPluginData();
  }
  if (reload && window_->GetFrame()) {
    window_->GetFrame()->Reload(WebFrameLoadType::kReload);
  }
}

PluginData* DOMPluginArray::GetPluginData() const {
  return (window_ && window_->GetFrame()) ? window_->GetFrame()->GetPluginData()
                                          : nullptr;
}

HeapVector<Member<DOMMimeType>> DOMPluginArray::GetFixedMimeTypeArray() {
  HeapVector<Member<DOMMimeType>> mimetypes;
  if (dom_plugins_.empty())
    return mimetypes;
  DCHECK_EQ(dom_plugins_[0]->length(), 2u);
  mimetypes.push_back(dom_plugins_[0]->item(0));
  mimetypes.push_back(dom_plugins_[0]->item(1));
  return mimetypes;
}

bool DOMPluginArray::IsPdfViewerAvailable() {
#if BUILDFLAG(IS_ANDROID)
  // TODO(crbug.com/556858610, crbug.com/556857897): Other desktop platforms
  // dynamically check whether PDF viewing is handled by an extension or
  // disabled via chrome://settings/content/pdfDocuments (PDF open vs. download
  // setting). On Android, neither extensions nor the PDF content setting are
  // currently supported. Revisit this check if/when PDF document content
  // settings or extensions need to be supported.
  if (base::FeatureList::IsEnabled(
          blink::features::kAndroidHandlePdfInIframe)) {
    return true;
  }
#endif
  auto* data = GetPluginData();
  if (!data)
    return false;
  for (const Member<MimeClassInfo>& mime_info : data->Mimes()) {
    if (mime_info->Type() == "application/pdf")
      return true;
  }
  return false;
}

}  // namespace blink
