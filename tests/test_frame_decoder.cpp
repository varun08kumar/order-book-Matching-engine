#include "matching_engine/net/frame_decoder.hpp"

#include <gtest/gtest.h>

using namespace me::net;

TEST(FrameDecoder, ExtractsSingleFrameDeliveredWhole) {
  std::vector<std::uint8_t> body = {1, 2, 3, 4, 5};
  auto frame = FrameMessage(body);

  FrameDecoder decoder;
  decoder.Feed(frame.data(), frame.size());

  std::vector<std::uint8_t> out;
  bool err = false;
  ASSERT_TRUE(decoder.TryExtractFrame(out, err));
  EXPECT_FALSE(err);
  EXPECT_EQ(out, body);
  EXPECT_FALSE(decoder.TryExtractFrame(out, err));
}

TEST(FrameDecoder, HandlesFragmentationAcrossManyFeeds) {
  std::vector<std::uint8_t> body = {10, 20, 30, 40, 50, 60, 70};
  auto frame = FrameMessage(body);

  FrameDecoder decoder;
  std::vector<std::uint8_t> out;
  bool err = false;

  // Feed one byte at a time -- worst-case fragmentation.
  for (std::size_t i = 0; i + 1 < frame.size(); ++i) {
    decoder.Feed(&frame[i], 1);
    EXPECT_FALSE(decoder.TryExtractFrame(out, err));
  }
  decoder.Feed(&frame.back(), 1);
  ASSERT_TRUE(decoder.TryExtractFrame(out, err));
  EXPECT_EQ(out, body);
}

TEST(FrameDecoder, HandlesCoalescedFramesInOneFeed) {
  std::vector<std::uint8_t> body1 = {1, 2, 3};
  std::vector<std::uint8_t> body2 = {4, 5, 6, 7};
  std::vector<std::uint8_t> body3 = {};

  auto f1 = FrameMessage(body1);
  auto f2 = FrameMessage(body2);
  auto f3 = FrameMessage(body3);

  std::vector<std::uint8_t> coalesced;
  coalesced.insert(coalesced.end(), f1.begin(), f1.end());
  coalesced.insert(coalesced.end(), f2.begin(), f2.end());
  coalesced.insert(coalesced.end(), f3.begin(), f3.end());

  FrameDecoder decoder;
  decoder.Feed(coalesced.data(), coalesced.size());

  std::vector<std::uint8_t> out;
  bool err = false;
  ASSERT_TRUE(decoder.TryExtractFrame(out, err));
  EXPECT_EQ(out, body1);
  ASSERT_TRUE(decoder.TryExtractFrame(out, err));
  EXPECT_EQ(out, body2);
  ASSERT_TRUE(decoder.TryExtractFrame(out, err));
  EXPECT_TRUE(out.empty());
  EXPECT_FALSE(decoder.TryExtractFrame(out, err));
}

TEST(FrameDecoder, PartialFrameThenMoreCoalescedFrames) {
  std::vector<std::uint8_t> body1 = {1, 2, 3, 4, 5};
  std::vector<std::uint8_t> body2 = {9, 9};
  auto f1 = FrameMessage(body1);
  auto f2 = FrameMessage(body2);

  FrameDecoder decoder;
  // Feed first frame's prefix + partial body only.
  decoder.Feed(f1.data(), 4 + 2);
  std::vector<std::uint8_t> out;
  bool err = false;
  EXPECT_FALSE(decoder.TryExtractFrame(out, err));

  // Now feed the rest of frame 1 plus all of frame 2 in one chunk.
  std::vector<std::uint8_t> rest(f1.begin() + 6, f1.end());
  rest.insert(rest.end(), f2.begin(), f2.end());
  decoder.Feed(rest.data(), rest.size());

  ASSERT_TRUE(decoder.TryExtractFrame(out, err));
  EXPECT_EQ(out, body1);
  ASSERT_TRUE(decoder.TryExtractFrame(out, err));
  EXPECT_EQ(out, body2);
}

TEST(FrameDecoder, RejectsOversizedFrame) {
  std::vector<std::uint8_t> huge_len_prefix = {0, 0, 0, 0};
  std::uint32_t bogus_len = FrameDecoder::kMaxFrameBytes + 1;
  for (int i = 0; i < 4; ++i) {
    huge_len_prefix[static_cast<std::size_t>(i)] =
        static_cast<std::uint8_t>(bogus_len >> (8 * i));
  }

  FrameDecoder decoder;
  decoder.Feed(huge_len_prefix.data(), huge_len_prefix.size());
  std::vector<std::uint8_t> out;
  bool err = false;
  EXPECT_FALSE(decoder.TryExtractFrame(out, err));
  EXPECT_TRUE(err);
}
