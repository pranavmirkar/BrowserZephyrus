// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <vector>

#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/strings/string_number_conversions.h"
#include "base/logging.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/time/time.h"
#include "base/test/task_environment.h"
#include "base/path_service.h"
#include "chrome/browser/zephyrus/agent/hands_free.h"
#include "chrome/browser/zephyrus/agent/phrase_recorder.h"
#include "chrome/browser/zephyrus/agent/voice_library.h"
#include "chrome/browser/zephyrus/agent/voice_dsp.h"
#include "chrome/browser/zephyrus/agent/voice_lock.h"
#include "components/os_crypt/async/browser/test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

// These run on real synthesised speech: two Windows voices (Zira, the "owner",
// and David, another speaker) saying "Hey Zep" and sentences, as 16 kHz mono
// WAV files. See testdata/voice/README.md for how they were made.
namespace zephyrus::agent::voice {
namespace {

std::vector<int16_t> LoadWav(const std::string& name) {
  base::FilePath root;
  EXPECT_TRUE(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  const base::FilePath path = root.AppendASCII("chrome")
                                  .AppendASCII("browser")
                                  .AppendASCII("zephyrus")
                                  .AppendASCII("agent")
                                  .AppendASCII("testdata")
                                  .AppendASCII("voice")
                                  .AppendASCII(name);
  std::string bytes;
  EXPECT_TRUE(base::ReadFileToString(path, &bytes)) << path;
  // Find the "data" chunk after the 12-byte RIFF header.
  size_t pos = 12;
  while (pos + 8 <= bytes.size()) {
    const std::string id = bytes.substr(pos, 4);
    const uint32_t size =
        static_cast<uint8_t>(bytes[pos + 4]) |
        (static_cast<uint8_t>(bytes[pos + 5]) << 8) |
        (static_cast<uint8_t>(bytes[pos + 6]) << 16) |
        (static_cast<uint32_t>(static_cast<uint8_t>(bytes[pos + 7])) << 24);
    if (id == "data") {
      std::vector<int16_t> pcm(size / 2);
      for (size_t i = 0; i < pcm.size(); ++i) {
        pcm[i] = static_cast<int16_t>(
            static_cast<uint8_t>(bytes[pos + 8 + 2 * i]) |
            (static_cast<uint8_t>(bytes[pos + 9 + 2 * i]) << 8));
      }
      return pcm;
    }
    pos += 8 + size;
  }
  ADD_FAILURE() << "no data chunk in " << name;
  return {};
}

// A quiet room: faint noise, from a fixed seed so every run hears the same.
std::vector<int16_t> Room(double seconds, uint32_t seed = 1) {
  std::vector<int16_t> out(static_cast<size_t>(seconds * kSampleRate));
  uint32_t state = seed;
  for (int16_t& s : out) {
    state = state * 1664525u + 1013904223u;
    s = static_cast<int16_t>(static_cast<int>((state >> 16) % 41) - 20);  // ~-64 dBFS
  }
  return out;
}

std::vector<int16_t> Join(std::initializer_list<std::vector<int16_t>> parts) {
  std::vector<int16_t> out;
  for (const auto& p : parts) {
    out.insert(out.end(), p.begin(), p.end());
  }
  return out;
}

// Enrols a voice from five "Hey Zep" recordings by `who` ("zira" or "david").
VoiceProfile Enrol(const std::string& who, const std::string& name) {
  VoiceProfile profile;
  profile.id = who;
  profile.name = name;
  for (int i = 0; i < 5; ++i) {
    SampleCheck check = AnalyseEnrollmentSample(
        LoadWav(who + "_wake_" + std::to_string(i) + ".wav"),
        profile.templates);
    EXPECT_EQ(check.problem, SampleProblem::kNone) << who << i;
    profile.templates.push_back(std::move(check.sample));
  }
  EXPECT_TRUE(profile.Prepare());
  return profile;
}

// ---- The maths ---------------------------------------------------------------

TEST(VoiceDsp, FramesAndSpeechSpanFollowTheSpeech) {
  const std::vector<float> samples = PcmToFloat(LoadWav("zira_wake_0.wav"));
  const Frames frames = ExtractFrames(samples);
  EXPECT_EQ(frames.features.size(), 178u);  // 10 ms hop over ~1.8 s
  const auto [first, last] = SpeechSpan(frames.energy_db);
  // Measured with the reference implementation: (16, 72).
  EXPECT_NEAR(static_cast<int>(first), 16, 3);
  EXPECT_NEAR(static_cast<int>(last), 72, 3);
}

TEST(VoiceDsp, SpeechSpanFindsSpeechOverALoudRoom) {
  // The laptop microphone: hiss at -25 dB (+-3), 30 frames of speech at -13.
  // "Within 25 dB of the peak" is below the hiss, so the old rule called every
  // frame speech.
  std::vector<float> energy;
  uint32_t state = 3;
  auto hiss = [&]() {
    state = state * 1664525u + 1013904223u;
    return -25.0f + (static_cast<float>(state >> 24) / 255.0f - 0.5f) * 6.0f;
  };
  for (int i = 0; i < 100; ++i) {
    energy.push_back(hiss());
  }
  for (int i = 0; i < 30; ++i) {
    energy.push_back(-13.0f);
  }
  for (int i = 0; i < 100; ++i) {
    energy.push_back(hiss());
  }
  const auto [first, last] = SpeechSpan(energy);
  EXPECT_NEAR(static_cast<int>(first), 100, 4);
  EXPECT_NEAR(static_cast<int>(last), 130, 4);
  const auto phrase = SpeechSpan(energy, 25.0f, -60.0f, /*phrase_only=*/true);
  EXPECT_NEAR(static_cast<int>(phrase.first), 100, 4);
  EXPECT_NEAR(static_cast<int>(phrase.second), 130, 4);
}

TEST(VoiceDsp, DtwMatchesTheReferenceImplementation) {
  auto features = [](const std::string& name) {
    const std::vector<float> samples = PcmToFloat(LoadWav(name));
    const Frames frames = ExtractFrames(samples);
    const auto [first, last] = SpeechSpan(frames.energy_db);
    return NormalisedFeatures(
        base::span(frames.features).subspan(first, last - first));
  };
  const auto z0 = features("zira_wake_0.wav");
  const auto z1 = features("zira_wake_1.wav");
  const auto z4 = features("zira_wake_4.wav");
  const auto d2 = features("david_wake_2.wav");
  // Golden values from the Python prototype the maths was tuned in.
  EXPECT_NEAR(OpenEndDtw(z0, z1).distance, 0.1200f, 0.02f);
  EXPECT_NEAR(OpenEndDtw(z0, z4).distance, 0.1704f, 0.02f);
  EXPECT_NEAR(OpenEndDtw(z0, d2).distance, 0.4293f, 0.03f);
  // The same phrase by the same voice is much closer than by another voice.
  EXPECT_LT(OpenEndDtw(z0, z1).distance * 2.0f, OpenEndDtw(z0, d2).distance);
}

TEST(VoiceDsp, PitchSeparatesTheTwoVoices) {
  auto median_pitch = [](const std::string& name) {
    const std::vector<float> samples = PcmToFloat(LoadWav(name));
    const Frames frames = ExtractFrames(samples);
    const auto [first, last] = SpeechSpan(frames.energy_db);
    return ComputeSpeakerVector(samples, frames, first, last)[2 * kCepstra];
  };
  const float zira = median_pitch("zira_wake_0.wav");
  const float david = median_pitch("david_wake_0.wav");
  EXPECT_NEAR(zira, 7.756f, 0.1f);  // ~216 Hz
  EXPECT_NEAR(david, 6.604f, 0.1f);  // ~97 Hz
  EXPECT_GT(zira - david, 0.8f);
}

// ---- Enrolment ---------------------------------------------------------------

TEST(VoiceLock, EnrolmentRefusesWhatIsNotAGoodRecording) {
  // A click: too short.
  std::vector<int16_t> click(3200, 0);
  for (int i = 0; i < 400; ++i) {
    click[1000 + i] = static_cast<int16_t>(8000 * std::sin(i * 0.5));
  }
  EXPECT_EQ(AnalyseEnrollmentSample(click, {}).problem,
            SampleProblem::kTooShort);
  // Nothing but a quiet room.
  EXPECT_EQ(AnalyseEnrollmentSample(Room(2.0), {}).problem,
            SampleProblem::kTooQuiet);
  // A whole sentence is not "Hey Zep".
  EXPECT_EQ(AnalyseEnrollmentSample(LoadWav("zira_other_1.wav"), {}).problem,
            SampleProblem::kTooLong);
}

TEST(VoiceLock, EnrolmentRefusesADifferentPhraseOnceThereAreEarlierOnes) {
  const VoiceProfile zira = Enrol("zira", "Owner");
  // "Zep" alone is short enough to be taken for the phrase, and is not it.
  const SampleCheck check = AnalyseEnrollmentSample(
      LoadWav("zira_word_zep.wav"), zira.templates);
  EXPECT_EQ(check.problem, SampleProblem::kNotThePhrase);
  EXPECT_FALSE(DescribeProblem(check.problem).empty());
}

TEST(VoiceLock, CalibrationStaysInsideItsBounds) {
  const VoiceProfile zira = Enrol("zira", "Owner");
  EXPECT_GE(zira.wake_threshold, 0.22f);
  EXPECT_LE(zira.wake_threshold, 0.32f);
  EXPECT_GE(zira.speaker_threshold, 3.0f);
  EXPECT_LE(zira.speaker_threshold, 5.0f);
  EXPECT_GT(zira.pitch_mean, 7.0f);
}

TEST(VoiceLock, TheOwnersPhraseWakesAndAnotherVoiceDoesNot) {
  std::vector<VoiceProfile> profiles = {Enrol("zira", "Owner")};
  // The owner, saying it as part of a command she was never recorded saying.
  const MatchResult owner =
      TestPhrase(profiles, LoadWav("zira_cmd_news.wav"), true,
                 Sensitivity::kBalanced);
  EXPECT_TRUE(owner.wake) << owner.wake_distance;
  EXPECT_TRUE(owner.voice_ok) << owner.speaker_distance;
  EXPECT_EQ(owner.profile, 0);

  const MatchResult other =
      TestPhrase(profiles, LoadWav("david_cmd_news.wav"), true,
                 Sensitivity::kBalanced);
  EXPECT_FALSE(other.wake) << other.wake_distance;

  // Not the phrase at all, from the owner herself.
  for (const char* name : {"zira_other_1.wav", "zira_other_2.wav"}) {
    const MatchResult r = TestPhrase(profiles, LoadWav(name), true,
                                     Sensitivity::kBalanced);
    EXPECT_FALSE(r.wake && r.voice_ok) << name << " " << r.wake_distance;
  }
}

TEST(VoiceLock, TheSpeakerGateStopsAVoiceThatSoundsRightButIsNotEnrolled) {
  // A profile whose recordings sound like the phrase but whose VOICE is
  // another's: what an impostor who says it well enough would look like to the
  // first test. Built by giving Zira's templates David's speaker features.
  VoiceProfile mixed = Enrol("zira", "Owner");
  const VoiceProfile david = Enrol("david", "Other");
  for (size_t i = 0; i < mixed.templates.size(); ++i) {
    mixed.templates[i].speaker = david.templates[i].speaker;
  }
  ASSERT_TRUE(mixed.Prepare());
  std::vector<VoiceProfile> profiles = {mixed};

  const std::vector<int16_t> zira = LoadWav("zira_cmd_news.wav");
  const MatchResult locked =
      TestPhrase(profiles, zira, true, Sensitivity::kBalanced);
  EXPECT_TRUE(locked.wake);  // the phrase matches...
  EXPECT_FALSE(locked.voice_ok) << locked.speaker_distance;  // ...the voice does not
  const MatchResult unlocked =
      TestPhrase(profiles, zira, false, Sensitivity::kBalanced);
  EXPECT_TRUE(unlocked.wake && unlocked.voice_ok);  // lock off: anyone
}

TEST(VoiceLock, SeveralVoicesAreToldApart) {
  std::vector<VoiceProfile> profiles = {Enrol("david", "Dad"),
                                        Enrol("zira", "Mum")};
  const MatchResult zira = TestPhrase(profiles, LoadWav("zira_cmd_weather.wav"),
                                      true, Sensitivity::kBalanced);
  EXPECT_TRUE(zira.wake && zira.voice_ok);
  EXPECT_EQ(zira.profile, 1);
  const MatchResult david = TestPhrase(profiles, LoadWav("david_cmd_weather.wav"),
                                       true, Sensitivity::kBalanced);
  EXPECT_TRUE(david.wake && david.voice_ok);
  EXPECT_EQ(david.profile, 0);
}

TEST(VoiceLock, ProfilesSurviveStorageAndBadBlobsAreRefused) {
  const std::vector<VoiceProfile> profiles = {Enrol("zira", "Mum"),
                                              Enrol("david", "Dad")};
  const std::string blob = SerialiseProfiles(profiles);
  auto parsed = ParseProfiles(blob);
  ASSERT_TRUE(parsed);
  ASSERT_EQ(parsed->size(), 2u);
  EXPECT_EQ((*parsed)[0].name, "Mum");
  EXPECT_EQ((*parsed)[1].templates.size(), 5u);
  // Recomputed, not stored, so the same numbers come back.
  EXPECT_NEAR((*parsed)[0].wake_threshold, profiles[0].wake_threshold, 1e-4f);
  const MatchResult r = TestPhrase(*parsed, LoadWav("zira_cmd_news.wav"), true,
                                   Sensitivity::kBalanced);
  EXPECT_TRUE(r.wake && r.voice_ok);

  EXPECT_FALSE(ParseProfiles(""));
  EXPECT_FALSE(ParseProfiles("not a profile blob"));
  EXPECT_FALSE(ParseProfiles(blob.substr(0, blob.size() / 2)));
  EXPECT_FALSE(ParseProfiles(blob + "x"));
  std::string too_many = blob;
  too_many[4] = 99;  // a profile count no one would have
  EXPECT_FALSE(ParseProfiles(too_many));
}

// ---- The engine on a stream --------------------------------------------------

class Recorder : public HandsFreeEngine::Delegate {
 public:
  void OnWake(const HandsFreeEngine::WakeInfo& info) override {
    ++wakes;
    last = info;
  }
  void OnCommand(std::vector<int16_t> pcm,
                 const HandsFreeEngine::WakeInfo&) override {
    commands.push_back(std::move(pcm));
  }
  void OnTimedOut() override { ++timeouts; }
  void OnIgnored(HandsFreeEngine::Ignored why, const MatchResult&) override {
    ignored.push_back(why);
  }

  int wakes = 0;
  int timeouts = 0;
  HandsFreeEngine::WakeInfo last;
  std::vector<std::vector<int16_t>> commands;
  std::vector<HandsFreeEngine::Ignored> ignored;
};

// Feeds in awkward chunk sizes, as a real device delivers them.
void FeedInChunks(HandsFreeEngine& engine, const std::vector<int16_t>& pcm) {
  static const size_t kSizes[] = {1600, 733, 160, 2049, 501};
  size_t at = 0;
  size_t i = 0;
  while (at < pcm.size()) {
    const size_t n = std::min(kSizes[i++ % 5], pcm.size() - at);
    engine.Feed(base::span(pcm).subspan(at, n));
    at += n;
  }
}

double Seconds(const std::vector<int16_t>& pcm) {
  return static_cast<double>(pcm.size()) / kSampleRate;
}

TEST(HandsFree, HeyZepAndACommandInOneBreath) {
  Recorder events;
  HandsFreeEngine engine(&events, {});
  engine.SetProfiles({Enrol("zira", "Owner")});
  FeedInChunks(engine,
               Join({Room(1.5), LoadWav("zira_cmd_news.wav"), Room(2.0, 2)}));
  EXPECT_EQ(events.wakes, 1);
  ASSERT_EQ(events.commands.size(), 1u);
  // "open the news page": a second or so of speech, without the phrase.
  EXPECT_GT(Seconds(events.commands[0]), 0.7);
  EXPECT_LT(Seconds(events.commands[0]), 2.6);
  EXPECT_TRUE(events.ignored.empty());
  EXPECT_EQ(engine.state(), HandsFreeEngine::State::kIdle);
}

TEST(HandsFree, AnotherVoiceSayingItIsIgnoredAndNothingIsHandedOver) {
  Recorder events;
  HandsFreeEngine engine(&events, {});
  engine.SetProfiles({Enrol("zira", "Owner")});
  FeedInChunks(engine,
               Join({Room(1.5), LoadWav("david_cmd_news.wav"), Room(2.0, 2)}));
  EXPECT_EQ(events.wakes, 0);
  EXPECT_TRUE(events.commands.empty());
  EXPECT_EQ(events.ignored.size(), 1u);
}

TEST(HandsFree, OrdinaryConversationIsNotForZep) {
  Recorder events;
  HandsFreeEngine engine(&events, {});
  engine.SetProfiles({Enrol("zira", "Owner")});
  FeedInChunks(engine, Join({Room(1.5), LoadWav("zira_other_1.wav"), Room(1.5, 2),
                             LoadWav("david_other_1.wav"), Room(1.5, 3),
                             LoadWav("zira_other_2.wav"), Room(1.5, 4),
                             LoadWav("david_other_2.wav"), Room(1.5, 5)}));
  EXPECT_EQ(events.wakes, 0);
  EXPECT_TRUE(events.commands.empty());
  EXPECT_GE(events.ignored.size(), 3u);
}

TEST(HandsFree, ThePhraseAloneThenTheCommandAfterAPause) {
  Recorder events;
  HandsFreeEngine engine(&events, {});
  engine.SetProfiles({Enrol("zira", "Owner")});
  FeedInChunks(engine, Join({Room(1.5), LoadWav("zira_wake_2.wav"), Room(1.2, 2),
                             LoadWav("zira_other_1.wav"), Room(2.0, 3)}));
  EXPECT_EQ(events.wakes, 1);
  ASSERT_EQ(events.commands.size(), 1u);
  EXPECT_GT(Seconds(events.commands[0]), 1.5);  // the whole sentence
}

TEST(HandsFree, ACommandInAnotherVoiceAfterThePhraseIsRefused) {
  Recorder events;
  HandsFreeEngine engine(&events, {});
  engine.SetProfiles({Enrol("zira", "Owner")});
  FeedInChunks(engine, Join({Room(1.5), LoadWav("zira_wake_2.wav"), Room(1.2, 2),
                             LoadWav("david_other_1.wav"), Room(2.0, 3)}));
  EXPECT_EQ(events.wakes, 1);
  EXPECT_TRUE(events.commands.empty());
  ASSERT_EQ(events.ignored.size(), 1u);
  EXPECT_EQ(events.ignored[0], HandsFreeEngine::Ignored::kWrongVoiceFollowUp);
}

TEST(HandsFree, NoCommandAfterThePhraseTimesOut) {
  Recorder events;
  HandsFreeEngine engine(&events, {});
  engine.SetProfiles({Enrol("zira", "Owner")});
  FeedInChunks(engine, Join({Room(1.5), LoadWav("zira_wake_2.wav"), Room(9.0, 2)}));
  EXPECT_EQ(events.wakes, 1);
  EXPECT_EQ(events.timeouts, 1);
  EXPECT_TRUE(events.commands.empty());
  EXPECT_EQ(engine.state(), HandsFreeEngine::State::kIdle);
}

TEST(HandsFree, WithTheLockOffAnyVoiceThatSaysItIsHeard) {
  // The phrase is still matched on this computer, against the recordings, so a
  // very different voice is not heard even then -- the lock adds the voice.
  Recorder events;
  HandsFreeEngine engine(&events, {.lock = false});
  engine.SetProfiles({Enrol("zira", "Owner")});
  FeedInChunks(engine,
               Join({Room(1.5), LoadWav("zira_cmd_weather.wav"), Room(2.0, 2)}));
  EXPECT_EQ(events.wakes, 1);
  EXPECT_EQ(events.commands.size(), 1u);
}

TEST(HandsFree, WithNoVoicesNothingIsExaminedOrKept) {
  Recorder events;
  HandsFreeEngine engine(&events, {});
  EXPECT_FALSE(engine.has_profiles());
  FeedInChunks(engine,
               Join({Room(1.5), LoadWav("zira_cmd_news.wav"), Room(2.0, 2)}));
  EXPECT_EQ(events.wakes, 0);
  EXPECT_TRUE(events.commands.empty());
  EXPECT_TRUE(events.ignored.empty());
}

TEST(HandsFree, TheChunkSizeDoesNotChangeTheDecision) {
  const std::vector<int16_t> stream =
      Join({Room(1.5), LoadWav("zira_cmd_news.wav"), Room(2.0, 2)});
  for (size_t chunk : {160u, 1u, 4000u}) {
    Recorder events;
    HandsFreeEngine engine(&events, {});
    engine.SetProfiles({Enrol("zira", "Owner")});
    for (size_t at = 0; at < stream.size(); at += chunk) {
      engine.Feed(base::span(stream).subspan(
          at, std::min(chunk, stream.size() - at)));
    }
    EXPECT_EQ(events.wakes, 1) << chunk;
    EXPECT_EQ(events.commands.size(), 1u) << chunk;
  }
}

// ---- Enrolment's listener ----------------------------------------------------

// All the utterances a recorder finds in a stream fed in awkward chunks.
std::vector<std::vector<int16_t>> Utterances(PhraseRecorder& recorder,
                                             const std::vector<int16_t>& pcm) {
  std::vector<std::vector<int16_t>> found;
  static const size_t kSizes[] = {1600, 733, 160, 2049, 501};
  size_t at = 0;
  size_t i = 0;
  while (at < pcm.size()) {
    const size_t n = std::min(kSizes[i++ % 5], pcm.size() - at);
    if (auto u = recorder.Feed(base::span(pcm).subspan(at, n))) {
      found.push_back(std::move(*u));
    }
    at += n;
  }
  return found;
}

TEST(PhraseRecorder, CatchesEachPhraseWholeAndOnlyThat) {
  PhraseRecorder recorder;
  const auto found = Utterances(
      recorder, Join({Room(1.5), LoadWav("zira_wake_0.wav"), Room(1.5, 2),
                      LoadWav("zira_wake_1.wav"), Room(1.5, 3)}));
  ASSERT_EQ(found.size(), 2u);
  // What it caught is a recording enrolment accepts, and not much longer than
  // the phrase itself: the room around it is trimmed to a little either side.
  for (const auto& utterance : found) {
    EXPECT_EQ(AnalyseEnrollmentSample(utterance, {}).problem,
              SampleProblem::kNone);
    EXPECT_LT(Seconds(utterance), 2.2);
  }
}

// A laptop's own microphone: a constant hiss at about -25 dB, the speech only
// ~12 dB above it, and a stretch of digital silence before the device starts.
// MEASURED on a real machine, where this made the recorder never find the end of
// a phrase and enrolment call every recording "too long" or "too quiet".
std::vector<int16_t> LoudMicrophone(const std::vector<int16_t>& speech,
                                    double lead_seconds,
                                    double tail_seconds,
                                    bool device_gap = false) {
  // The device gives digital silence before it starts, once.
  std::vector<int16_t> pcm(
      static_cast<size_t>((device_gap ? 0.4 : 0.0) * kSampleRate), 0);
  uint32_t state = 7;
  auto hiss = [&](size_t count) {
    for (size_t i = 0; i < count; ++i) {
      state = state * 1664525u + 1013904223u;
      // Uniform +-0.09 is about -25 dBFS RMS.
      const double x = (static_cast<double>(state >> 8) / 16777216.0 - 0.5) * 0.18;
      pcm.push_back(static_cast<int16_t>(x * 32768.0));
    }
  };
  hiss(static_cast<size_t>(lead_seconds * kSampleRate));
  // Speech scaled so its loudest 100 ms is at about -14 dB, as it was on the
  // real machine (the hiss at -25).
  double loudest = 1e-9;
  for (size_t at = 0; at + 1600 <= speech.size(); at += 400) {
    double sum = 0;
    for (size_t i = at; i < at + 1600; ++i) {
      const double v = speech[i] / 32768.0;
      sum += v * v;
    }
    loudest = std::max(loudest, std::sqrt(sum / 1600.0));
  }
  const double gain = 0.2 / loudest;
  for (int16_t v : speech) {
    state = state * 1664525u + 1013904223u;
    const double x = (static_cast<double>(state >> 8) / 16777216.0 - 0.5) * 0.18;
    pcm.push_back(static_cast<int16_t>(
        std::clamp(v * gain + x * 32768.0, -32768.0, 32767.0)));
  }
  hiss(static_cast<size_t>(tail_seconds * kSampleRate));
  return pcm;
}

TEST(PhraseRecorder, FindsAPhraseOverALoudMicrophoneHiss) {
  PhraseRecorder recorder;
  const auto found = Utterances(
      recorder,
      Join({LoudMicrophone(LoadWav("zira_wake_0.wav"), 3.0, 2.0, true),
            LoudMicrophone(LoadWav("zira_wake_1.wav"), 0.0, 2.0),
            LoudMicrophone(LoadWav("zira_wake_2.wav"), 0.0, 2.0)}));
  // The hiss must not be taken for speech: three phrases, each a phrase long.
  ASSERT_GE(found.size(), 2u);
  int accepted = 0;
  for (const auto& utterance : found) {
    EXPECT_LT(Seconds(utterance), 3.0);
    accepted += AnalyseEnrollmentSample(utterance, {}).problem ==
                        SampleProblem::kNone
                    ? 1
                    : 0;
  }
  EXPECT_GE(accepted, 2);
}

TEST(PhraseRecorder, IgnoresAQuietRoomAndAClick) {
  PhraseRecorder recorder;
  std::vector<int16_t> click(1600, 0);
  for (int i = 0; i < 160; ++i) {
    click[400 + i] = static_cast<int16_t>(9000 * std::sin(i * 0.4));
  }
  EXPECT_TRUE(Utterances(recorder,
                         Join({Room(2.0), click, Room(2.0, 2)}))
                  .empty());
}

// ---- The voices, kept ----------------------------------------------------------

class VoiceLibraryTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
};

VoiceProfile Named(const std::string& name) {
  VoiceProfile p = Enrol("zira", name);
  p.id.clear();
  return p;
}

TEST_F(VoiceLibraryTest, AddsRenamesAndRemovesWithGoodManners) {
  VoiceLibrary library((base::FilePath()));
  EXPECT_TRUE(library.loaded());  // nowhere to keep them, nothing to wait for
  EXPECT_EQ(library.Add(Named("Mum")), VoiceLibrary::Result::kOk);
  EXPECT_EQ(library.Add(Named("mum")), VoiceLibrary::Result::kDuplicate);
  EXPECT_EQ(library.Add(Named("   ")), VoiceLibrary::Result::kBadName);
  EXPECT_EQ(library.Add(Named(std::string(kMaxNameLength + 1, 'x'))),
            VoiceLibrary::Result::kBadName);
  ASSERT_EQ(library.profiles().size(), 1u);
  const std::string id = library.profiles()[0].id;
  EXPECT_FALSE(id.empty());
  EXPECT_EQ(library.Rename(id, " Dad "), VoiceLibrary::Result::kOk);
  EXPECT_EQ(library.profiles()[0].name, "Dad");
  EXPECT_EQ(library.Rename("nobody", "x"), VoiceLibrary::Result::kNotFound);
  EXPECT_EQ(library.Remove(id), VoiceLibrary::Result::kOk);
  EXPECT_TRUE(library.profiles().empty());
  EXPECT_EQ(library.Remove(id), VoiceLibrary::Result::kNotFound);
}

TEST_F(VoiceLibraryTest, HoldsOnlyAsManyAsItSays) {
  VoiceLibrary library((base::FilePath()));
  for (size_t i = 0; i < kMaxProfiles; ++i) {
    EXPECT_EQ(library.Add(Named("Voice" + std::to_string(i))),
              VoiceLibrary::Result::kOk);
  }
  EXPECT_EQ(library.Add(Named("One too many")), VoiceLibrary::Result::kFull);
}

TEST_F(VoiceLibraryTest, ObserversHearOfEveryChange) {
  VoiceLibrary library((base::FilePath()));
  int changes = 0;
  auto subscription =
      library.Subscribe(base::BindRepeating([](int* n) { ++*n; }, &changes));
  library.Add(Named("Mum"));
  library.Rename(library.profiles()[0].id, "Dad");
  library.Remove(library.profiles()[0].id);
  EXPECT_EQ(changes, 3);
}

TEST_F(VoiceLibraryTest, VoicesSurviveARestartAndAreNeverInTheClear) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const base::FilePath path = dir.GetPath().AppendASCII("voices");
  const auto encryptor = os_crypt_async::GetTestEncryptorForTesting();
  {
    VoiceLibrary library(path);
    EXPECT_FALSE(library.loaded());
    // Nothing can be saved until the keystore has answered.
    EXPECT_EQ(library.Add(Named("Mum")), VoiceLibrary::Result::kNoKeystore);
    library.SetEncryptor(encryptor);
    base::RunLoop loop;
    library.WhenLoaded(loop.QuitClosure());
    loop.Run();
    EXPECT_EQ(library.Add(Named("Mum")), VoiceLibrary::Result::kOk);
  }
  task_environment_.RunUntilIdle();
  std::string on_disk;
  ASSERT_TRUE(base::ReadFileToString(path, &on_disk));
  EXPECT_EQ(on_disk.find("Mum"), std::string::npos);
  EXPECT_EQ(on_disk.find("ZVP1"), std::string::npos);

  VoiceLibrary again(path);
  again.SetEncryptor(encryptor);
  base::RunLoop loop;
  again.WhenLoaded(loop.QuitClosure());
  loop.Run();
  ASSERT_EQ(again.profiles().size(), 1u);
  EXPECT_EQ(again.profiles()[0].name, "Mum");
  // And it still recognises her.
  std::vector<VoiceProfile> profiles = again.profiles();
  const MatchResult r = TestPhrase(profiles, LoadWav("zira_cmd_news.wav"), true,
                                   Sensitivity::kBalanced);
  EXPECT_TRUE(r.wake && r.voice_ok);
}

TEST_F(VoiceLibraryTest, DeletingTheLastVoiceThenRecordingAgainKeepsTheNewOne) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const base::FilePath path = dir.GetPath().AppendASCII("voices");
  const auto encryptor = os_crypt_async::GetTestEncryptorForTesting();
  {
    VoiceLibrary library(path);
    library.SetEncryptor(encryptor);
    base::RunLoop loop;
    library.WhenLoaded(loop.QuitClosure());
    loop.Run();
    ASSERT_EQ(library.Add(Named("Old")), VoiceLibrary::Result::kOk);
    const std::string id = library.profiles()[0].id;
    // No pause between them, as a quick pair of clicks would make.
    ASSERT_EQ(library.Remove(id), VoiceLibrary::Result::kOk);
    ASSERT_EQ(library.Add(Named("New")), VoiceLibrary::Result::kOk);
  }
  task_environment_.RunUntilIdle();
  VoiceLibrary again(path);
  again.SetEncryptor(encryptor);
  base::RunLoop loop;
  again.WhenLoaded(loop.QuitClosure());
  loop.Run();
  ASSERT_EQ(again.profiles().size(), 1u);
  EXPECT_EQ(again.profiles()[0].name, "New");
}

TEST_F(VoiceLibraryTest, ADamagedFileMeansNoVoicesNotACrash) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const base::FilePath path = dir.GetPath().AppendASCII("voices");
  ASSERT_TRUE(base::WriteFile(path, "this is not an encrypted voice file"));
  VoiceLibrary library(path);
  library.SetEncryptor(os_crypt_async::GetTestEncryptorForTesting());
  base::RunLoop loop;
  library.WhenLoaded(loop.QuitClosure());
  loop.Run();
  EXPECT_TRUE(library.profiles().empty());
}


TEST(HandsFree, KeepingWatchIsCheap) {
  // The listener runs the whole time the microphone is open, so what it costs
  // per second of audio is what it costs the person all day. Measured, not
  // assumed: a minute of room noise, then a minute of people talking (speech
  // that is not the phrase, which is the expensive case because each burst is
  // examined), fed as a real device delivers it.
  Recorder events;
  HandsFreeEngine engine(&events, {});
  engine.SetProfiles({Enrol("zira", "Owner")});

  std::vector<int16_t> quiet = Room(60.0);
  std::vector<int16_t> talk;
  const std::vector<int16_t> a = LoadWav("david_other_1.wav");
  const std::vector<int16_t> b = LoadWav("zira_other_2.wav");
  const std::vector<int16_t> gap = Room(1.0, 5);
  while (talk.size() < 60u * kSampleRate) {
    talk = Join({talk, a, gap, b, gap});
  }

  const base::TimeTicks start = base::TimeTicks::Now();
  FeedInChunks(engine, quiet);
  const base::TimeDelta quiet_cost = base::TimeTicks::Now() - start;
  const base::TimeTicks talk_start = base::TimeTicks::Now();
  FeedInChunks(engine, talk);
  const base::TimeDelta talk_cost = base::TimeTicks::Now() - talk_start;

  const double quiet_share = quiet_cost.InSecondsF() / Seconds(quiet);
  const double talk_share = talk_cost.InSecondsF() / Seconds(talk);
  LOG(INFO) << "listening cost: quiet " << quiet_share * 100.0
                  << "% of one core, talking " << talk_share * 100.0 << "%";
  EXPECT_EQ(events.wakes, 0);
  // Generous bounds (a loaded test machine, an unoptimised build): the point is
  // to catch it becoming ten times dearer, not to police a percent.
  EXPECT_LT(quiet_share, 0.05);
  EXPECT_LT(talk_share, 0.25);
}

TEST(VoiceLock, AGarbledVoiceFileIsRefusedNeverTrusted) {
  // The parser reads bytes from disk. Whatever it is fed -- a damaged file, a
  // truncated write, a hand-edited one -- it must answer "no voices" or a valid
  // set, and never read outside what it was given or allocate what a length
  // field claims. Mutations of a valid blob, from a fixed seed.
  const std::string good = SerialiseProfiles({Enrol("zira", "Mum")});
  uint32_t state = 12345;
  auto next = [&state]() {
    state = state * 1664525u + 1013904223u;
    return state >> 8;
  };
  for (int round = 0; round < 3000; ++round) {
    std::string blob = good;
    const int edits = 1 + static_cast<int>(next() % 6);
    for (int e = 0; e < edits; ++e) {
      switch (next() % 4) {
        case 0:  // flip a byte
          blob[next() % blob.size()] = static_cast<char>(next());
          break;
        case 1:  // truncate
          blob.resize(next() % blob.size());
          break;
        case 2:  // a huge length where a count or size sits
          if (blob.size() > 12) {
            const size_t at = 4 + next() % 8;
            blob[at] = static_cast<char>(0xFF);
          }
          break;
        default:  // append noise
          blob.push_back(static_cast<char>(next()));
          break;
      }
      if (blob.empty()) {
        break;
      }
    }
    auto parsed = ParseProfiles(blob);
    if (parsed) {
      EXPECT_LE(parsed->size(), kMaxProfiles);
      for (const VoiceProfile& p : *parsed) {
        EXPECT_LE(p.templates.size(), kMaxTemplates);
        EXPECT_LE(p.name.size(), kMaxNameLength * 4);
      }
    }
  }
}

// Diagnostic, not a test: runs the real enrolment and wake code over recordings
// made on a real microphone. Skipped unless ZEPH_REAL_DIR names a folder of
// 16 kHz mono WAVs of a person saying "Hey Zep" several times.
TEST(VoiceReal, DescribeRecordings) {
  const char* dir_env = std::getenv("ZEPH_REAL_DIR");
  if (!dir_env) {
    GTEST_SKIP() << "ZEPH_REAL_DIR not set";
  }
  const base::FilePath dir = base::FilePath::FromUTF8Unsafe(dir_env);
  base::FileEnumerator files(dir, false, base::FileEnumerator::FILES,
                             FILE_PATH_LITERAL("*.wav"));
  for (base::FilePath path = files.Next(); !path.empty(); path = files.Next()) {
    std::string bytes;
    ASSERT_TRUE(base::ReadFileToString(path, &bytes));
    size_t pos = 12;
    std::vector<int16_t> pcm;
    while (pos + 8 <= bytes.size()) {
      const std::string id = bytes.substr(pos, 4);
      const uint32_t size = static_cast<uint8_t>(bytes[pos + 4]) |
                            (static_cast<uint8_t>(bytes[pos + 5]) << 8) |
                            (static_cast<uint8_t>(bytes[pos + 6]) << 16) |
                            (static_cast<uint32_t>(static_cast<uint8_t>(bytes[pos + 7])) << 24);
      if (id == "data") {
        pcm.resize(std::min<size_t>(size, bytes.size() - pos - 8) / 2);
        for (size_t k = 0; k < pcm.size(); ++k) {
          pcm[k] = static_cast<int16_t>(static_cast<uint8_t>(bytes[pos + 8 + 2 * k]) |
                                        (static_cast<uint8_t>(bytes[pos + 9 + 2 * k]) << 8));
        }
        break;
      }
      pos += 8 + size;
    }
    LOG(INFO) << "REAL ===== " << path.BaseName().AsUTF8Unsafe() << " "
              << Seconds(pcm) << " s";

    // 1. What the enrolment screen's recorder makes of it.
    PhraseRecorder recorder;
    std::vector<std::vector<int16_t>> utterances;
    for (size_t at = 0; at < pcm.size(); at += 1600) {
      const size_t n = std::min<size_t>(1600, pcm.size() - at);
      if (auto u = recorder.Feed(base::span(pcm).subspan(at, n))) {
        utterances.push_back(std::move(*u));
      }
    }
    LOG(INFO) << "REAL recorder found " << utterances.size() << " utterances";

    // 2. Each, as enrolment would judge it, against the takes accepted so far.
    // ZEPH_REAL_SKIP drops the first takes (a beep that cued the person).
    const char* skip_env = std::getenv("ZEPH_REAL_SKIP");
    const size_t skip = skip_env ? static_cast<size_t>(std::atoi(skip_env)) : 0;
    const size_t skip_samples = skip ? 16000 * 3 : 0;
    std::vector<WakeTemplate> accepted;
    for (size_t u = skip; u < utterances.size(); ++u) {
      SampleCheck check = AnalyseEnrollmentSample(utterances[u], accepted);
      LOG(INFO) << "REAL take " << u << ": " << Seconds(utterances[u])
                << " s -> problem " << static_cast<int>(check.problem) << " ("
                << DescribeProblem(check.problem) << ")"
                << (accepted.empty() ? "" : " vs earlier");
      if (check.problem == SampleProblem::kNone && accepted.size() < kEnrollmentSamples) {
        accepted.push_back(std::move(check.sample));
      }
    }
    {
      // The whole matrix, judged with no gate: is it the takes or the gate?
      std::vector<WakeTemplate> solo;
      for (size_t u = skip; u < utterances.size(); ++u) {
        SampleCheck c = AnalyseEnrollmentSample(utterances[u], {});
        LOG(INFO) << "REAL solo " << u << " problem "
                  << static_cast<int>(c.problem) << " frames "
                  << c.sample.frames.size();
        if (c.problem == SampleProblem::kNone) {
          solo.push_back(std::move(c.sample));
        }
      }
      for (size_t x = 0; x < solo.size(); ++x) {
        std::string row;
        for (size_t y = 0; y < solo.size(); ++y) {
          row += x == y ? "  --  "
                        : " " + base::NumberToString(
                                    OpenEndDtw(NormalisedFeatures(solo[x].frames),
                                               NormalisedFeatures(solo[y].frames))
                                        .distance)
                                    .substr(0, 5);
        }
        LOG(INFO) << "REAL solo row " << x << ":" << row;
      }
    }
    LOG(INFO) << "REAL accepted " << accepted.size() << " of "
              << utterances.size();
    // 3. How far apart the accepted takes are from one another.
    for (size_t a = 0; a < accepted.size(); ++a) {
      std::string row;
      for (size_t b = 0; b < accepted.size(); ++b) {
        if (a == b) {
          row += "  --  ";
          continue;
        }
        const float d = OpenEndDtw(NormalisedFeatures(accepted[a].frames),
                                   NormalisedFeatures(accepted[b].frames)).distance;
        row += " " + base::NumberToString(d).substr(0, 5);
      }
      LOG(INFO) << "REAL dtw row " << a << ":" << row;
    }
    if (accepted.size() >= 3) {
      VoiceProfile profile;
      profile.id = "real";
      profile.name = "Real";
      profile.templates = accepted;
      profile.Prepare();
      LOG(INFO) << "REAL thresholds wake " << profile.wake_threshold
                << " speaker " << profile.speaker_threshold;
      // 4. Would it wake on the takes themselves, and on the whole recording?
      int woke = 0;
      for (const auto& utt : utterances) {
        const MatchResult r = TestPhrase(std::vector<VoiceProfile>{profile}, utt,
                                         true, Sensitivity::kBalanced);
        woke += (r.wake && r.voice_ok) ? 1 : 0;
        LOG(INFO) << "REAL  utterance wake=" << r.wake << " ok=" << r.voice_ok
                  << " dist=" << r.wake_distance << " spk=" << r.speaker_distance;
      }
      LOG(INFO) << "REAL woke on " << woke << " of " << utterances.size();

      // 5. The streaming engine over the WHOLE recording, as the browser runs
      // it: how many of the phrases in it does it wake on, and on what else.
      struct Counter : HandsFreeEngine::Delegate {
        int wakes = 0;
        int commands = 0;
        int ignored = 0;
        void OnWake(const HandsFreeEngine::WakeInfo&) override { ++wakes; }
        void OnCommand(std::vector<int16_t>,
                       const HandsFreeEngine::WakeInfo&) override {
          ++commands;
        }
        void OnTimedOut() override {}
        void OnIgnored(HandsFreeEngine::Ignored,
                       const MatchResult&) override {
          ++ignored;
        }
      } counter;
      HandsFreeEngine engine(&counter, {});
      engine.SetProfiles({profile});
      for (size_t at = skip_samples; at < pcm.size(); at += 1600) {
        engine.Feed(base::span(pcm).subspan(
            at, std::min<size_t>(1600, pcm.size() - at)));
      }
      LOG(INFO) << "REAL engine wakes " << counter.wakes << " commands "
                << counter.commands << " ignored "
                << counter.ignored << " floor " << engine.noise_floor_db();
    }
  }
}

}  // namespace
}  // namespace zephyrus::agent::voice
