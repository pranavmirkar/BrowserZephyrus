// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_workspace_icons.h"

#include <cmath>
#include <map>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

#include "base/no_destructor.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "cc/paint/paint_flags.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "third_party/skia/include/core/SkPathMeasure.h"
#include "third_party/skia/include/utils/SkParsePath.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"

namespace zephyrus {

namespace {

constexpr double kTau = 2.0 * std::numbers::pi;

// The glyphs are authored in a 24x24 box, as SVG always is.
constexpr float kViewBox = 24.f;

// How many points a container outline is sampled at.
//
// The same count the CSS version used, and for the same reason: both endpoints
// of the morph must have IDENTICAL vertex counts or there is no correspondence
// between them to interpolate. 96 is also well past what the eye resolves at a
// 28px cell, so the polygon reads as a curve.
constexpr int kOutlinePoints = 96;

// A rounded regular polygon, as a radius function of the angle.
//
// `round` blends between the polygon's flat edge (0) and a circle (1); the
// apothem term is what keeps the FLAT of the edge at the right distance rather
// than the vertex.
double PolygonRadius(int sides, double round, double a) {
  const double seg = kTau / sides;
  const double apothem = std::cos(std::numbers::pi / sides);
  const double local = std::fmod(std::fmod(a, seg) + seg, seg);
  return (apothem / std::cos(local - seg / 2.0)) * (1.0 - round) + round;
}

// Unnormalised radius at an angle. Peak-normalised by RadiusAt below.
double RawRadius(Shape shape, double a) {
  switch (shape) {
    case Shape::kCircle:
      return 1.0;
    case Shape::kSquircle:
      return std::pow(std::pow(std::abs(std::cos(a)), 4.0) +
                          std::pow(std::abs(std::sin(a)), 4.0),
                      -0.25);
    case Shape::kCookie:
      return 1.0 + 0.085 * std::cos(9 * a);
    case Shape::kClover:
      return 1.0 + 0.130 * std::cos(4 * a);
    case Shape::kBurst:
      return 1.0 + 0.095 * std::cos(12 * a);
    case Shape::kGem:
      return 1.0 + 0.110 * std::cos(6 * a);
    case Shape::kPentagon:
      return PolygonRadius(5, 0.45, a);
    case Shape::kTriangle:
      return PolygonRadius(3, 0.55, a);
    case Shape::kHexagon:
      return PolygonRadius(6, 0.40, a);
  }
  return 1.0;
}

// 1 / the shape's peak radius, so every silhouette fills the cell to exactly
// the same extent. Without this a cookie would sit visibly larger than a
// circle, because its lobes push past 1.
double PeakScale(Shape shape) {
  static base::NoDestructor<std::map<int, double>> cache;
  const int key = static_cast<int>(shape);
  auto it = cache->find(key);
  if (it != cache->end()) {
    return it->second;
  }
  double peak = 0.0;
  for (int i = 0; i < 720; ++i) {
    peak = std::max(peak, RawRadius(shape, (i / 720.0) * kTau));
  }
  const double scale = peak > 0.0 ? 1.0 / peak : 1.0;
  cache->emplace(key, scale);
  return scale;
}

double RadiusAt(Shape shape, double a) {
  return RawRadius(shape, a) * PeakScale(shape);
}

// Parsed once per glyph. Keyed on the literal's ADDRESS, which is stable
// because every path_data is a string literal in the table below.
const SkPath& GlyphPath(const char* path_data) {
  static base::NoDestructor<std::map<const char*, SkPath>> cache;
  auto it = cache->find(path_data);
  if (it != cache->end()) {
    return it->second;
  }
  SkPath path;
  // A glyph that fails to parse draws nothing rather than drawing garbage.
  SkParsePath::FromSVGString(path_data, &path);
  return cache->emplace(path_data, std::move(path)).first->second;
}

// The glyph's subpaths, parsed individually.
//
// Three effects work PER SUBPATH -- stagger brings them in one after another,
// orbit turns all but the first, draw retraces each -- so the concatenated path
// is not enough for them. Splitting on 'M' is exact here rather than a guess:
// the generator emits an uppercase absolute moveto at the head of every
// subpath and nowhere else (a relative 'm' is lowercase).
const std::vector<SkPath>& GlyphSubpaths(const char* path_data) {
  static base::NoDestructor<std::map<const char*, std::vector<SkPath>>> cache;
  auto it = cache->find(path_data);
  if (it != cache->end()) {
    return it->second;
  }
  std::vector<SkPath> parts;
  std::string_view all(path_data);
  size_t begin = all.find('M');
  while (begin != std::string_view::npos) {
    const size_t next = all.find('M', begin + 1);
    const std::string piece(all.substr(
        begin, next == std::string_view::npos ? std::string_view::npos
                                              : next - begin));
    SkPath path;
    if (SkParsePath::FromSVGString(piece.c_str(), &path) && !path.isEmpty()) {
      parts.push_back(std::move(path));
    }
    begin = next;
  }
  return cache->emplace(path_data, std::move(parts)).first->second;
}

// Piecewise-linear keyframes, which is what a CSS @keyframes block is once the
// percentages are read off it. Stops must be sorted.
struct Key {
  float at;
  float value;
};
float KeyframeAt(base::span<const Key> keys, float t) {
  if (keys.empty()) {
    return 0.f;
  }
  if (t <= keys.front().at) {
    return keys.front().value;
  }
  for (size_t i = 1; i < keys.size(); ++i) {
    if (t <= keys[i].at) {
      const float span = keys[i].at - keys[i - 1].at;
      const float f = span > 0.f ? (t - keys[i - 1].at) / span : 1.f;
      return keys[i - 1].value + (keys[i].value - keys[i - 1].value) * f;
    }
  }
  return keys.back().value;
}

// The leading `fraction` of a path, by arc length.
SkPath PathPrefix(const SkPath& path, float fraction) {
  if (fraction >= 1.f) {
    return path;
  }
  // getSegment appends into a BUILDER in this Skia version, not a path.
  SkPathBuilder out;
  SkPathMeasure measure(path, false);
  do {
    const SkScalar length = measure.getLength();
    if (length > 0) {
      measure.getSegment(0, length * std::clamp(fraction, 0.f, 1.f), &out,
                         true);
    }
  } while (measure.nextContour());
  return out.detach();
}

constexpr WorkspaceIcon kIcons[] = {
    {"personal", u"Personal", Shape::kSquircle, Effect::kBounce, 258,
     "M8.4 8.4A3.6 3.6 0 1 0 15.6 8.4A3.6 3.6 0 1 0 8.4 8.4Z"
         "M4.8 20.2c0-3.6 3.2-6 7.2-6s7.2 2.4 7.2 6"},
    {"work", u"Work", Shape::kSquircle, Effect::kStagger, 232,
     "M6.4 7.4H17.6A3.2 3.2 0 0 1 20.8 10.6V16.8A3.2 3.2 0 0 1 17.6 20H6.4A3.2 3.2 0 0 1 3.2 16.8V10.6A3.2 3.2 0 0 1 6.4 7.4Z"
         "M9 7.4V6a2 2 0 0 1 2-2h2a2 2 0 0 1 2 2v1.4 M3.2 12.8h17.6"
         "M11 12.8v1.6h2v-1.6"},
    {"study", u"Study", Shape::kPentagon, Effect::kWiggle, 196,
     "M2.6 9.4 12 4.8l9.4 4.6L12 14 2.6 9.4Z"
         "M6.6 11.6v4.4c0 .7.4 1.3 1 1.7 1.3.7 2.8 1.1 4.4 1.1s3.1-.4 4.4-1.1c.6-.4 1-1 1-1.7v-4.4"
         "M20.4 10v4.6"},
    {"code", u"Code", Shape::kHexagon, Effect::kDraw, 168,
     "M8.4 8 4 12l4.4 4 M15.6 8 20 12l-4.4 4 M0 0m13.4 5.4-2.8 13.2"},
    {"design", u"Design", Shape::kClover, Effect::kStagger, 322,
     "M12 3.6a8.4 8.4 0 0 0 0 16.8c1.3 0 2.2-.8 2.2-2 0-.6-.2-1-.6-1.4-.3-.4-.5-.8-.5-1.3 0-1.1.9-2 2-2h1.5c2.2 0 4-1.8 4-4 0-3.8-3.9-6.1-8.6-6.1Z"
         "M7.2 8.4A1 1 0 1 0 9.2 8.4A1 1 0 1 0 7.2 8.4Z"
         "M11.4 6.9A1 1 0 1 0 13.4 6.9A1 1 0 1 0 11.4 6.9Z"
         "M15.1 9.3A1 1 0 1 0 17.1 9.3A1 1 0 1 0 15.1 9.3Z"
         "M6.3 12.9A1 1 0 1 0 8.3 12.9A1 1 0 1 0 6.3 12.9Z"},
    {"shopping", u"Shopping", Shape::kCookie, Effect::kJitter, 38,
     "M4.9 8h14.2l.9 10.8a2.2 2.2 0 0 1-2.2 2.4H6.2A2.2 2.2 0 0 1 4 18.8L4.9 8Z"
         "M8.6 10.6V7a3.4 3.4 0 0 1 6.8 0v3.6"},
    {"travel", u"Travel", Shape::kBurst, Effect::kShimmer, 206,
     "M11 4.4a1 1 0 0 1 2 0v4.9l7.8 4.5v2.1L13 13.6v4.1l2.5 1.9v1.6L12 20.3l-3.5.9v-1.6l2.5-1.9v-4.1l-7.8 2.3v-2.1L11 9.3V4.4Z"},
    {"finance", u"Finance", Shape::kHexagon, Effect::kDraw, 142,
     "M4.6 19.8v-6.4 M9.5 19.8V8.6 M14.5 19.8v-4.2 M19.4 19.8V5"},
    {"media", u"Media", Shape::kCookie, Effect::kOrbit, 286,
     "M3.6 12A8.4 8.4 0 1 0 20.4 12A8.4 8.4 0 1 0 3.6 12Z"
         "M10.2 8.7 16 12l-5.8 3.3V8.7Z"},
    {"music", u"Music", Shape::kClover, Effect::kJitter, 304,
     "M9.2 17.9V6.4l9.6-2v11.5"
         "M4.6 18.1A2.3 2.3 0 1 0 9.2 18.1A2.3 2.3 0 1 0 4.6 18.1Z"
         "M14.2 16.1A2.3 2.3 0 1 0 18.8 16.1A2.3 2.3 0 1 0 14.2 16.1Z"
         "M9.2 9.6 18.8 7.6"},
    {"gaming", u"Gaming", Shape::kGem, Effect::kWiggle, 270,
     "M7.2 8.4h9.6a5.6 5.6 0 0 1 0 11.2H7.2a5.6 5.6 0 0 1 0-11.2Z"
         "M6.4 14h3.4 M8.1 12.3v3.4"
         "M14.4 12.7A1 1 0 1 0 16.4 12.7A1 1 0 1 0 14.4 12.7Z"
         "M16.8 15.3A1 1 0 1 0 18.8 15.3A1 1 0 1 0 16.8 15.3Z"},
    {"reading", u"Reading", Shape::kSquircle, Effect::kStagger, 28,
     "M12 7.6C10.3 6.1 8 5.3 5.3 5.3c-.7 0-1.3.6-1.3 1.3v10.2c0 .7.6 1.3 1.3 1.3 2.7 0 5 .8 6.7 2.3"
         "M12 7.6c1.7-1.5 4-2.3 6.7-2.3.7 0 1.3.6 1.3 1.3v10.2c0 .7-.6 1.3-1.3 1.3-2.7 0-5 .8-6.7 2.3"
         "M12 7.6v12.8"},
    {"social", u"Social", Shape::kCookie, Effect::kWiggle, 340,
     "M20.2 11.9c0 3.7-3.5 6.7-7.7 6.7-.9 0-1.8-.1-2.6-.4l-5.1 1.9 1.5-3.5a6.3 6.3 0 0 1-1.5-4.1c0-3.7 3.4-6.7 7.7-6.7s7.7 3 7.7 6.1Z"
         "M9.3 12h.01 M12.5 12h.01 M15.7 12h.01"},
    {"mail", u"Mail", Shape::kSquircle, Effect::kBounce, 214,
     "M6.4 5.4H17.6A3.2 3.2 0 0 1 20.8 8.6V15.4A3.2 3.2 0 0 1 17.6 18.6H6.4A3.2 3.2 0 0 1 3.2 15.4V8.6A3.2 3.2 0 0 1 6.4 5.4Z"
         "M0 0m4.8 8.2 6.1 4.5a2 2 0 0 0 2.2 0l6.1-4.5"},
    {"research", u"Research", Shape::kGem, Effect::kBreathe, 152,
     "M10 3.8v5.4L5.5 17a2.4 2.4 0 0 0 2.1 3.6h8.8a2.4 2.4 0 0 0 2.1-3.6L14 9.2V3.8"
         "M8.9 3.8h6.2 M7.4 15.2h9.2"},
    {"private", u"Private", Shape::kPentagon, Effect::kShimmer, 246,
     "M12 3.6 4.6 6.4v5.3c0 4.5 3 7.8 7.4 9.1 4.4-1.3 7.4-4.6 7.4-9.1V6.4L12 3.6Z"
         "M10.4 11.1A1.6 1.6 0 1 0 13.6 11.1A1.6 1.6 0 1 0 10.4 11.1Z"
         "M12 12.7v2.6"},
    {"health", u"Health", Shape::kClover, Effect::kPulse, 356,
     "M12 20.2S3.9 15.5 3.9 9.9A4.6 4.6 0 0 1 12 6.8a4.6 4.6 0 0 1 8.1 3.1c0 5.6-8.1 10.3-8.1 10.3Z"},
    {"home", u"Home", Shape::kSquircle, Effect::kBounce, 46,
     "M3.9 10.5 12 4.1l8.1 6.4v8a2.2 2.2 0 0 1-2.2 2.2H6.1a2.2 2.2 0 0 1-2.2-2.2v-8Z"
         "M9.5 20.7v-5.9h5v5.9"},
    {"focus", u"Focus", Shape::kBurst, Effect::kBreathe, 262,
     "M20.1 14.6A8.5 8.5 0 0 1 9.4 3.9a8.5 8.5 0 1 0 10.7 10.7Z"},
    {"cloud", u"Cloud", Shape::kCookie, Effect::kBreathe, 192,
     "M7.6 18.6h9.7a4.2 4.2 0 0 0 .6-8.3 6.1 6.1 0 0 0-11.6 1.4 3.5 3.5 0 0 0 1.3 6.9Z"},
    {"starred", u"Starred", Shape::kBurst, Effect::kJitter, 62,
     "M0 0m12 4 2.5 5.3 5.7.8-4.1 4.1 1 5.8-5.1-2.8-5.1 2.8 1-5.8L3.8 10.1l5.7-.8L12 4Z"},
    {"files", u"Files", Shape::kSquircle, Effect::kStagger, 224,
     "M3.8 7.6a2.2 2.2 0 0 1 2.2-2.2h3.3a2.2 2.2 0 0 1 1.7.8l1.2 1.5h5.8a2.2 2.2 0 0 1 2.2 2.2v7.7a2.2 2.2 0 0 1-2.2 2.2H6a2.2 2.2 0 0 1-2.2-2.2V7.6Z"},
    {"photos", u"Photos", Shape::kGem, Effect::kOrbit, 178,
     "M3.6 9a2.4 2.4 0 0 1 2.4-2.4h1.7l1.4-2.2h5.8l1.4 2.2H18A2.4 2.4 0 0 1 20.4 9v8.2A2.4 2.4 0 0 1 18 19.6H6a2.4 2.4 0 0 1-2.4-2.4V9Z"
         "M8.6 12.9A3.4 3.4 0 1 0 15.4 12.9A3.4 3.4 0 1 0 8.6 12.9Z"},
    {"calendar", u"Calendar", Shape::kSquircle, Effect::kBounce, 348,
     "M6.6 5.6H17.4A3.2 3.2 0 0 1 20.6 8.8V17.4A3.2 3.2 0 0 1 17.4 20.6H6.6A3.2 3.2 0 0 1 3.4 17.4V8.8A3.2 3.2 0 0 1 6.6 5.6Z"
         "M3.4 10.2h17.2 M8.4 3.4v4 M15.6 3.4v4"},
    {"web", u"Web", Shape::kCircle, Effect::kOrbit, 186,
     "M3.6 12A8.4 8.4 0 1 0 20.4 12A8.4 8.4 0 1 0 3.6 12Z M3.6 12h16.8"
         "M12 3.6c2.3 2.3 3.6 5.3 3.6 8.4S14.3 18.1 12 20.4c-2.3-2.3-3.6-5.3-3.6-8.4S9.7 5.9 12 3.6Z"},
    {"quick", u"Quick", Shape::kTriangle, Effect::kJitter, 74,
     "M13.3 3.6 5.6 13.9h5.2l-.1 6.5 7.7-10.3h-5.2l.1-6.5Z"},
    {"inbox", u"Inbox", Shape::kSquircle, Effect::kShimmer, 240,
     "M3.6 14.4h4.3l1.3 2.4h5.6l1.3-2.4h4.3"
         "M3.6 14.4 6 6.3a2.2 2.2 0 0 1 2.1-1.5h7.8A2.2 2.2 0 0 1 18 6.3l2.4 8.1v3.2a2.2 2.2 0 0 1-2.2 2.2H5.8a2.2 2.2 0 0 1-2.2-2.2v-3.2Z"},
    {"video", u"Videos", Shape::kSquircle, Effect::kBounce, 18,
     "M5.4 6.6H12.6A2.6 2.6 0 0 1 15.2 9.2V14.8A2.6 2.6 0 0 1 12.6 17.4H5.4A2.6 2.6 0 0 1 2.8 14.8V9.2A2.6 2.6 0 0 1 5.4 6.6Z"
         "M15.2 10.4 19.9 7.7c.5-.3 1.1 0 1.1.6v7.4c0 .6-.6.9-1.1.6L15.2 13.6"},
    {"news", u"News", Shape::kSquircle, Effect::kStagger, 30,
     "M5 4.8h11.2A1.8 1.8 0 0 1 18 6.6V19H6.8A1.8 1.8 0 0 1 5 17.2Z"
         "M18 9h1.4A1.6 1.6 0 0 1 21 10.6v6.8a1.6 1.6 0 0 1-1.6 1.6H18"
         "M8.5 8.5h6 M8.5 12h6 M8.5 15.5h3.5"},
    {"bookmark", u"Bookmark", Shape::kGem, Effect::kBounce, 220,
     "M7 4.5h10A1.5 1.5 0 0 1 18.5 6v14L12 16 5.5 20V6A1.5 1.5 0 0 1 7 4.5Z"},
    {"message", u"Messages", Shape::kCookie, Effect::kWiggle, 160,
     "M5 5h14a2 2 0 0 1 2 2v8.5a2 2 0 0 1-2 2H10l-4.5 3.5V17.5H5a2 2 0 0 1-2-2V7a2 2 0 0 1 2-2Z"},
    {"place", u"Places", Shape::kBurst, Effect::kBounce, 350,
     "M12 21s-6.5-5.6-6.5-11a6.5 6.5 0 0 1 13 0c0 5.4-6.5 11-6.5 11Z"
         "M9.3 10A2.7 2.7 0 1 0 14.7 10A2.7 2.7 0 1 0 9.3 10Z"},
    {"compass", u"Explore", Shape::kCircle, Effect::kOrbit, 200,
     "M3.5 12A8.5 8.5 0 1 0 20.5 12A8.5 8.5 0 1 0 3.5 12Z"
         "M15.6 8.4l-1.8 4.6-4.6 1.8 1.8-4.6Z"},
    {"rocket", u"Launch", Shape::kTriangle, Effect::kJitter, 15,
     "M12 3c3 1.6 5 4.6 5 8.4V15l-2.3 2.3H9.3L7 15v-3.6C7 7.6 9 4.6 12 3Z"
         "M10.4 10.2A1.6 1.6 0 1 0 13.6 10.2A1.6 1.6 0 1 0 10.4 10.2Z"
         "M10 19.5c.4 1 1 1.6 2 2 1-.4 1.6-1 2-2"
         "M7 12.5 4.5 15v3L7 16.5 M17 12.5 19.5 15v3L17 16.5"},
    {"idea", u"Ideas", Shape::kClover, Effect::kPulse, 52,
     "M9 18h6 M10 21h4"
         "M12 3.5a5.7 5.7 0 0 0-3.4 10.3c.6.5 1 1.2 1 2V16.2h4.8v-.4c0-.8.4-1.5 1-2A5.7 5.7 0 0 0 12 3.5Z"},
    {"wallet", u"Money", Shape::kSquircle, Effect::kBounce, 140,
     "M5 7.5A2.5 2.5 0 0 1 7.5 5H18v3.5"
         "M5 7.5V17a2.5 2.5 0 0 0 2.5 2.5h11A1.5 1.5 0 0 0 20 18V10a1.5 1.5 0 0 0-1.5-1.5H7.5A2.5 2.5 0 0 1 5 6"
         "M16 14h.01"},
    {"tag", u"Deals", Shape::kPentagon, Effect::kWiggle, 40,
     "M3.5 11.6V5.5A1.5 1.5 0 0 1 5 4h6.1a1.5 1.5 0 0 1 1.06.44l7.4 7.4a1.5 1.5 0 0 1 0 2.12l-6.1 6.1a1.5 1.5 0 0 1-2.12 0l-7.4-7.4A1.5 1.5 0 0 1 3.5 11.6Z"
         "M8.3 8.3h.01"},
    {"gift", u"Gifts", Shape::kCookie, Effect::kJitter, 330,
     "M4.5 11h15v8.5a1.5 1.5 0 0 1-1.5 1.5H6a1.5 1.5 0 0 1-1.5-1.5Z"
         "M3.5 7.5h17V11h-17Z M12 7.5V21"
         "M12 7.5C10 7.5 8 6.8 8 5.3A1.8 1.8 0 0 1 12 6 M12 7.5c2 0 4-.7 4-2.2A1.8 1.8 0 0 0 12 6"},
    {"coffee", u"Coffee", Shape::kSquircle, Effect::kBreathe, 25,
     "M5 9h11v5.5A4.5 4.5 0 0 1 11.5 19h-2A4.5 4.5 0 0 1 5 14.5Z"
         "M16 10h1.5a2.5 2.5 0 0 1 0 5H15.8"
         "M8 3.5v2 M11 3.5v2 M14 3.5v2"},
    {"food", u"Food", Shape::kCookie, Effect::kWiggle, 20,
     "M7 3.5v7a2 2 0 0 0 2 2v8 M5 3.5v5 M9 3.5v5"
         "M17.5 20.5V3.5c-2.2 1.3-3.5 3.8-3.5 7 0 1.5 1 2.5 3.5 2.5"},
    {"fitness", u"Fitness", Shape::kHexagon, Effect::kBounce, 120,
     "M6.5 8v8 M17.5 8v8 M3.5 10v4 M20.5 10v4 M6.5 12h11"},
    {"leaf", u"Nature", Shape::kClover, Effect::kBreathe, 110,
     "M5 19c0-8 5-13 14-14 0 9-5 14-13 14Z M5 19l7-7"},
    {"car", u"Cars", Shape::kSquircle, Effect::kJitter, 210,
     "M5 15.5V12l1.8-4.5A1.5 1.5 0 0 1 8.2 6.5h7.6a1.5 1.5 0 0 1 1.4 1L19 12v3.5"
         "M3.8 12h16.4"
         "M5 15.5h14v2a1 1 0 0 1-1 1h-1.5a1 1 0 0 1-1-1v-.5h-7v.5a1 1 0 0 1-1 1H6a1 1 0 0 1-1-1Z"
         "M7.5 14.2h.01 M16.5 14.2h.01"},
    {"terminal", u"Terminal", Shape::kSquircle, Effect::kDraw, 135,
     "M4.5 5h15A1.5 1.5 0 0 1 21 6.5v11a1.5 1.5 0 0 1-1.5 1.5h-15A1.5 1.5 0 0 1 3 17.5v-11A1.5 1.5 0 0 1 4.5 5Z"
         "M7 10l3 2.5L7 15 M12.5 15h4.5"},
    {"lock", u"Security", Shape::kGem, Effect::kBounce, 250,
     "M6.5 11h11a1.5 1.5 0 0 1 1.5 1.5v6.5a1.5 1.5 0 0 1-1.5 1.5h-11A1.5 1.5 0 0 1 5 19v-6.5A1.5 1.5 0 0 1 6.5 11Z"
         "M8 11V8a4 4 0 0 1 8 0v3 M12 15v1.8"},
    {"bell", u"Alerts", Shape::kBurst, Effect::kWiggle, 45,
     "M6 16.5V11a6 6 0 0 1 12 0v5.5l1.5 1.5h-15Z M10.2 20.5a2 2 0 0 0 3.6 0"},
    {"trophy", u"Goals", Shape::kPentagon, Effect::kShimmer, 48,
     "M7.5 4h9v5.5a4.5 4.5 0 0 1-9 0Z"
         "M7.5 6H4.5v1.5A3 3 0 0 0 7.5 10.5 M16.5 6h3v1.5A3 3 0 0 1 16.5 10.5"
         "M12 14v3.5 M9.5 17.5h5v3h-5Z"},
    {"flag", u"Flag", Shape::kTriangle, Effect::kWiggle, 5,
     "M5.5 21V4 M5.5 5h11.5l-2 3.5 2 3.5H5.5"},
    {"sun", u"Weather", Shape::kBurst, Effect::kOrbit, 55,
     "M8.4 12A3.6 3.6 0 1 0 15.6 12A3.6 3.6 0 1 0 8.4 12Z"
         "M12 3.5v1.8 M12 18.7v1.8 M3.5 12h1.8 M18.7 12h1.8"
         "M6 6l1.3 1.3 M16.7 16.7 18 18 M18 6l-1.3 1.3 M7.3 16.7 6 18"},
    {"headphones", u"Listen", Shape::kCircle, Effect::kPulse, 290,
     "M4.5 14v-2a7.5 7.5 0 0 1 15 0v2"
         "M4.5 14h2.5a1 1 0 0 1 1 1v3a1 1 0 0 1-1 1H6a1.5 1.5 0 0 1-1.5-1.5Z"
         "M19.5 14H17a1 1 0 0 0-1 1v3a1 1 0 0 0 1 1h1a1.5 1.5 0 0 0 1.5-1.5Z"},
    {"tv", u"Shows", Shape::kSquircle, Effect::kShimmer, 275,
     "M5 7h14a1.5 1.5 0 0 1 1.5 1.5v8A1.5 1.5 0 0 1 19 18H5a1.5 1.5 0 0 1-1.5-1.5v-8A1.5 1.5 0 0 1 5 7Z"
         "M8.5 3.5 12 7l3.5-3.5 M8 20.5h8"},
    {"laptop", u"Laptop", Shape::kSquircle, Effect::kBounce, 205,
     "M6 6h12a1.5 1.5 0 0 1 1.5 1.5V15h-15V7.5A1.5 1.5 0 0 1 6 6Z"
         "M3 18h18l-1.2 1.5H4.2Z"},
    {"clock", u"Time", Shape::kCircle, Effect::kOrbit, 190,
     "M3.5 12A8.5 8.5 0 1 0 20.5 12A8.5 8.5 0 1 0 3.5 12Z"
         "M12 7.5V12l3 1.8"},
    {"layers", u"Layers", Shape::kGem, Effect::kStagger, 235,
     "M12 4 3.5 8.5 12 13l8.5-4.5Z"
         "M3.5 12.5 12 17l8.5-4.5 M3.5 16 12 20.5 20.5 16"},
    {"pie", u"Reports", Shape::kCircle, Effect::kOrbit, 300,
     "M12 3.5a8.5 8.5 0 1 0 8.5 8.5H12Z"
         "M14.5 3.8A8.5 8.5 0 0 1 20.2 9.5H14.5Z"},
    {"key", u"Keys", Shape::kGem, Effect::kJitter, 60,
     "M4.2 15A3.8 3.8 0 1 0 11.8 15A3.8 3.8 0 1 0 4.2 15Z"
         "M10.7 12.3 19.5 3.5 M16 7l2.5 2.5 M13.5 9.5 15.5 11.5"},
    {"brush", u"Art", Shape::kCookie, Effect::kWiggle, 315,
     "M4.5 19.5c1.8 0 3-1 3-2.6 0-1.4 1.1-2.4 2.5-2.4l1 1c0 1.4-1 2.5-2.4 2.5-.3 1.6-1.5 2.5-4.1 2.5Z"
         "M10.5 14.5 19 6a1.4 1.4 0 0 0-2-2l-8.5 8.5"},
    {"wifi", u"Network", Shape::kCircle, Effect::kPulse, 180,
     "M3.5 9a12 12 0 0 1 17 0 M6.3 12a8 8 0 0 1 11.4 0 M9.1 15a4 4 0 0 1 5.8 0 M12 18.4h.01"},
    {"download", u"Downloads", Shape::kSquircle, Effect::kBounce, 150,
     "M12 4v11 M7.5 10.5 12 15l4.5-4.5 M5 19.5h14"},
    {"mic", u"Voice", Shape::kSquircle, Effect::kPulse, 345,
     "M12 3.5a3 3 0 0 0-3 3V12a3 3 0 0 0 6 0V6.5a3 3 0 0 0-3-3Z"
         "M6 11.5a6 6 0 0 0 12 0 M12 17.5v3 M9 20.5h6"},
    {"drop", u"Water", Shape::kGem, Effect::kBounce, 195,
     "M12 3.5s6 6.2 6 10.5a6 6 0 0 1-12 0C6 9.7 12 3.5 12 3.5Z"},
    {"building", u"Business", Shape::kSquircle, Effect::kStagger, 225,
     "M6 20.5V4.5A1 1 0 0 1 7 3.5h10a1 1 0 0 1 1 1v16 M3.5 20.5h17"
         "M9.5 7.5h1 M13.5 7.5h1 M9.5 11.5h1 M13.5 11.5h1 M10.5 20.5v-3.5h3v3.5"},
    {"phone", u"Phone", Shape::kSquircle, Effect::kBounce, 215,
     "M7.5 3.5h9a1.5 1.5 0 0 1 1.5 1.5v14a1.5 1.5 0 0 1-1.5 1.5h-9A1.5 1.5 0 0 1 6 19V5a1.5 1.5 0 0 1 1.5-1.5Z M11 18h2"},
    {"link", u"Links", Shape::kPentagon, Effect::kDraw, 170,
     "M10 14a4 4 0 0 0 5.6 0l3-3a4 4 0 0 0-5.6-5.6l-1 1"
         "M14 10a4 4 0 0 0-5.6 0l-3 3a4 4 0 0 0 5.6 5.6l1-1"},
    {"clipboard", u"Tasks", Shape::kSquircle, Effect::kStagger, 100,
     "M9 4.5h6a1 1 0 0 1 1 1v1H8v-1a1 1 0 0 1 1-1Z"
         "M8 6.5H6.5A1.5 1.5 0 0 0 5 8v11a1.5 1.5 0 0 0 1.5 1.5h11A1.5 1.5 0 0 0 19 19V8a1.5 1.5 0 0 0-1.5-1.5H16"
         "M8.5 12h7 M8.5 15.5h5"},
    {"ai", u"AI", Shape::kBurst, Effect::kPulse, 294,
     "M0 0m10.4 3.8 1.6 4.3 4.3 1.6-4.3 1.6-1.6 4.3-1.6-4.3L4.5 9.7l4.3-1.6 1.6-4.3Z"
         "M0 0m17.4 14.2.9 2.5 2.5.9-2.5.9-.9 2.5-.9-2.5-2.5-.9 2.5-.9.9-2.5Z"},
};

}  // namespace

base::span<const WorkspaceIcon> AllWorkspaceIcons() {
  return base::span<const WorkspaceIcon>(kIcons);
}

const WorkspaceIcon* FindWorkspaceIcon(std::string_view key) {
  if (key.empty()) {
    return nullptr;
  }
  for (const WorkspaceIcon& icon : kIcons) {
    if (key == icon.key) {
      return &icon;
    }
  }
  // Not one of ours -- the caller treats the string as an emoji. That is the
  // whole backward-compatibility story: workspaces created before this set keep
  // their emoji and nothing had to be migrated.
  return nullptr;
}

const WorkspaceIcon* FindWorkspaceIcon(const std::u16string& key) {
  // Icon keys are ASCII by construction, so a non-ASCII string cannot be one --
  // and skipping the conversion for emoji (which is every legacy value) keeps
  // this off the hot path in Layout.
  if (key.empty() || !base::IsStringASCII(key)) {
    return nullptr;
  }
  return FindWorkspaceIcon(base::UTF16ToASCII(key));
}

double SpringAt(const Spring& spring, double t) {
  const double w0 = std::sqrt(spring.stiffness);
  if (spring.damping < 1.0) {
    const double wd = w0 * std::sqrt(1.0 - spring.damping * spring.damping);
    return 1.0 - std::exp(-spring.damping * w0 * t) *
                     (std::cos(wd * t) +
                      ((spring.damping * w0) / wd) * std::sin(wd * t));
  }
  return 1.0 - std::exp(-w0 * t) * (1.0 + w0 * t);
}

double SpringSettleSeconds(const Spring& spring) {
  const double w0 = std::sqrt(spring.stiffness);
  // 0.002 is the source set's settle threshold. Overdamped springs approach
  // without ringing, so they need the extra factor to look finished.
  return spring.damping < 1.0
             ? -std::log(0.002) / (spring.damping * w0)
             : (-std::log(0.002) / w0) * 1.6;
}

SkPath ShapePath(Shape shape, const gfx::RectF& bounds, float morph) {
  const float cx = bounds.CenterPoint().x();
  const float cy = bounds.CenterPoint().y();
  const float unit = std::min(bounds.width(), bounds.height()) / 2.f;
  // Overshoot past 1 is ALLOWED, and is the whole point of a spatial spring:
  // the shape rings slightly past its target before settling. It cannot escape
  // the cell, because every silhouette is peak-normalised to radius 1 -- at the
  // vertices RadiusAt is exactly 1.0, so `morph` scales nothing there and only
  // deepens the valleys between them.
  //
  // Undershoot below 0 is NOT allowed: negative morph inverts the lobes, which
  // looks like a rendering fault rather than a bounce.
  const float t = std::clamp(morph, 0.f, 1.35f);

  // A true circle when nothing has morphed yet: a 96-gon is visually identical
  // but costs more to fill, and this is the state a cell spends most of its
  // life in.
  if (t <= 0.f || shape == Shape::kCircle) {
    return SkPathBuilder().addCircle(cx, cy, unit).detach();
  }

  SkPathBuilder builder;
  for (int i = 0; i < kOutlinePoints; ++i) {
    const double a = (static_cast<double>(i) / kOutlinePoints) * kTau;
    // Interpolate the RADIUS, not two finished outlines. Every vertex keeps the
    // same angle throughout the morph and only travels in and out, so nothing
    // crosses anything else and the shape stays simple at every step.
    const double r = (1.0 - t) * 1.0 + t * RadiusAt(shape, a);
    const float px = cx + static_cast<float>(std::cos(a) * r) * unit;
    const float py = cy + static_cast<float>(std::sin(a) * r) * unit;
    if (i == 0) {
      builder.moveTo(px, py);
    } else {
      builder.lineTo(px, py);
    }
  }
  builder.close();
  return builder.detach();
}

bool IsContinuousEffect(Effect effect) {
  return effect == Effect::kBreathe || effect == Effect::kPulse ||
         effect == Effect::kOrbit;
}

base::TimeDelta EffectDuration(Effect effect) {
  // The source set's durations, unchanged -- they were tuned against these
  // exact glyphs and there is nothing to gain by re-guessing them.
  switch (effect) {
    case Effect::kBounce:
      return base::Milliseconds(520);
    case Effect::kWiggle:
      return base::Milliseconds(560);
    case Effect::kJitter:
      return base::Milliseconds(620);
    case Effect::kStagger:
      return base::Milliseconds(740);  // 520 plus the last subpath's delay.
    case Effect::kDraw:
      return base::Milliseconds(480);
    case Effect::kBreathe:
      return base::Milliseconds(1700);
    case Effect::kPulse:
      return base::Milliseconds(1300);
    case Effect::kOrbit:
      return base::Milliseconds(3400);
    case Effect::kShimmer:
      return base::Milliseconds(900);
  }
  return base::Milliseconds(500);
}

void PaintWorkspaceGlyph(gfx::Canvas* canvas,
                         const WorkspaceIcon& icon,
                         const gfx::RectF& box,
                         SkColor color,
                         float stroke_width,
                         float progress) {
  const SkPath& glyph = GlyphPath(icon.path_data);
  if (glyph.isEmpty() || box.IsEmpty()) {
    return;
  }

  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kStroke_Style);
  flags.setStrokeCap(cc::PaintFlags::kRound_Cap);
  flags.setStrokeJoin(cc::PaintFlags::kRound_Join);
  flags.setColor(color);

  const float scale = std::min(box.width(), box.height()) / kViewBox;
  canvas->Save();
  canvas->Translate(
      gfx::Vector2d(static_cast<int>(box.x()), static_cast<int>(box.y())));
  canvas->sk_canvas()->scale(scale, scale);
  // Divided by the scale so the width is in SCREEN pixels, which is the whole
  // point: a glyph scaled linearly into a 28px cell would carry a sub-pixel
  // line and turn to grey mush. This is the non-scaling-stroke the source set
  // asked for, done on the canvas instead of in CSS.
  flags.setStrokeWidth(stroke_width / scale);

  // At rest, and worth the early exit: this is the state every cell but the
  // hovered one is in, on every paint.
  if (progress < 0.f) {
    canvas->DrawPath(glyph, flags);
    canvas->Restore();
    return;
  }

  const float t = std::clamp(progress, 0.f, 1.f);
  // Everything below turns about the glyph's own centre. Inside an SVG a
  // percentage transform-origin resolves against the viewBox unless told
  // otherwise; the source set hit exactly that and had to set
  // transform-box:view-box. This constant is that fix.
  constexpr SkScalar kMid = kViewBox / 2.f;
  cc::PaintCanvas* sk = canvas->sk_canvas();

  switch (icon.effect) {
    case Effect::kBounce: {
      static constexpr Key kScale[] = {
          {0.f, 1.f}, {0.28f, 1.17f}, {0.52f, 0.95f}, {0.74f, 1.05f},
          {1.f, 1.f}};
      static constexpr Key kLift[] = {
          {0.f, 0.f}, {0.28f, -2.5f}, {0.52f, 0.5f}, {0.74f, -0.5f},
          {1.f, 0.f}};
      const float k = KeyframeAt(kScale, t);
      sk->translate(kMid, kMid);
      sk->scale(k, k);
      sk->translate(0, KeyframeAt(kLift, t));
      sk->translate(-kMid, -kMid);
      canvas->DrawPath(glyph, flags);
      break;
    }
    case Effect::kWiggle: {
      static constexpr Key kRot[] = {{0.f, 0.f},   {0.18f, -11.f},
                                     {0.38f, 9.f}, {0.58f, -5.f},
                                     {0.78f, 2.5f}, {1.f, 0.f}};
      sk->translate(kMid, kMid);
      sk->rotate(KeyframeAt(kRot, t));
      sk->translate(-kMid, -kMid);
      canvas->DrawPath(glyph, flags);
      break;
    }
    case Effect::kJitter: {
      // The axes counter-phase, so the glyph conserves area the way a physical
      // object does. That is squash and stretch, not a plain pulse.
      static constexpr Key kX[] = {{0.f, 1.f},     {0.22f, 1.20f},
                                   {0.44f, 0.87f}, {0.64f, 1.08f},
                                   {0.82f, 0.97f}, {1.f, 1.f}};
      static constexpr Key kY[] = {{0.f, 1.f},     {0.22f, 0.84f},
                                   {0.44f, 1.16f}, {0.64f, 0.94f},
                                   {0.82f, 1.03f}, {1.f, 1.f}};
      sk->translate(kMid, kMid);
      sk->scale(KeyframeAt(kX, t), KeyframeAt(kY, t));
      sk->translate(-kMid, -kMid);
      canvas->DrawPath(glyph, flags);
      break;
    }
    case Effect::kBreathe: {
      // Continuous, so it has to be SEAMLESS at the loop point. A cosine is 1
      // at both ends; a keyframe list would visibly restart.
      const float phase = 0.5f - 0.5f * std::cos(static_cast<float>(t * kTau));
      const float k = 1.f + 0.12f * phase;
      sk->translate(kMid, kMid);
      sk->scale(k, k);
      sk->translate(-kMid, -kMid);
      flags.setColor(SkColorSetA(
          color,
          static_cast<U8CPU>(SkColorGetA(color) * (0.8f + 0.2f * phase))));
      canvas->DrawPath(glyph, flags);
      break;
    }
    case Effect::kPulse: {
      const float phase = 0.5f + 0.5f * std::cos(static_cast<float>(t * kTau));
      flags.setColor(SkColorSetA(
          color,
          static_cast<U8CPU>(SkColorGetA(color) * (0.32f + 0.68f * phase))));
      canvas->DrawPath(glyph, flags);
      break;
    }
    case Effect::kDraw: {
      // Each subpath retraces itself over the same window, which is what
      // pathLength="1" bought the CSS version: one dash length normalised
      // across subpaths of very different lengths.
      for (const SkPath& part : GlyphSubpaths(icon.path_data)) {
        canvas->DrawPath(PathPrefix(part, t), flags);
      }
      break;
    }
    case Effect::kStagger: {
      const std::vector<SkPath>& parts = GlyphSubpaths(icon.path_data);
      constexpr float kStep = 55.f / 740.f;  // 55ms as a fraction of the cycle.
      constexpr float kSpan = 520.f / 740.f;
      for (size_t i = 0; i < parts.size(); ++i) {
        const float local = std::clamp((t - kStep * i) / kSpan, 0.f, 1.f);
        // A spring, so each piece overshoots slightly as it lands.
        const float eased = static_cast<float>(
            SpringAt(kSpringSpatialFast,
                     local * SpringSettleSeconds(kSpringSpatialFast)));
        const float settled = std::clamp(eased, 0.f, 1.f);
        cc::PaintFlags part_flags = flags;
        part_flags.setColor(SkColorSetA(
            color, static_cast<U8CPU>(SkColorGetA(color) * settled)));
        canvas->Save();
        sk->translate(kMid, kMid);
        const float k = 0.4f + 0.6f * eased;
        sk->scale(k, k);
        sk->rotate(-14.f * (1.f - settled));
        sk->translate(-kMid, -kMid);
        canvas->DrawPath(parts[i], part_flags);
        canvas->Restore();
      }
      break;
    }
    case Effect::kOrbit: {
      // By-layer rotation: the frame holds still and only what moves turns.
      const std::vector<SkPath>& parts = GlyphSubpaths(icon.path_data);
      for (size_t i = 0; i < parts.size(); ++i) {
        if (i == 0) {
          canvas->DrawPath(parts[i], flags);
          continue;
        }
        canvas->Save();
        sk->translate(kMid, kMid);
        sk->rotate(360.f * t);
        sk->translate(-kMid, -kMid);
        canvas->DrawPath(parts[i], flags);
        canvas->Restore();
      }
      break;
    }
    case Effect::kShimmer: {
      // A travelling band, done with a CLIP rather than a mask.
      //
      // The source set masked a gradient rect by the glyph itself. A clip reads
      // the same -- only the drawn line catches the highlight -- without a mask
      // layer, which at a 28px cell would cost more than it shows.
      canvas->DrawPath(glyph, flags);
      constexpr float kBand = 7.f;
      const float x = -kBand + t * (kViewBox + kBand * 2.f);
      canvas->Save();
      sk->clipRect(
          SkRect::MakeLTRB(x - kBand, -6.f, x + kBand, kViewBox + 6.f));
      cc::PaintFlags sheen = flags;
      // Lifted toward white rather than to a second colour: the palette has one
      // accent and this is not a use for it.
      sheen.setColor(
          color_utils::AlphaBlend(SK_ColorWHITE, color, SkAlpha{0xB0}));
      canvas->DrawPath(glyph, sheen);
      canvas->Restore();
      break;
    }
  }

  canvas->Restore();
}

}  // namespace zephyrus
