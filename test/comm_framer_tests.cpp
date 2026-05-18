/**
 * Unit tests for the comms-framework Framer classes (task P1.5i).
 *
 * Framers are pure -- these run with no transport, no Qt event loop and no
 * hardware, which is the point of separating framing from transport.
 */

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "model/comm_framer.h"

static std::vector<uint8_t> Bytes(const std::string& s) {
  return std::vector<uint8_t>(s.begin(), s.end());
}

TEST(Framer, PassThroughEmitsChunkAsOneFrame) {
  PassThroughFramer framer;
  auto frames = framer.Feed(Bytes("a complete frame"));
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(frames[0], Bytes("a complete frame"));
}

TEST(Framer, PassThroughEmptyInputYieldsNoFrame) {
  PassThroughFramer framer;
  EXPECT_TRUE(framer.Feed({}).empty());
}

TEST(Framer, LineFramerSplitsConsecutiveSentences) {
  LineFramer framer;
  auto frames = framer.Feed(Bytes("$AAA,1,2*70\r\n$BBB,3*71\r\n"));
  EXPECT_EQ(frames.size(), 2u);
}

TEST(Framer, LineFramerBuffersAcrossChunks) {
  LineFramer framer;
  // A sentence split mid-payload across two transport chunks.
  auto first = framer.Feed(Bytes("$AAA,1,"));
  EXPECT_TRUE(first.empty());  // no terminator yet -- nothing complete
  auto second = framer.Feed(Bytes("2,3*70\r\n"));
  EXPECT_EQ(second.size(), 1u);  // chunk completes the sentence
}

// --- N2kGatewayFramer -------------------------------------------------------

/** Wrap application data in an Actisense <ESC><STX> ... <ESC><ETX> packet,
 *  doubling any ESC byte inside the data. */
static std::vector<uint8_t> N2kPacket(const std::vector<uint8_t>& data) {
  std::vector<uint8_t> wire = {kN2kEscape, kN2kStartOfText};
  for (uint8_t b : data) {
    if (b == kN2kEscape) wire.push_back(kN2kEscape);
    wire.push_back(b);
  }
  wire.push_back(kN2kEscape);
  wire.push_back(kN2kEndOfText);
  return wire;
}

TEST(Framer, N2kGatewayExtractsApplicationData) {
  N2kGatewayFramer framer;
  const CommFrame data = {0x93, 0x05, 0x02, 0x01, 0xF8, 0x01, 0xBB};
  auto frames = framer.Feed(N2kPacket(data));
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(frames[0], data);
}

TEST(Framer, N2kGatewayUnescapesDoubledEscape) {
  N2kGatewayFramer framer;
  // An ESC (0x10) inside the payload arrives doubled on the wire.
  const CommFrame data = {0x93, kN2kEscape, 0x42};
  auto frames = framer.Feed(N2kPacket(data));
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(frames[0], data);
}

TEST(Framer, N2kGatewaySplitsConsecutivePackets) {
  N2kGatewayFramer framer;
  std::vector<uint8_t> wire = N2kPacket({0xA0, 0x01});
  const std::vector<uint8_t> second = N2kPacket({0x93, 0x02});
  wire.insert(wire.end(), second.begin(), second.end());
  EXPECT_EQ(framer.Feed(wire).size(), 2u);
}

TEST(Framer, N2kGatewayBuffersAcrossChunks) {
  N2kGatewayFramer framer;
  const std::vector<uint8_t> wire = N2kPacket({0x93, 0x11, 0x22, 0x33});
  const size_t cut = wire.size() / 2;
  auto first = framer.Feed({wire.begin(), wire.begin() + cut});
  EXPECT_TRUE(first.empty());  // packet not yet terminated
  auto second = framer.Feed({wire.begin() + cut, wire.end()});
  EXPECT_EQ(second.size(), 1u);
}

TEST(Framer, N2kGatewayIgnoresBytesOutsidePackets) {
  N2kGatewayFramer framer;
  std::vector<uint8_t> wire = {0xFF, 0x00, 0x12};  // line noise
  const std::vector<uint8_t> pkt = N2kPacket({0x93, 0x07});
  wire.insert(wire.end(), pkt.begin(), pkt.end());
  auto frames = framer.Feed(wire);
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(frames[0], (CommFrame{0x93, 0x07}));
}
