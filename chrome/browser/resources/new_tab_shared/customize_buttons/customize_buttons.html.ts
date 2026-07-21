// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {html} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import type {CustomizeButtonsElement} from './customize_buttons.js';

export function getHtml(this: CustomizeButtonsElement) {
  // clang-format off
  return html`<!--_html_template_start_-->
<!-- Zephyrus: customize buttons hidden -->
<div id="customizeButtons" hidden>
  <cr-button id="customizeButton" class="customize-button"
      @click="${this.onCustomizeClick_}" title="$i18n{customizeThisPage}"
      aria-pressed="${this.showCustomize}">
    <cr-icon class="customize-icon" slot="prefix-icon" icon="ntp:pencil">
    </cr-icon>
    <div id="customizeText" class="customize-text"
        ?hidden="${!this.showCustomizeChromeText}">
      $i18n{customizeButton}
    </div>
  </cr-button>
</div>
<!--_html_template_end_-->`;
  // clang-format on
}
