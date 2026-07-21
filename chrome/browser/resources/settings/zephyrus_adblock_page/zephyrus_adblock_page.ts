// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview
 * 'settings-zephyrus-adblock-page' is the settings page for the built-in
 * Zephyrus ad blocker: master toggles, per-site allowlist, and stats. It is
 * entirely pref-backed (chrome.settingsPrivate) — no dedicated browser proxy.
 */
import 'chrome://resources/cr_elements/cr_button/cr_button.js';
import 'chrome://resources/cr_elements/cr_icon_button/cr_icon_button.js';
import 'chrome://resources/cr_elements/cr_input/cr_input.js';
import 'chrome://resources/cr_elements/cr_shared_style.css.js';
import 'chrome://resources/cr_elements/icons.html.js';
import '../controls/settings_toggle_button.js';
import '../settings_page/settings_section.js';
import '../settings_shared.css.js';

import {PrefsMixin} from '/shared/settings/prefs/prefs_mixin.js';
import {PolymerElement} from 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js';
import type {DomRepeatEvent} from 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js';

import {loadTimeData} from '../i18n_setup.js';

import {getTemplate} from './zephyrus_adblock_page.html.js';

const ALLOWLIST_PREF: string = 'zephyrus.adblock.allowlist';

const SettingsZephyrusAdblockPageElementBase = PrefsMixin(PolymerElement);

export class SettingsZephyrusAdblockPageElement extends
    SettingsZephyrusAdblockPageElementBase {
  static get is() {
    return 'settings-zephyrus-adblock-page';
  }

  static get template() {
    return getTemplate();
  }

  static get properties() {
    return {
      newDomain_: {
        type: String,
        value: '',
      },

      statsText_: {
        type: String,
        computed: 'computeStatsText_(' +
            'prefs.zephyrus.adblock.total_blocked.value, ' +
            'prefs.zephyrus.adblock.rule_count.value)',
      },

      allowlistEmpty_: {
        type: Boolean,
        computed: 'computeAllowlistEmpty_(' +
            'prefs.zephyrus.adblock.allowlist.value.*)',
      },
    };
  }

  declare private newDomain_: string;
  declare private statsText_: string;
  declare private allowlistEmpty_: boolean;

  private computeStatsText_(blocked: number, rules: number): string {
    return loadTimeData.getStringF(
        'zephyrusAdblockStatsValue', blocked || 0, rules || 0);
  }

  private computeAllowlistEmpty_(): boolean {
    const pref = this.getPref<string[]>(ALLOWLIST_PREF);
    return !pref.value || pref.value.length === 0;
  }

  // Normalizes user input to a bare host (lowercase, no scheme/path/leading dot
  // or "www."), matching the browser-side normalization.
  private normalize_(input: string): string {
    let d = input.trim().toLowerCase();
    const scheme = d.indexOf('://');
    if (scheme !== -1) {
      d = d.substring(scheme + 3);
    }
    const slash = d.indexOf('/');
    if (slash !== -1) {
      d = d.substring(0, slash);
    }
    while (d.startsWith('.')) {
      d = d.substring(1);
    }
    if (d.startsWith('www.')) {
      d = d.substring(4);
    }
    return d;
  }

  private onAddDomain_() {
    const domain = this.normalize_(this.newDomain_);
    if (domain) {
      // appendPrefListItem no-ops if the item is already present.
      this.appendPrefListItem(ALLOWLIST_PREF, domain);
    }
    this.newDomain_ = '';
  }

  private onDomainKeydown_(e: KeyboardEvent) {
    if (e.key === 'Enter') {
      this.onAddDomain_();
    }
  }

  private onRemoveDomain_(e: DomRepeatEvent<string>) {
    this.deletePrefListItem(ALLOWLIST_PREF, e.model.item);
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-zephyrus-adblock-page': SettingsZephyrusAdblockPageElement;
  }
}

customElements.define(
    SettingsZephyrusAdblockPageElement.is,
    SettingsZephyrusAdblockPageElement);
