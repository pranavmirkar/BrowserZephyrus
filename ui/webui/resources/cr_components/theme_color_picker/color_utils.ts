// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
import type {SkColor} from '//resources/mojo/skia/public/mojom/skcolor.mojom-webui.js';
import type {BrowserColorVariant} from '//resources/mojo/ui/base/mojom/themes.mojom-webui.js';

export interface Color {
  background: SkColor;
  foreground: SkColor;
  base: SkColor;
}

export const EMPTY_COLOR: Color = {
  background: {value: 0},
  foreground: {value: 0},
  base: {value: 0},
};

export const LIGHT_DEFAULT_COLOR: Color = {
  background: {value: 0xffffffff},
  foreground: {value: 0xffdee1e6},
  base: {value: 0},
};

export const DARK_DEFAULT_COLOR: Color = {
  background: {value: 0xff323639},
  foreground: {value: 0xff202124},
  base: {value: 0},
};

// Zephyrus: the DEFAULT swatch in Customize Chrome, re-hued to Pomegranate.
// These are the values the picker draws for "the browser's own colour", so
// leaving them blue showed a blue chip for a red browser -- the one swatch that
// is supposed to be a mirror of the default theme. Tones are the same stops the
// baseline ramp uses (ref_color_mixer.cc): 40 for the fill, 90 for what sits on
// it in light, and the pair inverted for dark.
export const LIGHT_BASELINE_BLUE_COLOR: Color = {
  background: {value: 0xffc6102e},
  foreground: {value: 0xffffdad6},
  base: {value: 0xffc7c7c7},
};

export const DARK_BASELINE_BLUE_COLOR: Color = {
  background: {value: 0xffffb3ad},
  foreground: {value: 0xff680112},
  base: {value: 0xff757575},
};

export const LIGHT_BASELINE_GREY_COLOR: Color = {
  background: {value: 0xff0b57d0},
  foreground: {value: 0xffe3e3e3},
  base: {value: 0xffc7c7c7},
};

export const DARK_BASELINE_GREY_COLOR: Color = {
  background: {value: 0xffa8c7fa},
  foreground: {value: 0xff474747},
  base: {value: 0xff757575},
};

export enum ColorType {
  NONE,
  DEFAULT,
  MAIN,
  CHROME,
  CUSTOM,
  GREY,
}

export interface SelectedColor {
  type: ColorType;
  chromeColor?: SkColor;
  variant?: BrowserColorVariant;
}
