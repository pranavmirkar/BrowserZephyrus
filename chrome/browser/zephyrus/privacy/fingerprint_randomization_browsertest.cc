// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// §6.5 randomization and reporting, end to end in a real renderer.
//
// Every gap these tests cover shipped and passed every existing test, because
// the existing tests verify that perturbation HAPPENS. They never verified that
// it was WIRED. The two are separate properties and one says nothing about the
// other:
//
//   - a worker can be perturbed and disagree with its own document;
//   - a surface can be perturbed and never reported, so it is invisible on the
//     dashboard and uncounted by the attempt heuristic;
//   - an extension can be hidden from getSupportedExtensions() and still be
//     handed out by getExtension();
//   - and the report itself can be a renderer-fatal Mojo request.
//
// All four were found by auditing callers by hand. None of them failed a test.
//
// Note there is no hardcoded "unperturbed" hash anywhere below. Canvas
// rasterization differs by platform and GPU, so a golden value would be a
// flake. Instead every "is it actually perturbing?" check compares TWO ORIGINS:
// distinct seeds must produce distinct bytes for the same drawing, which is
// false in exactly the case we care about — noise that is not being applied.

#include <string>

#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/zephyrus/privacy/privacy_event.h"
#include "chrome/browser/zephyrus/privacy/privacy_features.h"
#include "chrome/browser/zephyrus/privacy/privacy_intelligence_service.h"
#include "chrome/browser/zephyrus/privacy/privacy_intelligence_service_factory.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace zephyrus_privacy {
namespace {

// One drawing, one hash, used from every context. Shared as a string so the
// document and the worker provably run the SAME code: two copies that drifted
// apart would make the agreement test compare two different pictures and pass
// for the wrong reason.
constexpr char kDrawAndHash[] = R"JS(
  const zDraw = (x) => {
    x.fillStyle = '#4477aa'; x.fillRect(0, 0, 64, 64);
    x.fillStyle = '#f60';    x.fillRect(8, 8, 20, 20);
    x.fillStyle = '#0a3';    x.fillRect(30, 40, 25, 15);
  };
  const zHash = (d) => {
    let h = 2166136261 >>> 0;
    for (let i = 0; i < d.length; i++) {
      h ^= d[i]; h = Math.imul(h, 16777619) >>> 0;
    }
    return h.toString(16);
  };
)JS";

class FingerprintRandomizationBrowserTest : public InProcessBrowserTest {
 public:
  FingerprintRandomizationBrowserTest() {
    // Randomization is gated on collection being on as well
    // (IsFingerprintRandomizationEnabled), so enabling only the randomization
    // flag would leave every surface untouched and every assertion below
    // passing vacuously.
    features_.InitWithFeatures({kZephyrusPrivacyIntelligence,
                                kZephyrusPrivacyFingerprintRandomization},
                               {});
  }

  // HTTPS, not http, and not a detail. Two of the surfaces under test only
  // exist in a secure context: navigator.deviceMemory is [SecureContext] in the
  // IDL, and navigator.serviceWorker is absent entirely. On a plain http origin
  // the deviceMemory getter never runs (so its report never fires) and service
  // worker registration throws — both of which look like Zephyrus bugs and are
  // not.
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_https_test_server().Start());
  }

 protected:
  content::WebContents* web_contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  PrivacyIntelligenceService* service() {
    return PrivacyIntelligenceServiceFactory::GetForBrowserContext(
        browser()->profile());
  }

  GURL NavigateTo(const std::string& host) {
    const GURL url = embedded_https_test_server().GetURL(host, "/title1.html");
    EXPECT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
    return url;
  }

  // The canvas as the DOCUMENT sees it.
  std::string DocumentCanvasHash() {
    return content::EvalJs(web_contents(),
                           base::StrCat({kDrawAndHash, R"JS(
      const c = document.createElement('canvas'); c.width = 64; c.height = 64;
      const x = c.getContext('2d'); zDraw(x);
      zHash(x.getImageData(0, 0, 64, 64).data);
    )JS"}))
        .ExtractString();
  }

  // The same canvas as a DEDICATED WORKER sees it, via OffscreenCanvas.
  std::string WorkerCanvasHash() {
    return content::EvalJs(web_contents(),
                           base::StrCat({"const shared = String.raw`",
                                         kDrawAndHash, R"JS(`;
      new Promise((resolve, reject) => {
        const src = shared + `
          self.onmessage = () => {
            const oc = new OffscreenCanvas(64, 64);
            const x = oc.getContext('2d'); zDraw(x);
            self.postMessage(zHash(x.getImageData(0, 0, 64, 64).data));
          };`;
        const w = new Worker(
            URL.createObjectURL(new Blob([src], {type: 'text/javascript'})));
        const timer = setTimeout(() => reject('worker never answered'), 10000);
        w.onmessage = (e) => { clearTimeout(timer); resolve(e.data); };
        w.onerror = (e) => { clearTimeout(timer); reject(e.message); };
        w.postMessage(0);
      });
    )JS"}))
        .ExtractString();
  }

  // The same canvas as a SERVICE WORKER sees it. Kept in a real script file
  // rather than a blob because a service worker must be served from the origin
  // it will be scoped to.
  //
  // Messaged through a MessageChannel against registration.active rather than
  // navigator.serviceWorker.controller: a page is only "controlled" after a
  // reload, and waiting for that would test navigation rather than the seed.
  std::string ServiceWorkerCanvasHash() {
    return content::EvalJs(web_contents(), R"JS(
      (async () => {
        const reg = await navigator.serviceWorker.register(
            '/service_worker/zephyrus_canvas_worker.js');
        const active = reg.active || await new Promise((resolve) => {
          const pending = reg.installing || reg.waiting;
          pending.addEventListener('statechange', function check() {
            if (reg.active) {
              pending.removeEventListener('statechange', check);
              resolve(reg.active);
            }
          });
        });
        const ch = new MessageChannel();
        return await new Promise((resolve) => {
          ch.port1.onmessage = (e) => resolve(e.data);
          active.postMessage('go', [ch.port2]);
        });
      })()
    )JS")
        .ExtractString();
  }

  bool HasReported(const GURL& page, FingerprintSurface surface) {
    const uint32_t bit = 1u << static_cast<uint32_t>(surface);
    // The renderer reports asynchronously; poll rather than sleep a fixed
    // amount so a slow bot is not a flaky failure.
    for (int attempt = 0; attempt < 100; ++attempt) {
      if (service()->GetPageSignals(page).fingerprint_surface_mask & bit) {
        return true;
      }
      base::RunLoop loop;
      base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE, loop.QuitClosure(), base::Milliseconds(100));
      loop.Run();
    }
    return false;
  }

  base::test::ScopedFeatureList features_;
};

// ===========================================================================
// (a) A worker must perturb EXACTLY as its own document does.
// ===========================================================================
//
// "The worker has a seed" is not the property that matters. A worker that
// disagrees with its parent about the same canvas is the cross-read
// inconsistency that makes sites serve bot challenges — strictly worse than not
// perturbing the worker at all, because it is a signal no real machine emits.
IN_PROC_BROWSER_TEST_F(FingerprintRandomizationBrowserTest,
                       DocumentAndWorkerPerturbIdentically) {
  NavigateTo("a.com");
  const std::string document_hash = DocumentCanvasHash();
  const std::string worker_hash = WorkerCanvasHash();

  EXPECT_EQ(document_hash, worker_hash)
      << "a dedicated worker and its own document disagree about the same "
         "canvas; a page can detect that by reading both, and no real machine "
         "behaves this way";
}

// The control for the test above, and the one that fails if the seed stops
// reaching Blink at all. Without it, DocumentAndWorkerPerturbIdentically would
// still pass with randomization completely broken — two unperturbed contexts
// agree perfectly.
IN_PROC_BROWSER_TEST_F(FingerprintRandomizationBrowserTest,
                       DifferentOriginsGetDifferentPixels) {
  NavigateTo("a.com");
  const std::string a_document = DocumentCanvasHash();
  const std::string a_worker = WorkerCanvasHash();

  NavigateTo("b.com");
  const std::string b_document = DocumentCanvasHash();
  const std::string b_worker = WorkerCanvasHash();

  EXPECT_NE(a_document, b_document)
      << "two origins drew the same picture and read back identical bytes, so "
         "no noise is being applied — every other assertion here is vacuous";
  EXPECT_NE(a_worker, b_worker)
      << "worker canvases are identical across origins: the worker is "
         "unperturbed even though the document is";
  // Within each origin the two contexts must still agree.
  EXPECT_EQ(a_document, a_worker);
  EXPECT_EQ(b_document, b_worker);
}

// A service worker has no creating frame, so the copy-from-parent route that
// covers dedicated workers cannot reach it. Measured unperturbed before this
// was built — the exact flag-off bytes — which meant a script could evade §6.5
// entirely by moving its canvas work into one.
//
// The property asserted is the same one as for dedicated workers, and for the
// same reason: agreeing with its own origin's documents matters as much as
// being perturbed at all.
IN_PROC_BROWSER_TEST_F(FingerprintRandomizationBrowserTest,
                       ServiceWorkerPerturbsAndAgreesWithItsOrigin) {
  NavigateTo("a.com");
  const std::string a_document = DocumentCanvasHash();
  const std::string a_service_worker = ServiceWorkerCanvasHash();

  NavigateTo("b.com");
  const std::string b_service_worker = ServiceWorkerCanvasHash();

  EXPECT_NE(a_service_worker, b_service_worker)
      << "two origins' service workers read back identical bytes, so a service "
         "worker is unperturbed; a script evades §6.5 by drawing its canvas in "
         "one";
  EXPECT_EQ(a_document, a_service_worker)
      << "a service worker disagrees with a document of its own origin about "
         "the same canvas, which is a signal no real machine emits";
}

// ===========================================================================
// The report must never be able to kill the renderer.
// ===========================================================================
//
// PrivacyReporter is registered in PopulateChromeFrameBinders only, and its
// browser side is a content::DocumentService that needs a RenderFrameHost.
// Requesting it from a worker scope is not a dropped message — it is a bad Mojo
// message, and the browser terminates the whole renderer:
//
//   "Received bad user message: No binder found for interface
//    zephyrus_privacy.mojom.PrivacyReporter for the dedicated worker scope"
//
// Reached by a worker doing nothing more exotic than reading back a canvas, and
// NOT gated on any feature flag, because the renderer reports the touch whether
// or not it perturbs. Any page using a canvas in a worker crashed.
IN_PROC_BROWSER_TEST_F(FingerprintRandomizationBrowserTest,
                       WorkerCanvasReadDoesNotKillTheRenderer) {
  NavigateTo("a.com");
  ASSERT_FALSE(WorkerCanvasHash().empty());

  // The point of the test: the renderer that just serviced a worker canvas
  // read is still alive and still running script.
  EXPECT_EQ(42, content::EvalJs(web_contents(), "40 + 2"))
      << "the renderer died after a worker read a canvas; the user sees a "
         "crashed tab";
  EXPECT_FALSE(web_contents()->IsCrashed());
}

// ===========================================================================
// (b) Every instrumented surface must REPORT, not merely perturb.
// ===========================================================================
//
// WebGL shipped with its perturbation wired and its
// ZephyrusReportFingerprintSurface call missing, so WebGL fingerprinting never
// reached the privacy service: absent from the dashboard, and uncounted by the
// attempt heuristic, which classifies a page on how many DISTINCT surfaces it
// touched. Every existing test passed. Each surface is asserted separately so a
// failure names the one that went silent.
IN_PROC_BROWSER_TEST_F(FingerprintRandomizationBrowserTest,
                       EveryInstrumentedSurfaceReports) {
  const GURL page = NavigateTo("a.com");

  // The control: nothing reported before the page touches anything.
  ASSERT_EQ(0u, service()->GetPageSignals(page).fingerprint_surface_mask)
      << "a page that has touched nothing must report nothing";

  ASSERT_TRUE(content::ExecJs(web_contents(), base::StrCat({kDrawAndHash, R"JS(
    const c = document.createElement('canvas'); c.width = 64; c.height = 64;
    const x = c.getContext('2d'); zDraw(x);
    x.getImageData(0, 0, 64, 64);   // kCanvasRead
    c.toDataURL();                  // kCanvasExport
    navigator.hardwareConcurrency;  // kHardwareConcurrency
    navigator.deviceMemory;         // kDeviceMemory
    screen.colorDepth;              // kScreenDepth
    const ac = new AudioContext();
    const an = ac.createAnalyser();
    an.getByteFrequencyData(new Uint8Array(an.frequencyBinCount));  // kAudioBuffer
  )JS"})));

  EXPECT_TRUE(HasReported(page, FingerprintSurface::kCanvasRead))
      << "getImageData was not reported";
  EXPECT_TRUE(HasReported(page, FingerprintSurface::kCanvasExport))
      << "toDataURL was not reported";
  EXPECT_TRUE(HasReported(page, FingerprintSurface::kHardwareConcurrency))
      << "navigator.hardwareConcurrency was not reported";
  EXPECT_TRUE(HasReported(page, FingerprintSurface::kDeviceMemory))
      << "navigator.deviceMemory was not reported";
  EXPECT_TRUE(HasReported(page, FingerprintSurface::kScreenDepth))
      << "screen.colorDepth was not reported";
  EXPECT_TRUE(HasReported(page, FingerprintSurface::kAudioBuffer))
      << "AnalyserNode readback was not reported";
}

// WebGL is the surface that actually regressed, and it is separated from the
// six above so that an environment without a working WebGL implementation skips
// only this check instead of hiding all seven.
IN_PROC_BROWSER_TEST_F(FingerprintRandomizationBrowserTest, WebglReports) {
  const GURL page = NavigateTo("a.com");

  if (!content::EvalJs(web_contents(),
                       "!!document.createElement('canvas').getContext('webgl')")
           .ExtractBool()) {
    GTEST_SKIP() << "no WebGL in this environment";
  }

  ASSERT_TRUE(content::ExecJs(
      web_contents(),
      "document.createElement('canvas').getContext('webgl')"
      ".getSupportedExtensions();"));

  EXPECT_TRUE(HasReported(page, FingerprintSurface::kWebglRenderer))
      << "getSupportedExtensions was not reported; WebGL fingerprinting is "
         "invisible to the dashboard and uncounted by the attempt heuristic";
}

// ===========================================================================
// (c) getSupportedExtensions() and getExtension() must AGREE.
// ===========================================================================
//
// Hiding a name from the list while getExtension() still returns a live object
// is trivially detectable: a script asks for each known name directly and
// compares. A browser caught lying that way is worse off than one that never
// filtered at all. Both paths go through ZephyrusWithholdsExtension today;
// nothing stops a future edit touching only one of them.
IN_PROC_BROWSER_TEST_F(FingerprintRandomizationBrowserTest,
                       WebglExtensionListAndGetExtensionAgree) {
  NavigateTo("a.com");

  if (!content::EvalJs(web_contents(),
                       "!!document.createElement('canvas').getContext('webgl')")
           .ExtractBool()) {
    GTEST_SKIP() << "no WebGL in this environment";
  }

  // Ask about a superset: every name the context admits to supporting, plus
  // every name §6.5 withholds. Checking only the advertised list would never
  // exercise the withheld ones, which are the whole point.
  const std::string disagreed =
      content::EvalJs(web_contents(), R"JS(
    (() => {
      const gl = document.createElement('canvas').getContext('webgl');
      const listed = new Set(gl.getSupportedExtensions());
      const withheld = [
        'WEBGL_debug_shaders', 'WEBGL_compressed_texture_s3tc_srgb',
        'WEBGL_compressed_texture_etc', 'WEBGL_compressed_texture_etc1',
        'WEBGL_compressed_texture_astc', 'WEBGL_compressed_texture_pvrtc',
        'WEBGL_compressed_texture_atc', 'EXT_disjoint_timer_query',
        'EXT_disjoint_timer_query_webgl2', 'KHR_parallel_shader_compile',
        'OVR_multiview2', 'WEBGL_multi_draw',
      ];
      const disagreed = [];
      for (const name of new Set([...listed, ...withheld])) {
        const handedOut = gl.getExtension(name) !== null;
        // Not listed but handed out is the detectable lie. Listed but not
        // handed out is the same bug mirrored, and equally detectable.
        if (handedOut !== listed.has(name)) {
          disagreed.push(name + (handedOut ? ' (hidden but handed out)'
                                           : ' (listed but withheld)'));
        }
      }
      return disagreed.join(', ');
    })()
  )JS")
          .ExtractString();

  EXPECT_EQ("", disagreed)
      << "getSupportedExtensions() and getExtension() disagree, which is "
         "directly detectable by any script that asks for a name by hand";
}

}  // namespace
}  // namespace zephyrus_privacy
