// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/controls/menu/menu_config.h"

#include <windows.h>  // Must come before other Windows system headers.

#include <Vssym32.h>

#include "ui/gfx/system_fonts_win.h"

namespace views {

void MenuConfig::InitPlatform() {
  context_menu_font_list = font_list =
      gfx::FontList(gfx::win::GetSystemFont(gfx::win::SystemFont::kMenu));

  BOOL show_cues;
  show_mnemonics =
      (SystemParametersInfo(SPI_GETKEYBOARDCUES, 0, &show_cues, 0) &&
       show_cues == TRUE);

  SystemParametersInfo(SPI_GETMENUSHOWDELAY, 0, &show_delay, 0);

  // Zephyrus menu geometry, taken from the "macos-context-menu" component in
  // the Zephyrus Figma (node 133:297). Colours are NOT from the design — those
  // come from the browser's own dark theme (see the menu block in
  // chrome_color_mixer.cc); only the metrics and type are the designer's.
  //
  //   panel   radius 10, 5px padding, 1px border
  //   item    26px tall, radius 5, 10px horizontal padding, 4px vertical
  //   divider 1px, inset 10px from each edge
  //   text    13px label / 12px accelerator
  // 13px label type. InitPlatform() above sets the Windows system menu font
  // (Segoe UI ~12pt / 16px), which is what made every row taller and wider than
  // the design — the metrics below are meaningless until the font matches.
  // Inter is the design's family; Segoe UI is the fallback when it is absent.
  context_menu_font_list = font_list = gfx::FontList("Inter, Segoe UI, 13px");

  // 8, not the design's 10: DWM rounds the menu window at the system radius
  // (8dip), and a 10dip fill inside an 8dip clip gets its corners shaved.
  corner_radius = 8;
  item_corner_radius = 5;
  item_horizontal_padding = 10;
  item_vertical_margin = 4;

  // 26px rows. Height is font + margins, then floored by this minimum, so the
  // floor has to be set too or short rows spring back to the platform default.
  minimum_text_item_height = 26;
  minimum_container_item_height = 26;

  // 5px panel padding on all sides. This is also what insets the hover
  // highlight from the panel edge — the design's highlight is a rounded pill
  // floating inside the panel, not a full-bleed band across it.
  menu_horizontal_border_size = 5;
  rounded_menu_vertical_border_size = 5;

  // A hairline divider with breathing room, rather than Windows' tall band:
  // 3px above + 1px line + 3px below = the design's 4px group gap either side.
  separator_height = 7;
  separator_upper_height = 3;
  separator_lower_height = 3;
  separator_spacing_height = 3;
  separator_thickness = 1;
  separator_horizontal_border_padding = 10;

  use_bubble_border = corner_radius > 0;
}

}  // namespace views
