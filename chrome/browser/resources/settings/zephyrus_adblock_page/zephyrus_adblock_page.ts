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

      blockedText_: {
        type: String,
        computed: 'computeBlockedText_(' +
            'prefs.zephyrus.adblock.total_blocked.value)',
      },

      rulesText_: {
        type: String,
        computed: 'computeRulesText_(' +
            'prefs.zephyrus.adblock.enabled.value, ' +
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
  declare private blockedText_: string;
  declare private rulesText_: string;
  declare private allowlistEmpty_: boolean;

  // Grouped for the reader's locale: "118,146", not "118146".
  private computeBlockedText_(blocked: number): string {
    return (blocked || 0).toLocaleString();
  }

  // Off says what off means instead of a rule count that is not being used.
  private computeRulesText_(enabled: boolean, rules: number): string {
    return enabled ?
        loadTimeData.getStringF(
            'zephyrusAdblockHeroRules', (rules || 0).toLocaleString()) :
        loadTimeData.getString('zephyrusAdblockHeroOffSub');
  }

  // Each chip's remove button names its site: a screen reader otherwise
  // hears a row of identical "Remove" buttons.
  private removeLabel_(domain: string): string {
    return loadTimeData.getStringF('zephyrusAdblockRemoveSite', domain);
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
