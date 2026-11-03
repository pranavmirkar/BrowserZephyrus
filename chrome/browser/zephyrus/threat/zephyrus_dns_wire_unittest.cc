// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#include "chrome/browser/zephyrus/threat/zephyrus_dns_wire.h"

#include <stdint.h>

#include <string>
#include <vector>

#include "base/containers/span.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/browser/zephyrus/threat/zephyrus_dns_wire_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus_threat {
namespace {

using testing_util::ARecord;
using testing_util::CleanReply;
using testing_util::FilteredReply;
using testing_util::OptWithEde;
using testing_util::Reply;
using testing_util::SinkholeReply;

constexpr char kHost[] = "wquie-eiiow201113.pages.dev";

std::vector<uint8_t> Hex(const std::string& hex) {
  std::vector<uint8_t> out;
  EXPECT_TRUE(base::HexStringToBytes(hex, &out));
  return out;
}

// Answers captured from the real services (2026-11-03). Quad9 for a name it filters:
// NXDOMAIN, one OPT record, EDE 17.
std::vector<uint8_t> RealQuad9Filtered() {
  return Hex(
      "0000810300010000000000011177717569652d6569696f773230313131330570616765730364"
      "6576000001000100002904d0000000000006000f00020011");
}
// Quad9 for example.com.
std::vector<uint8_t> RealQuad9Clean() {
  return Hex(
      "000081800001000200000001076578616d706c6503636f6d0000010001c00c00010001000000"
      "580004ac4293f3c00c000100010000005800046814179a0000290200000000000000");
}

TEST(ZephyrusDnsWireTest, TheQueryIsAnRfc8484AQuestionWithAnOptRecord) {
  const std::vector<uint8_t> q = BuildDnsQuery("example.com");
  EXPECT_EQ(base::HexEncode(q),
            "000001000001000000000001"                    // id 0, RD, 1 question, 1 additional
            "076578616D706C6503636F6D00" "00010001"       // example.com A IN
            "00" "0029" "04D0" "00000000" "0000");        // OPT, 1232, no options
  // Case does not matter to a DNS name; the query is always lower case.
  EXPECT_EQ(BuildDnsQuery("EXAMPLE.Com"), q);
}

TEST(ZephyrusDnsWireTest, NamesThatCannotBeAskedAboutBuildNothing) {
  for (const char* bad : {"", ".", ".a.com", "a..com", "a.com.", "a b.com", "a/b.com",
                          "a:80.com", "caf\xC3\xA9.com"}) {
    EXPECT_TRUE(BuildDnsQuery(std::string_view(bad)).empty()) << bad;
  }
  EXPECT_TRUE(BuildDnsQuery(std::string_view("a\0b.com", 7)).empty());
  EXPECT_TRUE(BuildDnsQuery(std::string(64, 'a') + ".com").empty());
  EXPECT_FALSE(BuildDnsQuery(std::string(63, 'a') + ".com").empty());
  EXPECT_TRUE(BuildDnsQuery(std::string(254, 'a')).empty());
  EXPECT_FALSE(BuildDnsQuery("under_score.example.com").empty());
  EXPECT_FALSE(BuildDnsQuery("xn--bcher-kva.example").empty());
}

TEST(ZephyrusDnsWireTest, RealAnswersFromTheServices) {
  EXPECT_EQ(ParseDnsAnswer(RealQuad9Filtered(), kHost), WireAnswer::kBlocked);
  EXPECT_EQ(ParseDnsAnswer(RealQuad9Clean(), "example.com"), WireAnswer::kClean);
  // The same bytes are not an answer about a different name.
  EXPECT_EQ(ParseDnsAnswer(RealQuad9Filtered(), "example.com"), WireAnswer::kUnknown);
  EXPECT_EQ(ParseDnsAnswer(RealQuad9Clean(), kHost), WireAnswer::kUnknown);
}

TEST(ZephyrusDnsWireTest, TheGeneratedAnswersMatchTheRealOnes) {
  // The test helpers must build what resolvers really send, or the other tests prove
  // nothing.
  EXPECT_EQ(FilteredReply(kHost), RealQuad9Filtered());
}

TEST(ZephyrusDnsWireTest, ASinkholeAddressIsABlock) {
  EXPECT_EQ(ParseDnsAnswer(SinkholeReply(kHost), kHost), WireAnswer::kBlocked);
  EXPECT_EQ(ParseDnsAnswer(CleanReply(kHost), kHost), WireAnswer::kClean);
  // Only an A record counts, and only in the answer section.
  std::vector<uint8_t> aaaa = {0xC0, 0x0C, 0x00, 0x1C, 0x00, 0x01, 0x00, 0x00,
                               0x00, 0x3C, 0x00, 0x10};
  aaaa.insert(aaaa.end(), 16, 0);
  EXPECT_EQ(ParseDnsAnswer(Reply(kHost, 0, 1, 0, 0, aaaa), kHost), WireAnswer::kClean);
  EXPECT_EQ(ParseDnsAnswer(Reply(kHost, 0, 0, 1, 0, ARecord(0, 0, 0, 0)), kHost),
            WireAnswer::kClean);
  EXPECT_EQ(ParseDnsAnswer(Reply(kHost, 0, 1, 0, 0, ARecord(0, 0, 0, 1)), kHost),
            WireAnswer::kClean);
}

TEST(ZephyrusDnsWireTest, OnlyTheRefusalReasonsAreABlock) {
  for (const uint16_t rcode : {0, 3}) {
    for (const uint16_t info : {15, 16, 17}) {
      EXPECT_EQ(ParseDnsAnswer(Reply(kHost, rcode, 0, 0, 1, OptWithEde(info)), kHost),
                WireAnswer::kBlocked)
          << rcode << "/" << info;
    }
    // Other errors (Other, Stale Answer, Not Ready, Prohibited, Network Error) are
    // not a verdict on the name.
    for (const uint16_t info : {0, 3, 14, 18, 23}) {
      const WireAnswer got =
          ParseDnsAnswer(Reply(kHost, rcode, 0, 0, 1, OptWithEde(info)), kHost);
      EXPECT_EQ(got, rcode == 0 ? WireAnswer::kClean : WireAnswer::kUnknown)
          << rcode << "/" << info;
    }
  }
}

TEST(ZephyrusDnsWireTest, ABareNxdomainOrAServerErrorIsNotABlock) {
  EXPECT_EQ(ParseDnsAnswer(Reply(kHost, 3, 0, 0, 0, {}), kHost), WireAnswer::kUnknown);
  for (const uint16_t rcode : {1, 2, 4, 5}) {
    EXPECT_EQ(ParseDnsAnswer(Reply(kHost, rcode, 0, 0, 1, OptWithEde(17)), kHost),
              WireAnswer::kUnknown)
        << rcode;
  }
}

TEST(ZephyrusDnsWireTest, ABadHeaderIsRefused) {
  const std::vector<uint8_t> good = FilteredReply(kHost);
  ASSERT_EQ(ParseDnsAnswer(good, kHost), WireAnswer::kBlocked);
  auto with = [&](size_t i, uint8_t v) {
    std::vector<uint8_t> p = good;
    p[i] = v;
    return ParseDnsAnswer(p, kHost);
  };
  EXPECT_EQ(with(0, 1), WireAnswer::kUnknown);      // id must be 0
  EXPECT_EQ(with(1, 1), WireAnswer::kUnknown);
  EXPECT_EQ(with(2, 0x01), WireAnswer::kUnknown);   // not a response
  EXPECT_EQ(with(2, 0x89), WireAnswer::kUnknown);   // opcode not 0
  EXPECT_EQ(with(2, 0x83), WireAnswer::kUnknown);   // truncated
  EXPECT_EQ(with(5, 0), WireAnswer::kUnknown);      // no question
  EXPECT_EQ(with(5, 2), WireAnswer::kUnknown);      // two questions
  EXPECT_EQ(with(12, 0x02), WireAnswer::kUnknown);  // the echoed question differs
}

TEST(ZephyrusDnsWireTest, HostileRecordsAreRefusedNotFollowed) {
  // A record count the packet cannot hold.
  EXPECT_EQ(ParseDnsAnswer(Reply(kHost, 0, 0xFFFF, 0, 0, ARecord(1, 2, 3, 4)), kHost),
            WireAnswer::kUnknown);
  EXPECT_EQ(ParseDnsAnswer(Reply(kHost, 0, 40, 0, 0, {}), kHost), WireAnswer::kUnknown);
  // A record that claims more data than the packet has.
  std::vector<uint8_t> long_rdata = ARecord(1, 2, 3, 4);
  long_rdata[11] = 0xFF;
  EXPECT_EQ(ParseDnsAnswer(Reply(kHost, 0, 1, 0, 0, long_rdata), kHost), WireAnswer::kUnknown);
  // A name using the reserved label types.
  for (const uint8_t reserved : {0x40, 0x80}) {
    std::vector<uint8_t> r = ARecord(1, 2, 3, 4);
    r[0] = reserved;
    EXPECT_EQ(ParseDnsAnswer(Reply(kHost, 0, 1, 0, 0, r), kHost), WireAnswer::kUnknown);
  }
  // A label running past the end.
  EXPECT_EQ(ParseDnsAnswer(Reply(kHost, 0, 1, 0, 0, {0x3F, 'a', 'b'}), kHost),
            WireAnswer::kUnknown);
  // An OPT whose option is longer than its data.
  std::vector<uint8_t> opt = OptWithEde(17);
  opt[14] = 0x20;  // option length
  EXPECT_EQ(ParseDnsAnswer(Reply(kHost, 3, 0, 0, 1, opt), kHost), WireAnswer::kUnknown);
  // An EDE option with no info code.
  std::vector<uint8_t> short_ede = {0x00, 0x00, 0x29, 0x04, 0xD0, 0x00, 0x00, 0x00, 0x00,
                                    0x00, 0x04, 0x00, 0x0F, 0x00, 0x00};
  EXPECT_EQ(ParseDnsAnswer(Reply(kHost, 3, 0, 0, 1, short_ede), kHost), WireAnswer::kUnknown);
  // An OPT record that is not at the root, or sits in the answer section, is not trusted.
  std::vector<uint8_t> named_opt = OptWithEde(17);
  named_opt.erase(named_opt.begin());
  named_opt.insert(named_opt.begin(), {0xC0, 0x0C});
  EXPECT_NE(ParseDnsAnswer(Reply(kHost, 3, 0, 0, 1, named_opt), kHost), WireAnswer::kBlocked);
  EXPECT_NE(ParseDnsAnswer(Reply(kHost, 3, 1, 0, 0, OptWithEde(17)), kHost),
            WireAnswer::kBlocked);
}

TEST(ZephyrusDnsWireTest, ACutOffAnswerIsNeverABlock) {
  for (const std::vector<uint8_t>& whole :
       {FilteredReply(kHost), SinkholeReply(kHost), CleanReply(kHost)}) {
    for (size_t n = 0; n < whole.size(); ++n) {
      const std::vector<uint8_t> cut(whole.begin(), whole.begin() + n);
      EXPECT_EQ(ParseDnsAnswer(cut, kHost), WireAnswer::kUnknown) << n;
    }
  }
}

TEST(ZephyrusDnsWireTest, EverySingleByteDamageIsSurvivedAndNeverMakesCleanBlocked) {
  const std::vector<uint8_t> clean = CleanReply(kHost);
  for (size_t i = 0; i < clean.size(); ++i) {
    for (int v = 0; v < 256; ++v) {
      std::vector<uint8_t> p = clean;
      p[i] = static_cast<uint8_t>(v);
      EXPECT_NE(ParseDnsAnswer(p, kHost), WireAnswer::kBlocked) << i << "/" << v;
    }
  }
  // And damage to a blocked answer is survived (the answer may become anything).
  const std::vector<uint8_t> blocked = FilteredReply(kHost);
  for (size_t i = 0; i < blocked.size(); ++i) {
    for (int v = 0; v < 256; ++v) {
      std::vector<uint8_t> p = blocked;
      p[i] = static_cast<uint8_t>(v);
      ParseDnsAnswer(p, kHost);
    }
  }
}

}  // namespace
}  // namespace zephyrus_threat
