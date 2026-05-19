/**
 * Unit tests for the comms-framework Framer classes.
 *
 * Framers carry wire data as QByteArray.
 */

#include <cstdint>
#include <initializer_list>
#include <string>

#include <QByteArray>

#include <gtest/gtest.h>

#include "model/comm_framer.h"

/** A QByteArray from a string literal. */
static QByteArray Bytes(const std::string& s) {
  return QByteArray::fromStdString(s);
}

/** A QByteArray from a list of byte values. */
static QByteArray Bytes(std::initializer_list<int> values) {
  QByteArray b;
  for (int v : values) b.append(static_cast<char>(v));
  return b;
}

TEST(Framer, PassThroughEmitsChunkAsOneFrame) {
  PassThroughFramer framer;
  auto frames = framer.Feed(Bytes("a complete frame"));
  ASSERT_EQ(frames.size(), 1);
  EXPECT_EQ(frames[0], Bytes("a complete frame"));
}

TEST(Framer, PassThroughEmptyInputYieldsNoFrame) {
  PassThroughFramer framer;
  EXPECT_TRUE(framer.Feed({}).isEmpty());
}

TEST(Framer, LineFramerSplitsConsecutiveSentences) {
  LineFramer framer;
  auto frames = framer.Feed(Bytes("$AAA,1,2*70\r\n$BBB,3*71\r\n"));
  EXPECT_EQ(frames.size(), 2);
}

TEST(Framer, LineFramerBuffersAcrossChunks) {
  LineFramer framer;
  // A sentence split mid-payload across two transport chunks.
  auto first = framer.Feed(Bytes("$AAA,1,"));
  EXPECT_TRUE(first.isEmpty());  // no terminator yet -- nothing complete
  auto second = framer.Feed(Bytes("2,3*70\r\n"));
  EXPECT_EQ(second.size(), 1);  // chunk completes the sentence
}

// --- N2kGatewayFramer -------------------------------------------------------

/** Wrap application data in an Actisense <ESC><STX> ... <ESC><ETX> packet,
 *  doubling any ESC byte inside the data. */
static QByteArray N2kPacket(const QByteArray& data) {
  QByteArray wire;
  wire.append(kN2kEscape);
  wire.append(kN2kStartOfText);
  for (char c : data) {
    if (static_cast<uint8_t>(c) == static_cast<uint8_t>(kN2kEscape))
      wire.append(kN2kEscape);
    wire.append(c);
  }
  wire.append(kN2kEscape);
  wire.append(kN2kEndOfText);
  return wire;
}

TEST(Framer, N2kGatewayExtractsApplicationData) {
  N2kGatewayFramer framer;
  const QByteArray data = Bytes({0x93, 0x05, 0x02, 0x01, 0xF8, 0x01, 0xBB});
  auto frames = framer.Feed(N2kPacket(data));
  ASSERT_EQ(frames.size(), 1);
  EXPECT_EQ(frames[0], data);
}

TEST(Framer, N2kGatewayUnescapesDoubledEscape) {
  N2kGatewayFramer framer;
  // An ESC (0x10) inside the payload arrives doubled on the wire.
  const QByteArray data = Bytes({0x93, kN2kEscape, 0x42});
  auto frames = framer.Feed(N2kPacket(data));
  ASSERT_EQ(frames.size(), 1);
  EXPECT_EQ(frames[0], data);
}

TEST(Framer, N2kGatewaySplitsConsecutivePackets) {
  N2kGatewayFramer framer;
  QByteArray wire = N2kPacket(Bytes({0xA0, 0x01}));
  wire.append(N2kPacket(Bytes({0x93, 0x02})));
  EXPECT_EQ(framer.Feed(wire).size(), 2);
}

TEST(Framer, N2kGatewayBuffersAcrossChunks) {
  N2kGatewayFramer framer;
  const QByteArray wire = N2kPacket(Bytes({0x93, 0x11, 0x22, 0x33}));
  const int cut = wire.size() / 2;
  auto first = framer.Feed(wire.left(cut));
  EXPECT_TRUE(first.isEmpty());  // packet not yet terminated
  auto second = framer.Feed(wire.mid(cut));
  EXPECT_EQ(second.size(), 1);
}

TEST(Framer, N2kGatewayIgnoresBytesOutsidePackets) {
  N2kGatewayFramer framer;
  QByteArray wire = Bytes({0xFF, 0x00, 0x12});  // line noise
  wire.append(N2kPacket(Bytes({0x93, 0x07})));
  auto frames = framer.Feed(wire);
  ASSERT_EQ(frames.size(), 1);
  EXPECT_EQ(frames[0], Bytes({0x93, 0x07}));
}
