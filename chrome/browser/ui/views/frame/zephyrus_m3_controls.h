// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_M3_CONTROLS_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_M3_CONTROLS_H_

#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/view.h"

namespace gfx {
class Canvas;
class Insets;
}  // namespace gfx

// The M3 Expressive building blocks the Zephyrus setup cards are made of: a
// tonal card, a filled text field, a list row with a switch, and a section
// label. Moved here from the workspace setup card so the agent settings are
// built from the same parts rather than a second copy of them.
namespace zephyrus::m3 {

class Switch;

inline constexpr float kM3CardCorner = 24.f;
inline constexpr float kM3FieldCorner = 16.f;

// A tonal card: the container a group of controls sits on.
class Card : public views::View {
  METADATA_HEADER(Card, views::View)

 public:
  explicit Card(const gfx::Insets& padding);
  ~Card() override;

  void OnPaintBackground(gfx::Canvas* canvas) override;
};

// The editable text inside a FilledField; tells the field to repaint its
// focus outline.
class FieldText : public views::Textfield {
  METADATA_HEADER(FieldText, views::Textfield)

 public:
  explicit FieldText(views::View* owner);
  ~FieldText() override;

  void OnFocus() override;
  void OnBlur() override;

 private:
  raw_ptr<views::View> owner_;
};

// An M3 filled text field, rounded the Expressive way: a soft container, a
// small label inside it, and a 2dp primary outline while focused.
class FilledField : public views::View {
  METADATA_HEADER(FilledField, views::View)

 public:
  FilledField(std::u16string label, int width);
  ~FilledField() override;

  views::Textfield* field() { return field_; }

  // Editable or not. A disabled field stays on screen, dimmed, so it reads as
  // "switched off by something else" rather than missing.
  void SetInputEnabled(bool enabled);

  void OnPaintBackground(gfx::Canvas* canvas) override;

 private:
  const std::u16string label_;
  raw_ptr<FieldText> field_ = nullptr;
};

// A list row: headline + supporting text, and a trailing switch. Clicking
// anywhere on the row toggles it, as M3 list items do.
class SwitchRow : public views::Button {
  METADATA_HEADER(SwitchRow, views::Button)

 public:
  SwitchRow(std::u16string headline, std::u16string supporting, bool on);
  ~SwitchRow() override;

  bool is_on() const;
  // Sets the switch from code. The change callback is NOT run: whoever sets it
  // already knows.
  void SetOn(bool on);
  void set_on_change(base::RepeatingClosure on_change);

  void OnThemeChanged() override;

 private:
  // A press on the row body: flip the switch, which then reports it.
  void Toggle();
  void Changed();

  raw_ptr<views::Label> headline_ = nullptr;
  raw_ptr<views::Label> supporting_ = nullptr;
  raw_ptr<Switch> switch_ = nullptr;
  base::RepeatingClosure on_change_;
};

class SectionLabel : public views::Label {
  METADATA_HEADER(SectionLabel, views::Label)

 public:
  explicit SectionLabel(const std::u16string& text);
  ~SectionLabel() override;

  void OnThemeChanged() override;
};

}  // namespace zephyrus::m3

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_M3_CONTROLS_H_
