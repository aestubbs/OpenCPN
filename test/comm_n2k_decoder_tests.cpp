/**
 * Unit tests for N2kDecoder (task P1.5d).
 *
 * Pure -- no transport, no Qt, no hardware. Encode() output is checked by
 * feeding it back through N2kGatewayFramer, which un-escapes and de-frames.
 */

#include <cstdint>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "model/comm_framer.h"
#include "model/comm_n2k_decoder.h"

static const std::shared_ptr<const NavAddr> kSrc =
    std::make_shared<const NavAddr>(NavAddr::Bus::N2000, "n2k-iface");

// A received Actisense N2K data frame: code 0x93, length, priority, the
// 3-byte PGN (127250 = 0x01F112, little-endian), destination, source, then
// padding so the frame is long enough for the NAME reinterpretation.
static CommFrame DataFrame() {
  return {0x93, 0x08, 0x02, 0x12, 0xF1, 0x01, 0xFF, 0x01, 0xAA, 0xBB};
}

TEST(N2kDecoder, DecodesDataFrame) {
  N2kDecoder decoder(DS_TYPE_INPUT);
  auto msgs = decoder.Decode(DataFrame(), kSrc);
  ASSERT_EQ(msgs.size(), 1u);
  auto n2k = std::dynamic_pointer_cast<const Nmea2000Msg>(msgs[0]);
  ASSERT_TRUE(n2k);
  EXPECT_EQ(n2k->PGN.pgn, 127250u);
}

TEST(N2kDecoder, IgnoresManagementFrame) {
  N2kDecoder decoder(DS_TYPE_INPUT);
  CommFrame mgmt = {0xA0, 0x01, 0x47, 0x00, 0x00, 0x00, 0x00, 0x00};
  EXPECT_TRUE(decoder.Decode(mgmt, kSrc).empty());
}

TEST(N2kDecoder, IgnoresShortFrame) {
  N2kDecoder decoder(DS_TYPE_INPUT);
  EXPECT_TRUE(decoder.Decode({0x93, 0x01, 0x02}, kSrc).empty());
}

TEST(N2kDecoder, OutputOnlyConnectionDecodesNothing) {
  N2kDecoder decoder(DS_TYPE_OUTPUT);
  EXPECT_TRUE(decoder.Decode(DataFrame(), kSrc).empty());
}

TEST(N2kDecoder, EncodeProducesFramerReadablePacket) {
  N2kDecoder decoder(DS_TYPE_INPUT_OUTPUT);
  // Payload deliberately contains an ESC byte (0x10) to exercise escaping.
  const std::vector<unsigned char> data = {0x01, 0x10, 0x02};
  auto msg = std::make_shared<const Nmea2000Msg>(
      static_cast<uint64_t>(127250), data,
      std::make_shared<const NavAddr2000>(), 6);

  auto frames = decoder.Encode(msg, nullptr);
  ASSERT_EQ(frames.size(), 1u);

  // The framer un-escapes and de-frames -- a faithful packet round-trips.
  N2kGatewayFramer framer;
  auto app = framer.Feed(frames[0]);
  ASSERT_EQ(app.size(), 1u);
  const CommFrame& a = app[0];
  ASSERT_GE(a.size(), 8u + data.size());
  EXPECT_EQ(a[0], 0x94);               // PC-to-gateway TX data code
  EXPECT_EQ(a[1], data.size() + 6);    // declared length
  EXPECT_EQ(a[2], 6);                  // priority
  EXPECT_EQ(a[3], 0x12);               // PGN little-endian
  EXPECT_EQ(a[4], 0xF1);
  EXPECT_EQ(a[5], 0x01);
  EXPECT_EQ(a[6], 255);                // broadcast destination
  EXPECT_EQ(a[7], data.size());        // data length
  EXPECT_EQ(CommFrame(a.begin() + 8, a.begin() + 8 + data.size()), data);
}

TEST(N2kDecoder, EncodeUsesDestinationAddress) {
  N2kDecoder decoder(DS_TYPE_INPUT_OUTPUT);
  auto msg = std::make_shared<const Nmea2000Msg>(
      static_cast<uint64_t>(127250), std::vector<unsigned char>{0x00},
      std::make_shared<const NavAddr2000>(), 6);
  auto dest = std::make_shared<const NavAddr2000>(
      "n2k-iface", static_cast<unsigned char>(0x42));

  auto frames = decoder.Encode(msg, dest);
  ASSERT_EQ(frames.size(), 1u);
  N2kGatewayFramer framer;
  auto app = framer.Feed(frames[0]);
  ASSERT_EQ(app.size(), 1u);
  EXPECT_EQ(app[0][6], 0x42);  // destination node, not broadcast
}

TEST(N2kDecoder, EncodeRejectsInputOnlyConnection) {
  N2kDecoder decoder(DS_TYPE_INPUT);
  auto msg = std::make_shared<const Nmea2000Msg>(
      static_cast<uint64_t>(127250), std::vector<unsigned char>{0x00},
      std::make_shared<const NavAddr2000>(), 6);
  EXPECT_TRUE(decoder.Encode(msg, nullptr).empty());
}

TEST(N2kDecoder, EncodeRejectsNon2000Message) {
  N2kDecoder decoder(DS_TYPE_INPUT_OUTPUT);
  std::shared_ptr<const NavMsg> n0183 = std::make_shared<const Nmea0183Msg>(
      "GPGGA", "$GPGGA,,*00", std::make_shared<const NavAddr>());
  EXPECT_TRUE(decoder.Encode(n0183, nullptr).empty());
}
