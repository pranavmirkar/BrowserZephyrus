// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#ifndef CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_REGEX_MATCHER_H_
#define CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_REGEX_MATCHER_H_

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/ref_counted.h"
#include "chrome/browser/zephyrus/adblock/adblock_filter_engine.h"
#include "url/gurl.h"

namespace re2 {
class FilteredRE2;
}

namespace zephyrus_adblock {

// The `/regex/` network rules of the filter lists (about 590 in the shipped
// ones): `/\/ads\/[0-9]+\.js/$script`, `@@/^https:\/\/ok\./`, and so on.
//
// The keyword buckets and the compiled index match patterns made of literals,
// `*` and `^`; they cannot run a regex, so these rules were dropped, and a
// tracker the lists name only by regex got through. This is the missing half.
//
// HOW. RE2 runs the regexes (linear time, so a hostile list or URL cannot make
// it backtrack), but only a few per request, and only after three cheaper
// questions have said yes:
//  1. GATE. Every regex needs one of a few literals to occur in the URL
//     (re2::FilteredRE2 works them out at build time, and the rarest set that
//     is still sufficient becomes the rule's "gates"). A scan of the URL finds
//     which gates occur in it; a rule none of whose gates does is never looked
//     at. A regex that needs no literal is tried for every request, and there
//     are very few.
//  2. TYPE. A rule scoped to $script or $doc is skipped for any other kind of
//     request before its other options are read.
//  3. OPTIONS. Party, $domain=, $to=, ... by RuleOptionsMatch, the same code the
//     engine uses for its own rules.
//
// What does not run: a regex RE2 cannot express (lookahead, back-references:
// a few dozen in the shipped lists) is counted in skipped_count() and left out,
// which is the safe side -- the request is allowed, never blocked wrongly.
// A URL over kMaxMatchedUrlBytes is not tested at all, for the same reason.
//
// Immutable once built; Match() is const and safe from any thread.
class AdblockRegexMatcher
    : public base::RefCountedThreadSafe<AdblockRegexMatcher> {
 public:
  // The longest URL (in bytes) that is matched against the regexes. Longer
  // ones are allowed through: RE2 is linear, but a megabyte URL times every
  // candidate regex is still time spent on the UI thread.
  static constexpr size_t kMaxMatchedUrlBytes = 16384;

  // Compiles the regex rules `engine` parsed.
  static scoped_refptr<const AdblockRegexMatcher> Build(
      const AdblockFilterEngine& engine);

  // Compiles the regex rules in `list_text`, which is a whole filter list: the
  // regex lines are picked out and the rest is never parsed.
  static scoped_refptr<const AdblockRegexMatcher> BuildFromList(
      std::string_view list_text);

  // Like BuildFromList, shared: the process keeps the matcher for the most
  // recent `list_key`, so every profile that asks for the same list gets the
  // same compiled regexes. Blocks while another thread builds the same one.
  // `list_key` 0 means "no key": always builds a fresh one.
  static scoped_refptr<const AdblockRegexMatcher> GetOrBuild(
      uint64_t list_key,
      std::string_view list_text);

  // For tests: forget the process's cached matcher.
  static void ResetForTesting();

  // What the regex rules say about this request.
  RegexHits Match(const GURL& url,
                  const GURL& initiator,
                  ResourceType type) const;

  size_t rule_count() const { return entries_.size(); }
  // Regex rules left out because RE2 cannot compile them.
  size_t skipped_count() const { return skipped_; }
  // Compiled rules with no literal to gate on: tried for every request.
  size_t unfiltered_count() const { return always_.size(); }
  // How many distinct gate literals the rules share between them.
  size_t gate_count() const { return gates_.size(); }

 private:
  friend class base::RefCountedThreadSafe<AdblockRegexMatcher>;
  AdblockRegexMatcher();
  ~AdblockRegexMatcher();

  void Compile(const std::vector<FilterRule>& rules);
  // Files each entry under the literals in `gates_of_entry` (a URL containing
  // any one of them wakes it); an entry with none is always tried.
  void BuildGates(
      const std::vector<std::vector<std::string>>& gates_of_entry);

  // The part of a rule that the cheap type test needs, kept apart from the
  // (large) FilterRule so the test touches 8 bytes per candidate.
  struct Quick {
    uint32_t type_mask;
    uint32_t type_not;
    // An untyped block rule never reaches a page load or a pop-up (see
    // FilterRule::pure_host), and a regex rule is never a pure host.
    bool untyped_block;
  };

  // A literal and the rules that need it.
  struct Gate {
    std::string text;  // lowercase, at least four bytes
    uint32_t first_entry;
    uint32_t entry_count;
  };
  // Gates sharing their first four bytes, found with one table probe.
  struct Slot {
    uint32_t prefix = 0;  // 0 = empty
    uint32_t first_gate = 0;
    uint32_t gate_count = 0;
  };

  // One per regex that compiled, in the order FilteredRE2 numbered them. The
  // pattern is not kept here: the compiled RE2 holds it.
  std::vector<FilterRule> entries_;
  std::vector<Quick> quick_;
  // The compiled regex of each entry. A FilteredRE2 of one regex: it owns the
  // RE2, and said which literals the regex needs when it was built.
  std::vector<std::unique_ptr<re2::FilteredRE2>> regexes_;

  std::vector<uint32_t> always_;       // entries with no gate
  std::vector<Gate> gates_;            // sorted by prefix
  std::vector<uint32_t> gate_entries_;
  std::vector<Slot> slots_;            // power-of-two sized, open addressing
  std::vector<uint64_t> prefix_bits_;  // one bit per hashed 3-byte prefix
  size_t skipped_ = 0;
};

}  // namespace zephyrus_adblock

#endif  // CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_REGEX_MATCHER_H_
