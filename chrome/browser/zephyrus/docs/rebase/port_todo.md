
## chrome/browser/ui/views/extensions/extensions_menu_coordinator.cc: upstream added a local `arrow` (GetPopupArrow()); we always use TOP_CENTER, so delete the now-unused `arrow` computation above the bubble_delegate line (unused-variable is an error).

## chrome/browser/ui/browser.cc (hunk 1): upstream moved or removed this code; re-apply these Zephyrus edits where it lives now

```diff
--- base-151
+++ zephyrus
@@ -392,4 +392,9 @@
 
 void Browser::UpdateTargetURL(WebContents* source, const GURL& url) {
+  // Zephyrus Glance needs to know which link the pointer is over: that is what
+  // an Alt+click opens.
+  if (BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(this)) {
+    browser_view->OnZephyrusTargetURL(source, url);
+  }
   std::vector<StatusBubble*> status_bubbles = GetStatusBubbles();
   for (StatusBubble* status_bubble : status_bubbles) {
```

## chrome/browser/ui/browser.cc (hunk 0): upstream moved or removed this code; re-apply these Zephyrus edits where it lives now

```diff
--- base-151
+++ zephyrus
@@ -27,4 +27,8 @@
     return;
   }
+
+  // Zephyrus: from here the window really is going away, so TabStripEmpty()
+  // must let it close instead of holding it open on the empty state.
+  zephyrus_window_closing_ = true;
 
   // Don't use GetForProfileIfExisting here, we want to force creation of the
```

## chrome/browser/ui/views/location_bar/location_bar_view.cc (hunk 1): upstream moved or removed this code; re-apply these Zephyrus edits where it lives now

```diff
--- base-151
+++ zephyrus
@@ -1,6 +1,20 @@
-  if (intent_chip_) {
-    int intra_item_padding = kIntentChipIntraItemPadding;
-    add_trailing_decoration(intent_chip_, intra_item_padding,
-                            /*edge_padding=*/trailing_decorations_edge_padding);
+  // Zephyrus: the search-engine mark, at the far right of the field. Added
+  // FIRST because trailing decorations are placed right-to-left in the order
+  // they are added, so this is the outermost one.
+  //
+  // It is always present rather than only while editing: someone looking at a
+  // page should be able to see and change which engine the next search will use
+  // without first having to focus the omnibox. At 24dp it fits at every width
+  // the field can be, which the old named chip did not -- that one hid itself
+  // below a 260dp field and took the leading edge with it.
+  if (zephyrus_engine_pill_) {
+    zephyrus_engine_pill_->SetVisible(true);
+    // Concentric with the trailing cap (Rule 2): the mark's circle shares the
+    // cap's centre, so its inset from the edge is the cap radius minus its
+    // own. A fixed 6dp left it off-centre in a 26dp field.
+    const int cap_inset = std::max(
+        0, height() / 2 - zephyrus_engine_pill_->GetPreferredSize().width() / 2);
+    add_trailing_decoration(zephyrus_engine_pill_, /*intra_item_padding=*/6,
+                            /*edge_padding=*/cap_inset);
   }
 
```

## chrome/browser/ui/views/location_bar/location_bar_view.cc (hunk 0): upstream moved or removed this code; re-apply these Zephyrus edits where it lives now

```diff
--- base-151
+++ zephyrus
@@ -17,16 +17,36 @@
           ? 4
           : trailing_decorations_edge_padding;
-  add_trailing_decoration(page_action_icon_container_,
-                          /*intra_item_padding=*/0,
-                          /*edge_padding=*/
-                          info.is_aim_last_visible_page_action
-                              ? kTrailingEdgePaddingForAim
-                              : kTrailingEdgePaddingForNonAim);
-  add_trailing_decoration(page_action_container_,
-                          /*intra_item_padding=*/0,
-                          /*edge_padding=*/trailing_decorations_edge_padding);
-  add_trailing_decoration(ai_mode_hint_label_, /*intra_item_padding=*/0,
-                          /*edge_padding=*/trailing_decorations_edge_padding);
-  for (ContentSettingImageView* view : base::Reversed(content_setting_views_)) {
-    int intra_item_padding = kContentSettingIntraItemPadding;
-    add_trailing_decoration(view, intra_item_padding,
+  // Zephyrus: keep the search bar clean — strip every trailing icon (page
+  // actions incl. PWA install, cookie controls, the bookmark star, content
+  // settings, intent chip, AI-mode hint). Only the leading magnifier and the
+  // text remain; the transient "clear text" button stays while editing.
+  constexpr bool kZephyrusCleanSearchBar = true;
+  if (kZephyrusCleanSearchBar) {
+    // Actually hide the views — skipping the decoration alone leaves them
+    // visible at stale bounds (they drifted left of the pill). Their
+    // controllers may re-show them on state changes, but Layout runs after,
+    // so hiding here every pass keeps the bar clean.
+    auto hide = [](views::View* v) {
+      if (v) {
+        v->SetVisible(false);
+      }
+    };
+    hide(page_action_icon_container_);
+    hide(page_action_container_);
+    hide(ai_mode_hint_label_);
+    hide(intent_chip_);
+    for (ContentSettingImageView* view : content_setting_views_) {
+      hide(view);
+    }
+  }
+  if (!kZephyrusCleanSearchBar) {
+    add_trailing_decoration(page_action_icon_container_,
+                            /*intra_item_padding=*/0,
+                            /*edge_padding=*/
+                            info.is_aim_last_visible_page_action
+                                ? kTrailingEdgePaddingForAim
+                                : kTrailingEdgePaddingForNonAim);
+    add_trailing_decoration(page_action_container_,
+                            /*intra_item_padding=*/0,
+                            /*edge_padding=*/trailing_decorations_edge_padding);
+    add_trailing_decoration(ai_mode_hint_label_, /*intra_item_padding=*/0,
```

## chrome/browser/ui/webui/history/history_ui.cc (hunk 0): upstream moved or removed this code; re-apply these Zephyrus edits where it lives now

```diff
--- base-151
+++ zephyrus
@@ -29,8 +29,20 @@
     mojo::PendingRemote<history_clusters::mojom::Page> page,
     mojo::PendingReceiver<history_clusters::mojom::PageHandler> receiver) {
-  history_clusters_handler_ =
-      std::make_unique<history_clusters::HistoryClustersHandler>(
-          std::move(receiver), std::move(page), Profile::FromWebUI(web_ui()),
-          web_ui()->GetWebContents(),
-          // HistoryUI should always be in a tab. Look it up unconditionally.
-          tabs::TabInterface::GetFromContents(web_ui()->GetWebContents()));
+  // Zephyrus: chrome://history is not always in a tab — the centralized
+  // settings popup hosts it in a plain WebView. Upstream looked the tab up
+  // unconditionally (GetFromContents CHECKs), crashing the browser the moment
+  // the "By group" view bound this handler. Fall back to the window-scoped
+  // handler via the embedding context the popup registers.
+  content::WebContents* web_contents = web_ui()->GetWebContents();
+  if (tabs::TabInterface* tab =
+          tabs::TabInterface::MaybeGetFromContents(web_contents)) {
+    history_clusters_handler_ =
+        std::make_unique<history_clusters::HistoryClustersHandler>(
+            std::move(receiver), std::move(page), Profile::FromWebUI(web_ui()),
+            web_contents, tab);
+  } else {
+    history_clusters_handler_ =
+        std::make_unique<history_clusters::HistoryClustersHandler>(
+            std::move(receiver), std::move(page), Profile::FromWebUI(web_ui()),
+            web_contents, webui::GetBrowserWindowInterface(web_contents));
+  }
```

## chrome/app/settings_strings.grdp (hunk 0): upstream moved or removed this code; re-apply these Zephyrus edits where it lives now

```diff
--- base-151
+++ zephyrus
@@ -53,5 +53,5 @@
     <!-- Where possible, these strings have been unified on Desktop & Android in privacy_sandox_strings.grdp. The strings here either only appear on one platform, or have platform specific requirements, e.g. the format of placeholders, and so cannot be unified. For the latter case, they have corresponding _CANONICAL representations used when consent is recorded on either platform, and so they must stay in sync -->
     <message name="IDS_SETTINGS_TOPICS_PAGE_CURRENT_TOPICS_REGION_A11Y_DESCRIPTION" desc="An explanation read by screen readers that describes the list of topics.">
-      List of topics estimated by Chrome based on your recent browsing history
+      List of topics estimated by Zephyrus based on your recent browsing history
     </message>
     <message name="IDS_SETTINGS_TOPICS_PAGE_BLOCKED_TOPICS_REGION_A11Y_DESCRIPTION" desc="Read by screen readers to describe the list of blocked topics.">
@@ -75,5 +75,5 @@
     </message>
     <message name="IDS_SETTINGS_FLEDGE_PAGE_CURRENT_SITES_DESCRIPTION" desc="A paragraph beneath the 'Sites' label. It explains how users can control the sites that appear in the list. * 'block sites': When a user blocks a site, that site may no longer save info with Chrome about the user's visit. As the user continues to browse, they will not see ads suggested by the blocked site. * 'auto-deletes': We included the 'auto' to reinforce that this is part of a system and the deletion is done regularly.">
-      You can block sites you don't want. Chrome also auto-deletes sites from the list that are older than 30 days.
+      You can block sites you don't want. Zephyrus also auto-deletes sites from the list that are older than 30 days.
     </message>
     <message name="IDS_SETTINGS_FLEDGE_PAGE_CURRENT_SITES_DESCRIPTION_LEARN_MORE" desc="A button label that opens a dialog box allowing the user to learn more about the Site-suggested ads setting.">
@@ -120,5 +120,5 @@
     </message>
     <message name="IDS_SETTINGS_FLEDGE_PAGE_LEARN_MORE_BULLET_1" desc="1 of 3 paragraphs on the Learn more page for Site-suggested ads. * 'It's common...' this sentence is intended to be reassuring. Many sites the user visits are retaining some information about the user, such as their preferred language if the site has a language picker. If cookies are allowed, this info might be stored with a first-party cookie on the user's device. Sites can also store limited information with Chrome.">
-      It's common for sites you visit to remember things you're interested in, to personalize your experience. Sites can also store information with Chrome about your interests.
+      It's common for sites you visit to remember things you're interested in, to personalize your experience. Sites can also store information with Zephyrus about your interests.
     </message>
     <message name="IDS_SETTINGS_FLEDGE_PAGE_LEARN_MORE_BULLET_2" desc="2 of 3 paragraphs on the Learn more page for Site-suggested ads.">
@@ -126,5 +126,5 @@
     </message>
     <message name="IDS_SETTINGS_FLEDGE_PAGE_LEARN_MORE_BULLET_3" desc="3 of 3 paragraphs on the Learn more page for Site-suggested ads. * 'Auto-deletes': This could just read 'Chrome deletes...' but we include the 'auto' to suggest that the deletion happens regularly and as part of the system. * 'A site you visit again...': We want to reassure the user. We just told them sites are automatically deleted. And yet, a user might see the same site in the list month after month if they visit that site regularly.">
-      Chrome auto-deletes sites that are older than 30 days. A site you visit again might reappear on the list. Or you can block a site from suggesting ads for you. Learn more about <ph name="BEGIN_LINK">&lt;a href="$1" target="_blank"&gt;</ph>managing your ad privacy in Chrome.<ph name="END_LINK">&lt;/a&gt;</ph>
+      Zephyrus auto-deletes sites that are older than 30 days. A site you visit again might reappear on the list. Or you can block a site from suggesting ads for you. Learn more about <ph name="BEGIN_LINK">&lt;a href="$1" target="_blank"&gt;</ph>managing your ad privacy in Zephyrus.<ph name="END_LINK">&lt;/a&gt;</ph>
     </message>
     <message name="IDS_SETTINGS_FLEDGE_PAGE_CURRENT_SITES_DESCRIPTION_LEARN_MORE_A11Y_LABEL" desc="Text read by screen readers to help users distinguish this 'Learn more' link from others that might get added to this page.">
@@ -155,4 +155,4 @@
     </message>
     <message name="IDS_SETTINGS_AD_MEASUREMENT_PAGE_CONSIDER_BULLET_2" desc="2 of 3 bullets in the 'Things to consider' section that describes the Ad measurement setting.">
-      Chrome limits the total amount of data that sites can share through the browser to measure ad performance
+      Zephyrus limits the total amount of data that sites can share through the browser to measure ad performance
     </message>
```

## chrome/browser/search/search.cc (hunk 0): upstream moved or removed this code; re-apply these Zephyrus edits where it lives now

```diff
--- base-151
+++ zephyrus
@@ -1,11 +1,15 @@
-#if BUILDFLAG(IS_ANDROID)
+#if !BUILDFLAG(IS_ANDROID)
+    // Zephyrus never hands the new-tab surface to the default search engine.
+    // Stock Chromium sends non-Google users to the provider's own new-tab page
+    // (DuckDuckGo's, Bing's, ...) or to the third-party NTP; both are exactly
+    // the branded page Zephyrus exists to avoid.
+    //
+    // So every route to a new tab -- Ctrl+T, the toolbar button, session
+    // restore, startup pages, Home -- lands on Chromium's own New Tab Page,
+    // whatever the default engine is. That page is also where the browser's
+    // theme is customised, so this is what keeps Customize Chrome reachable on
+    // a DuckDuckGo profile.
+    return NewTabURLDetails(chrome::ChromeUINewTabPageURLAsGURL(),
+                            NEW_TAB_URL_VALID);
+#else
     const GURL local_url;
-#else
-    const bool default_is_google = DefaultSearchProviderIsGoogle(profile);
-    const GURL local_url(default_is_google
-                             ? chrome::ChromeUINewTabPageURLAsGURL()
-                             : GURL(chrome::kChromeUINewTabPageThirdPartyURL));
-    if (default_is_google) {
-      return NewTabURLDetails(local_url, NEW_TAB_URL_VALID);
-    }
-#endif
```

## ui/native_theme/features/native_theme_features.cc (hunk 1): upstream moved or removed this code; re-apply these Zephyrus edits where it lives now

```diff
--- base-151
+++ zephyrus
@@ -1,6 +1,2 @@
-#else
-constexpr base::FeatureState kOverlayScrollbarFeatureState =
-    base::FEATURE_DISABLED_BY_DEFAULT;
-#endif
 // Enables or disables overlay scrollbars in Blink (i.e. web content) on Aura
 // or Linux.  The status of native UI overlay scrollbars is determined in
```

## ui/native_theme/features/native_theme_features.cc (hunk 0): upstream moved or removed this code; re-apply these Zephyrus edits where it lives now

```diff
--- base-151
+++ zephyrus
@@ -1 +1,3 @@
-#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_FUCHSIA) || BUILDFLAG(IS_IOS)
+// Zephyrus: enable overlay scrollbars on all platforms. Combined with Fluent
+// scrollbars (on by default on Win/Linux) this yields the modern thin,
+// auto-hiding Fluent overlay scrollbars instead of the legacy thick ones.
```
