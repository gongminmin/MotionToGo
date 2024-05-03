#include <cstdlib>
#include <filesystem>
#include <format>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <gtest/gtest.h>

namespace
{
    struct Image
    {
        uint32_t width;
        uint32_t height;
        std::vector<uint32_t> data;
    };

    Image LoadImage(const std::filesystem::path& file_path)
    {
        Image ret{};

        int width, height;
        auto* data = stbi_load(file_path.string().c_str(), &width, &height, nullptr, 4);
        if (data != nullptr)
        {
            ret.width = width;
            ret.height = height;

            const uint32_t* data_32 = reinterpret_cast<uint32_t*>(data);
            ret.data.assign(data_32, data_32 + width * height);

            stbi_image_free(data);
        }

        return ret;
    }

    void CompareImage(const Image& lhs, const Image& rhs, int ch_threshole)
    {
        ASSERT_EQ(lhs.width, rhs.width);
        ASSERT_EQ(lhs.height, rhs.height);
        ASSERT_EQ(lhs.data.size(), rhs.data.size());
        if (ch_threshole == 0)
        {
            ASSERT_EQ(lhs.data, rhs.data);
        }
        else
        {
            for (uint32_t y = 0; y < lhs.height; ++y)
            {
                for (uint32_t x = 0; x < lhs.width; ++x)
                {
                    const uint32_t offset = y * lhs.width + x;
                    const int lr = (lhs.data[offset] >> 0) & 0xFF;
                    const int lg = (lhs.data[offset] >> 8) & 0xFF;
                    const int lb = (lhs.data[offset] >> 16) & 0xFF;
                    const int rr = (rhs.data[offset] >> 0) & 0xFF;
                    const int rg = (rhs.data[offset] >> 8) & 0xFF;
                    const int rb = (rhs.data[offset] >> 16) & 0xFF;
                    ASSERT_LE(std::abs(lr - rr), ch_threshole);
                    ASSERT_LE(std::abs(lg - rg), ch_threshole);
                    ASSERT_LE(std::abs(lb - rb), ch_threshole);
                }
            }
        }
    }
} // namespace

namespace MotionToGo
{
    TEST(MotionToGoTest, ImageSeq)
    {
        EXPECT_EQ(std::system(std::format("{} -I \"{}ImageSeq\"", MOTION_TO_GO_APP, TEST_DATA_DIR).c_str()), 0);

        Image output_frame_1 = LoadImage(std::format("{}ImageSeq/Output/Frame_1.png", TEST_DATA_DIR));
        Image original_frame_1 = LoadImage(std::format("{}ImageSeq/Frame_1.png", TEST_DATA_DIR));
        CompareImage(output_frame_1, original_frame_1, 0);

        Image expected_frame_2 = LoadImage(std::format("{}ImageSeq/Expected/ImageSeq_Frame_2.png", TEST_DATA_DIR));
        Image output_frame_2 = LoadImage(std::format("{}ImageSeq/Output/Frame_2.png", TEST_DATA_DIR));
        CompareImage(output_frame_2, expected_frame_2, 0);
    }

    TEST(MotionToGoTest, ImageSeqOverlay)
    {
        EXPECT_EQ(std::system(std::format("{} -I \"{}ImageSeq\" -L", MOTION_TO_GO_APP, TEST_DATA_DIR).c_str()), 0);

        Image output_frame_1 = LoadImage(std::format("{}ImageSeq/Output/Frame_1.png", TEST_DATA_DIR));
        Image original_frame_1 = LoadImage(std::format("{}ImageSeq/Frame_1.png", TEST_DATA_DIR));
        CompareImage(output_frame_1, original_frame_1, 0);

        Image expected_frame_2 = LoadImage(std::format("{}ImageSeq/Expected/ImageSeqOverlay_Frame_2.png", TEST_DATA_DIR));
        Image output_frame_2 = LoadImage(std::format("{}ImageSeq/Output/Frame_2.png", TEST_DATA_DIR));
        CompareImage(output_frame_2, expected_frame_2, 0);
    }

    TEST(MotionToGoTest, ImageSeqFramerate)
    {
        EXPECT_EQ(std::system(std::format("{} -I \"{}ImageSeq\" -F 60", MOTION_TO_GO_APP, TEST_DATA_DIR).c_str()), 0);

        Image output_frame_1 = LoadImage(std::format("{}ImageSeq/Output/Frame_1.png", TEST_DATA_DIR));
        Image original_frame_1 = LoadImage(std::format("{}ImageSeq/Frame_1.png", TEST_DATA_DIR));
        CompareImage(output_frame_1, original_frame_1, 0);

        Image expected_frame_2 = LoadImage(std::format("{}ImageSeq/Expected/ImageSeqFramerate_Frame_2.png", TEST_DATA_DIR));
        Image output_frame_2 = LoadImage(std::format("{}ImageSeq/Output/Frame_2.png", TEST_DATA_DIR));
        CompareImage(output_frame_2, expected_frame_2, 0);
    }

    TEST(MotionToGoTest, Video)
    {
        EXPECT_EQ(std::system(std::format("{} -I \"{}Video/3719155-hd_1920_1080_8fps.mp4\"", MOTION_TO_GO_APP, TEST_DATA_DIR).c_str()), 0);

        const uint32_t check_frames[] = {1, 14, 39, 56};
        for (const uint32_t frame : check_frames)
        {
            Image expected_frame = LoadImage(std::format("{}Video/Expected/Frame_{}.png", TEST_DATA_DIR, frame));
            Image output_frame = LoadImage(std::format("{}Video/Output/Frame_{}.png", TEST_DATA_DIR, frame));
            CompareImage(output_frame, expected_frame, 5);
        }
    }
} // namespace MotionToGo

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);

    int ret = RUN_ALL_TESTS();
    if (ret != 0)
    {
        [[maybe_unused]] int ch = getchar();
    }

    return ret;
}
