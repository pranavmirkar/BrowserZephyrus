// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_LENS_QUALITY_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_LENS_QUALITY_H_

namespace zephyrus::agent {

// Decides when the lens mascot should paint in "lite" mode (see
// LensPaintOptions) and when it can go back to full quality.
//
// The prototype had a built-in fallback: if the average frame time stayed above
// 27 ms for three seconds while a non-Static fill was showing, it dropped its
// blurs for the rest of the page's life. The handoff asked for it to be
// reversible. This is that, with two additions so it cannot flap: it comes back
// only after the machine has been comfortably fast (average under 18 ms) for a
// sustained stretch, and it never changes its mind twice within a few seconds.
//
// Pure: it is fed frame times and answers a question. No clock, no painting.
class LensQualityGovernor {
 public:
  // Thresholds, in seconds (frame interval) and seconds of wall time.
  static constexpr double kSlowAverage = 0.027;   // drop to lite above this...
  static constexpr double kSlowFor = 3.0;         // ...once it has lasted this long
  static constexpr double kFastAverage = 0.018;   // back to full below this...
  static constexpr double kFastFor = 6.0;         // ...for this long
  static constexpr double kMinHold = 4.0;         // never flip twice within this

  // One frame: `interval` is the time since the previous frame, and `heavy` is
  // whether the fill being shown is one that can cost (anything but Static).
  // Frames longer than 0.25 s are treated as a stall (a hidden window waking
  // up), not as evidence the machine is slow.
  void OnFrame(double interval, bool heavy);

  bool lite() const { return lite_; }
  double average_interval() const { return average_; }

  // Forget everything, e.g. when the skin is switched or the window moves to
  // another display.
  void Reset();

 private:
  bool lite_ = false;
  double average_ = 1.0 / 60;
  double slow_for_ = 0;
  double fast_for_ = 0;
  double since_change_ = 1e9;
};

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_LENS_QUALITY_H_
