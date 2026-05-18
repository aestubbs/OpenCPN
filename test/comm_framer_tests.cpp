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
