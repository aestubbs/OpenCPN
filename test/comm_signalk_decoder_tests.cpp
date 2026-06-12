/**
 * Unit tests for SignalKDecoder.
 *
 * One frame = one JSON document from the server's websocket stream; the
 * decoder needs no transport or hardware.
 */

#include <memory>

#include <QByteArray>

#include <gtest/gtest.h>

#include "model/comm_signalk_decoder.h"

/** A CommFrame from a string literal. */
static CommFrame Frame(const std::string& s) {
  return QByteArray::fromStdString(s);
}

static const std::shared_ptr<const NavAddr> kSrc =
    std::make_shared<const NavAddr>(NavAddr::Bus::Signalk, "test-iface");

// A node.js-server hello: bare self id (no "vessels." prefix).
static const char* kHelloNodeJs =
    R"({"version":"1.46.3","self":"urn:mrn:signalk:uuid:1234","roles":[]})";

TEST(SignalKDecoder, WrapsDocumentInSignalkMsg) {
  SignalKDecoder decoder("192.168.1.10:3000");
  auto msgs = decoder.Decode(Frame(kHelloNodeJs), kSrc);
  ASSERT_EQ(msgs.size(), 1);
  EXPECT_EQ(msgs[0]->bus, NavAddr::Bus::Signalk);
  auto sk = std::dynamic_pointer_cast<const SignalkMsg>(msgs[0]);
  ASSERT_TRUE(sk);
  EXPECT_EQ(sk->raw_message, std::string(kHelloNodeJs));
  EXPECT_EQ(sk->source->iface, "192.168.1.10:3000");
}

TEST(SignalKDecoder, NormalizesBareSelfId) {
  SignalKDecoder decoder("host:3000");
  decoder.Decode(Frame(kHelloNodeJs), kSrc);
  // node.js servers send the bare id; the decoder restores the prefix.
  EXPECT_EQ(decoder.self(), "vessels.urn:mrn:signalk:uuid:1234");
}

TEST(SignalKDecoder, KeepsPrefixedSelfId) {
  SignalKDecoder decoder("host:3000");
  decoder.Decode(
      Frame(R"({"self":"vessels.urn:mrn:signalk:uuid:abcd"})"), kSrc);
  EXPECT_EQ(decoder.self(), "vessels.urn:mrn:signalk:uuid:abcd");
}

TEST(SignalKDecoder, TracksContextAcrossFrames) {
  SignalKDecoder decoder("host:3000");
  decoder.Decode(Frame(kHelloNodeJs), kSrc);
  decoder.Decode(
      Frame(R"({"context":"vessels.urn:mrn:imo:mmsi:230099999"})"), kSrc);
  EXPECT_EQ(decoder.context(), "vessels.urn:mrn:imo:mmsi:230099999");
  // Self is sticky -- a context switch does not change it.
  EXPECT_EQ(decoder.self(), "vessels.urn:mrn:signalk:uuid:1234");
  // The carried context rides on the produced message.
  auto msgs = decoder.Decode(Frame(R"({"updates":[]})"), kSrc);
  ASSERT_EQ(msgs.size(), 1);
  auto sk = std::dynamic_pointer_cast<const SignalkMsg>(msgs[0]);
  ASSERT_TRUE(sk);
  EXPECT_EQ(sk->context, "vessels.urn:mrn:imo:mmsi:230099999");
  EXPECT_EQ(sk->context_self, "vessels.urn:mrn:signalk:uuid:1234");
}

TEST(SignalKDecoder, RejectsMalformedJson) {
  SignalKDecoder decoder("host:3000");
  auto msgs = decoder.Decode(Frame("{not json"), kSrc);
  EXPECT_EQ(msgs.size(), 0);
}

TEST(SignalKDecoder, RejectsNonObjectDocument) {
  SignalKDecoder decoder("host:3000");
  auto msgs = decoder.Decode(Frame(R"(["array","payload"])"), kSrc);
  EXPECT_EQ(msgs.size(), 0);
}

TEST(SignalKDecoder, EncodeYieldsNothing) {
  SignalKDecoder decoder("host:3000");
  auto msg = std::make_shared<const SignalkMsg>("self", "ctx", "{}", "i");
  EXPECT_EQ(decoder.Encode(msg, nullptr).size(), 0);
}
