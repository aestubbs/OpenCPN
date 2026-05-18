/**
 * Unit tests for Nmea0183Decoder (task P1.5b).
 *
 * The decoder is pure -- these run with no transport, no Qt event loop and
 * no hardware, exercising it against literal sentence frames.
 */

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "model/comm_n0183_decoder.h"
#include "model/conn_params.h"

/** A frame is the byte vector a LineFramer hands the decoder. */
static CommFrame Frame(const std::string& s) {
  return CommFrame(s.begin(), s.end());
}

/** Default connection params with a chosen I/O direction. */
static ConnectionParams Params(dsPortType io) {
  ConnectionParams p;  // empty filter lists -> everything passes
  p.IOSelect = io;
  return p;
}

static const std::shared_ptr<const NavAddr> kSrc =
    std::make_shared<const NavAddr>(NavAddr::Bus::N0183, "test-iface");

// A valid GGA sentence (the canonical example) and its correct checksum.
static const char* kGoodGga =
    "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";

TEST(Nmea0183Decoder, DecodesValidSentence) {
  Nmea0183Decoder decoder(Params(DS_TYPE_INPUT));
  auto msgs = decoder.Decode(Frame(kGoodGga), kSrc);
  ASSERT_EQ(msgs.size(), 1u);
  EXPECT_EQ(msgs[0]->state, NavMsg::State::kOk);
  EXPECT_EQ(msgs[0]->source, kSrc);
}

TEST(Nmea0183Decoder, FlagsBadChecksum) {
  Nmea0183Decoder decoder(Params(DS_TYPE_INPUT));
  // Same sentence, deliberately wrong checksum.
  std::string bad(kGoodGga);
  bad.replace(bad.size() - 2, 2, "00");
  auto msgs = decoder.Decode(Frame(bad), kSrc);
  ASSERT_EQ(msgs.size(), 1u);
  EXPECT_EQ(msgs[0]->state, NavMsg::State::kBadChecksum);
}

TEST(Nmea0183Decoder, FlagsGarbageWithControlChar) {
  Nmea0183Decoder decoder(Params(DS_TYPE_INPUT));
  auto msgs = decoder.Decode(Frame("$GP\x01GA,1,2,3*00"), kSrc);
  ASSERT_EQ(msgs.size(), 1u);
  EXPECT_EQ(msgs[0]->state, NavMsg::State::kCannotParse);
}

TEST(Nmea0183Decoder, StripsV4TagPrefix) {
  Nmea0183Decoder decoder(Params(DS_TYPE_INPUT));
  // Anything before the '$' (here a v4 tag block) is dropped.
  auto msgs = decoder.Decode(Frame(std::string("\\s:GP01*5C\\") + kGoodGga),
                             kSrc);
  ASSERT_EQ(msgs.size(), 1u);
  EXPECT_EQ(msgs[0]->state, NavMsg::State::kOk);
}

TEST(Nmea0183Decoder, OutputOnlyConnectionDecodesNothing) {
  Nmea0183Decoder decoder(Params(DS_TYPE_OUTPUT));
  EXPECT_TRUE(decoder.Decode(Frame(kGoodGga), kSrc).empty());
}

TEST(Nmea0183Decoder, EmptyOrSentenceLessFrameYieldsNothing) {
  Nmea0183Decoder decoder(Params(DS_TYPE_INPUT));
  EXPECT_TRUE(decoder.Decode(Frame("no sentence here"), kSrc).empty());
  EXPECT_TRUE(decoder.Decode(Frame(""), kSrc).empty());
}

TEST(Nmea0183Decoder, EncodeAppendsCrlf) {
  Nmea0183Decoder decoder(Params(DS_TYPE_INPUT_OUTPUT));
  auto msg = std::make_shared<const Nmea0183Msg>("GPGGA", kGoodGga, kSrc);
  auto frames = decoder.Encode(msg, nullptr);
  ASSERT_EQ(frames.size(), 1u);
  const std::string out(frames[0].begin(), frames[0].end());
  EXPECT_EQ(out, std::string(kGoodGga) + "\r\n");
}

TEST(Nmea0183Decoder, EncodeDoesNotDoubleTerminate) {
  Nmea0183Decoder decoder(Params(DS_TYPE_INPUT_OUTPUT));
  auto msg = std::make_shared<const Nmea0183Msg>(
      "GPGGA", std::string(kGoodGga) + "\r\n", kSrc);
  auto frames = decoder.Encode(msg, nullptr);
  ASSERT_EQ(frames.size(), 1u);
  const std::string out(frames[0].begin(), frames[0].end());
  EXPECT_EQ(out, std::string(kGoodGga) + "\r\n");
}

TEST(Nmea0183Decoder, EncodeRejectsInputOnlyConnection) {
  Nmea0183Decoder decoder(Params(DS_TYPE_INPUT));
  auto msg = std::make_shared<const Nmea0183Msg>("GPGGA", kGoodGga, kSrc);
  EXPECT_TRUE(decoder.Encode(msg, nullptr).empty());
}

TEST(Nmea0183Decoder, EncodeRejectsNon0183Message) {
  Nmea0183Decoder decoder(Params(DS_TYPE_INPUT_OUTPUT));
  std::shared_ptr<const NavMsg> n2k = std::make_shared<const Nmea2000Msg>(
      static_cast<uint64_t>(129025));
  EXPECT_TRUE(decoder.Encode(n2k, nullptr).empty());
}
