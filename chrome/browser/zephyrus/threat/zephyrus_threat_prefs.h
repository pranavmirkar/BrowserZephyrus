// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#ifndef CHROME_BROWSER_ZEPHYRUS_THREAT_ZEPHYRUS_THREAT_PREFS_H_
#define CHROME_BROWSER_ZEPHYRUS_THREAT_ZEPHYRUS_THREAT_PREFS_H_

namespace zephyrus_threat {

// Local State, a bool, off by default: ask a free filtering DNS resolver about
// the host names of sites that are not popular, so a phishing or malware page
// is warned about before it loads. Turning it on sends those host names (and
// nothing else) to the resolver; see ShieldDnsChecker. Registered in
// browser_prefs.cc with this literal (the same layering reason as the other
// zephyrus.* Local State prefs there).
inline constexpr char kShieldDnsPref[] = "zephyrus.threat.shield_dns";

}  // namespace zephyrus_threat

#endif  // CHROME_BROWSER_ZEPHYRUS_THREAT_ZEPHYRUS_THREAT_PREFS_H_
