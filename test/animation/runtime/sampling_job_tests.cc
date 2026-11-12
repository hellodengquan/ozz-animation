//----------------------------------------------------------------------------//
//                                                                            //
// ozz-animation is hosted at http://github.com/guillaumeblanc/ozz-animation  //
// and distributed under the MIT License (MIT).                               //
//                                                                            //
// Copyright (c) Guillaume Blanc                                              //
//                                                                            //
// Permission is hereby granted, free of charge, to any person obtaining a    //
// copy of this software and associated documentation files (the "Software"), //
// to deal in the Software without restriction, including without limitation  //
// the rights to use, copy, modify, merge, publish, distribute, sublicense,   //
// and/or sell copies of the Software, and to permit persons to whom the      //
// Software is furnished to do so, subject to the following conditions:       //
//                                                                            //
// The above copyright notice and this permission notice shall be included in //
// all copies or substantial portions of the Software.                        //
//                                                                            //
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR //
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   //
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    //
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER //
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING    //
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER        //
// DEALINGS IN THE SOFTWARE.                                                  //
//                                                                            //
//----------------------------------------------------------------------------//

#include "gtest/gtest.h"
#include "ozz/animation/offline/animation_builder.h"
#include "ozz/animation/offline/raw_animation.h"
#include "ozz/animation/runtime/animation.h"
#include "ozz/animation/runtime/sampling_job.h"
#include "ozz/base/maths/gtest_math_helper.h"
#include "ozz/base/maths/soa_transform.h"
#include "ozz/base/memory/unique_ptr.h"

using ozz::animation::Animation;
using ozz::animation::SamplingJob;
using ozz::animation::offline::AnimationBuilder;
using ozz::animation::offline::RawAnimation;

ozz::math::SoaTransform MakeTransform(float _t) {
  auto v = ozz::math::simd_float4::Load(_t, _t, _t, _t);
  return {{v, v, v}, {v, v, v, v}, {v, v, v}};
}

TEST(JobValidity, SamplingJob) {
  RawAnimation raw_animation;
  raw_animation.duration = 1.f;
  raw_animation.tracks.resize(1);

  AnimationBuilder builder;
  ozz::unique_ptr<Animation> animation(builder(raw_animation));
  ASSERT_TRUE(animation);

  // Allocates context.
  SamplingJob::Context context(1);

  {  // Empty/default job
    SamplingJob job;
    EXPECT_FALSE(job.Validate());
    EXPECT_FALSE(job.Run());
  }

  {  // Invalid output
    SamplingJob job;
    job.animation = animation.get();
    job.context = &context;
    EXPECT_FALSE(job.Validate());
    EXPECT_FALSE(job.Run());
  }

  {  // Invalid animation.
    ozz::math::SoaTransform output[1];

    SamplingJob job;
    job.context = &context;
    job.output = output;
    EXPECT_FALSE(job.Validate());
    EXPECT_FALSE(job.Run());
  }

  {  // Invalid context.
    ozz::math::SoaTransform output[1];

    SamplingJob job;
    job.animation = animation.get();
    job.output = output;
    EXPECT_FALSE(job.Validate());
    EXPECT_FALSE(job.Run());
  }

  {  // Invalid context size.
    SamplingJob::Context zero_cache(0);
    ozz::math::SoaTransform output[1];

    SamplingJob job;
    job.animation = animation.get();
    job.context = &zero_cache;
    job.output = output;
    EXPECT_FALSE(job.Validate());
    EXPECT_FALSE(job.Run());
  }

  {  // Invalid job with empty output.
    ozz::math::SoaTransform* output = nullptr;
    SamplingJob job;
    job.ratio =
        2155.f;  // Any time ratio can be set, it's clamped in unit interval.
    job.animation = animation.get();
    job.context = &context;
    job.output = ozz::span<ozz::math::SoaTransform>(output, size_t(0));
    EXPECT_FALSE(job.Validate());
    EXPECT_FALSE(job.Run());
  }

  {  // valid job with output smaller than animation, but not empty.
    RawAnimation big_raw_animation;
    big_raw_animation.duration = 1.f;
    big_raw_animation.tracks.resize(2);
    ozz::unique_ptr<Animation> big_animation(builder(big_raw_animation));
    ASSERT_TRUE(big_animation);

    ozz::math::SoaTransform output[1];
    SamplingJob job;
    job.ratio =
        2155.f;  // Any time ratio can be set, it's clamped in unit interval.
    job.animation = big_animation.get();
    job.context = &context;
    job.output = output;
    EXPECT_TRUE(job.Validate());
    EXPECT_TRUE(job.Run());
  }

  {  // Valid job.
    ozz::math::SoaTransform output[1];
    SamplingJob job;
    job.ratio = 2155.f;  // Any time can be set.
    job.animation = animation.get();
    job.context = &context;
    job.output = output;
    EXPECT_TRUE(job.Validate());
    EXPECT_TRUE(job.Run());
  }

  {  // Valid job with bigger context.
    SamplingJob::Context big_cache(2);
    ozz::math::SoaTransform output[1];
    SamplingJob job;
    job.ratio = 2155.f;  // Any time can be set.
    job.animation = animation.get();
    job.context = &big_cache;
    job.output = output;
    EXPECT_TRUE(job.Validate());
    EXPECT_TRUE(job.Run());
  }

  {  // Valid job with bigger output.
    ozz::math::SoaTransform output[2];
    SamplingJob job;
    job.ratio = 2155.f;  // Any time can be set.
    job.animation = animation.get();
    job.context = &context;
    job.output = output;
    EXPECT_TRUE(job.Validate());
    EXPECT_TRUE(job.Run());
  }

  {  // Default animation.
    ozz::math::SoaTransform output[1];
    Animation default_animation;
    SamplingJob job;
    job.animation = &default_animation;
    job.context = &context;
    job.output = output;
    EXPECT_TRUE(job.Validate());
    EXPECT_TRUE(job.Run());
  }
}

TEST(Sampling, SamplingJob) {
  // Instantiates a builder objects with default parameters.
  AnimationBuilder builder;

  // Building an Animation with unsorted keys fails.
  RawAnimation raw_animation;
  raw_animation.duration = 1.f;
  raw_animation.tracks.resize(4);

  SamplingJob::Context context(4);

  // Raw animation inputs.
  //     0                 1
  // -----------------------
  // 0 - |  A              |
  // 1 - |                 |
  // 2 - B  C   D   E      F
  // 3 - |  G       H      |

  // Final animation.
  //     0                 1
  // -----------------------
  // 0 - A-1               4
  // 1 - 1                 5
  // 2 - B2 C6  D8 E10    F11
  // 3 - 3  G7     H9      12

  struct {
    float sample_time;
    float trans[12];
  } result[] = {
      {-.2f, {-1.f, 0.f, 2.f, 7.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}},
      {.0f, {-1.f, 0.f, 2.f, 7.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}},
      {.0000001f,
       {-1.f, 0.f, 2.f, 7.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}},
      {.1f, {-1.f, 0.f, 4.f, 7.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}},
      {.2f, {-1.f, 0.f, 6.f, 7.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}},
      {.3f, {-1.f, 0.f, 7.f, 7.5, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}},
      {.4f, {-1.f, 0.f, 8.f, 8.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}},
      {.3999999f,
       {-1.f, 0.f, 8.f, 8.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}},
      {.4000001f,
       {-1.f, 0.f, 8.f, 8.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}},
      {.5f, {-1.f, 0.f, 9.f, 8.5f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}},
      {.6f, {-1.f, 0.f, 10.f, 9.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}},
      {.9999999f,
       {-1.f, 0.f, 11.f, 9.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}},
      {1.f, {-1.f, 0.f, 11.f, 9.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}},
      {1.000001f,
       {-1.f, 0.f, 11.f, 9.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}},
      {.5f, {-1.f, 0.f, 9.f, 8.5f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}},
      {.9999999f,
       {-1.f, 0.f, 11.f, 9.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}},
      {.0000001f,
       {-1.f, 0.f, 2.f, 7.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}}};

  RawAnimation::TranslationKey a = {.2f, ozz::math::Float3(-1.f, 0.f, 0.f)};
  raw_animation.tracks[0].translations.push_back(a);

  RawAnimation::TranslationKey b = {0.f, ozz::math::Float3(2.f, 0.f, 0.f)};
  raw_animation.tracks[2].translations.push_back(b);
  RawAnimation::TranslationKey c = {0.2f, ozz::math::Float3(6.f, 0.f, 0.f)};
  raw_animation.tracks[2].translations.push_back(c);
  RawAnimation::TranslationKey d = {0.4f, ozz::math::Float3(8.f, 0.f, 0.f)};
  raw_animation.tracks[2].translations.push_back(d);
  RawAnimation::TranslationKey e = {0.6f, ozz::math::Float3(10.f, 0.f, 0.f)};
  raw_animation.tracks[2].translations.push_back(e);
  RawAnimation::TranslationKey f = {1.f, ozz::math::Float3(11.f, 0.f, 0.f)};
  raw_animation.tracks[2].translations.push_back(f);

  RawAnimation::TranslationKey g = {0.2f, ozz::math::Float3(7.f, 0.f, 0.f)};
  raw_animation.tracks[3].translations.push_back(g);
  RawAnimation::TranslationKey h = {0.6f, ozz::math::Float3(9.f, 0.f, 0.f)};
  raw_animation.tracks[3].translations.push_back(h);

  // Builds animation
  ozz::unique_ptr<Animation> animation(builder(raw_animation));
  ASSERT_TRUE(animation);

  ozz::math::SoaTransform output[1];

  SamplingJob job;
  job.animation = animation.get();
  job.context = &context;
  job.output = output;

  for (size_t i = 0; i < OZZ_ARRAY_SIZE(result); ++i) {
    output[0] = MakeTransform(93.f);
    job.ratio = result[i].sample_time / animation->duration();
    EXPECT_TRUE(job.Validate());
    EXPECT_TRUE(job.Run());

    EXPECT_SOAFLOAT3_EQ_EST(
        output[0].translation, result[i].trans[0], result[i].trans[1],
        result[i].trans[2], result[i].trans[3], result[i].trans[4],
        result[i].trans[5], result[i].trans[6], result[i].trans[7],
        result[i].trans[8], result[i].trans[9], result[i].trans[10],
        result[i].trans[11]);
    EXPECT_SOAQUATERNION_EQ_EST(output[0].rotation, 0.f, 0.f, 0.f, 0.f, 0.f,
                                0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 1.f,
                                1.f, 1.f);
    EXPECT_SOAFLOAT3_EQ_EST(output[0].scale, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f,
                            1.f, 1.f, 1.f, 1.f, 1.f);
  }
}

TEST(SamplingNoTrack, SamplingJob) {
  RawAnimation raw_animation;
  raw_animation.duration = 46.f;

  SamplingJob::Context context(1);

  AnimationBuilder builder;
  ozz::unique_ptr<Animation> animation(builder(raw_animation));
  ASSERT_TRUE(animation);

  ozz::math::SoaTransform test_output[1] = {MakeTransform(93.f)};
  ozz::math::SoaTransform output[1] = {MakeTransform(93.f)};

  SamplingJob job;
  job.ratio = 0.f;
  job.animation = animation.get();
  job.context = &context;
  job.output = output;
  EXPECT_TRUE(job.Validate());
  EXPECT_TRUE(job.Run());

  // Tests output.
  EXPECT_EQ(memcmp(test_output, output, sizeof(output)), 0);
}

TEST(Sampling1Track0Key, SamplingJob) {
  RawAnimation raw_animation;
  raw_animation.duration = 46.f;
  raw_animation.tracks.resize(1);  // Adds a joint.

  SamplingJob::Context context(1);

  AnimationBuilder builder;
  ozz::unique_ptr<Animation> animation(builder(raw_animation));
  ASSERT_TRUE(animation);

  ozz::math::SoaTransform output[1];

  SamplingJob job;
  job.animation = animation.get();
  job.context = &context;
  job.output = output;

  for (float t = -.2f; t < 1.2f; t += .1f) {
    output[0] = MakeTransform(93.f);
    job.ratio = t;
    EXPECT_TRUE(job.Validate());
    EXPECT_TRUE(job.Run());
    EXPECT_SOAFLOAT3_EQ_EST(output[0].translation, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f,
                            0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    EXPECT_SOAQUATERNION_EQ_EST(output[0].rotation, 0.f, 0.f, 0.f, 0.f, 0.f,
                                0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 1.f,
                                1.f, 1.f);
    EXPECT_SOAFLOAT3_EQ_EST(output[0].scale, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f,
                            1.f, 1.f, 1.f, 1.f, 1.f);
  }
}

TEST(Sampling1Track1Key, SamplingJob) {
  RawAnimation raw_animation;
  raw_animation.duration = 46.f;
  raw_animation.tracks.resize(1);  // Adds a joint.

  SamplingJob::Context context(1);

  const RawAnimation::TranslationKey tkey = {.3f,
                                             ozz::math::Float3(1.f, -1.f, 5.f)};
  raw_animation.tracks[0].translations.push_back(tkey);  // Adds a key.

  AnimationBuilder builder;
  ozz::unique_ptr<Animation> animation(builder(raw_animation));
  ASSERT_TRUE(animation);

  ozz::math::SoaTransform output[1];

  SamplingJob job;
  job.animation = animation.get();
  job.context = &context;
  job.output = output;

  for (float t = -.2f; t < 1.2f; t += .1f) {
    output[0] = MakeTransform(93.f);
    job.ratio = t;
    EXPECT_TRUE(job.Validate());
    EXPECT_TRUE(job.Run());
    EXPECT_SOAFLOAT3_EQ_EST(output[0].translation, 1.f, 0.f, 0.f, 0.f, -1.f,
                            0.f, 0.f, 0.f, 5.f, 0.f, 0.f, 0.f);
    EXPECT_SOAQUATERNION_EQ_EST(output[0].rotation, 0.f, 0.f, 0.f, 0.f, 0.f,
                                0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 1.f,
                                1.f, 1.f);
    EXPECT_SOAFLOAT3_EQ_EST(output[0].scale, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f,
                            1.f, 1.f, 1.f, 1.f, 1.f);
  }
}

TEST(Sampling1Track2Keys, SamplingJob) {
  RawAnimation raw_animation;
  raw_animation.duration = 46.f;
  raw_animation.tracks.resize(1);  // Adds a joint.

  SamplingJob::Context context(1);

  const RawAnimation::TranslationKey tkey0 = {.5f,
                                              ozz::math::Float3(1.f, 2.f, 4.f)};
  raw_animation.tracks[0].translations.push_back(tkey0);  // Adds a key.
  const RawAnimation::TranslationKey tkey1 = {.8f,
                                              ozz::math::Float3(2.f, 4.f, 8.f)};
  raw_animation.tracks[0].translations.push_back(tkey1);  // Adds a key.

  AnimationBuilder builder;
  ozz::unique_ptr<Animation> animation(builder(raw_animation));
  ASSERT_TRUE(animation);

  ozz::math::SoaTransform output[1] = {MakeTransform(93.f)};

  SamplingJob job;
  job.animation = animation.get();
  job.context = &context;
  job.output = output;

  // Samples at t = 0.
  job.ratio = 0.f;
  EXPECT_TRUE(job.Validate());
  EXPECT_TRUE(job.Run());
  EXPECT_SOAFLOAT3_EQ_EST(output[0].translation, 1.f, 0.f, 0.f, 0.f, 2.f, 0.f,
                          0.f, 0.f, 4.f, 0.f, 0.f, 0.f);
  EXPECT_SOAQUATERNION_EQ_EST(output[0].rotation, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f,
                              0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f, 1.f);
  EXPECT_SOAFLOAT3_EQ_EST(output[0].scale, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f,
                          1.f, 1.f, 1.f, 1.f, 1.f);

  // Samples at t = tkey0.
  job.ratio = tkey0.time / animation->duration();
  EXPECT_TRUE(job.Validate());
  EXPECT_TRUE(job.Run());
  EXPECT_SOAFLOAT3_EQ_EST(output[0].translation, 1.f, 0.f, 0.f, 0.f, 2.f, 0.f,
                          0.f, 0.f, 4.f, 0.f, 0.f, 0.f);
  EXPECT_SOAQUATERNION_EQ_EST(output[0].rotation, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f,
                              0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f, 1.f);
  EXPECT_SOAFLOAT3_EQ_EST(output[0].scale, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f,
                          1.f, 1.f, 1.f, 1.f, 1.f);

  // Samples at t = tkey1.
  job.ratio = tkey1.time / animation->duration();
  EXPECT_TRUE(job.Validate());
  EXPECT_TRUE(job.Run());
  EXPECT_SOAFLOAT3_EQ_EST(output[0].translation, 2.f, 0.f, 0.f, 0.f, 4.f, 0.f,
                          0.f, 0.f, 8.f, 0.f, 0.f, 0.f);
  EXPECT_SOAQUATERNION_EQ_EST(output[0].rotation, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f,
                              0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f, 1.f);
  EXPECT_SOAFLOAT3_EQ_EST(output[0].scale, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f,
                          1.f, 1.f, 1.f, 1.f, 1.f);

  // Samples at t = end.
  job.ratio = 1.f;
  EXPECT_TRUE(job.Validate());
  EXPECT_TRUE(job.Run());
  EXPECT_SOAFLOAT3_EQ_EST(output[0].translation, 2.f, 0.f, 0.f, 0.f, 4.f, 0.f,
                          0.f, 0.f, 8.f, 0.f, 0.f, 0.f);
  EXPECT_SOAQUATERNION_EQ_EST(output[0].rotation, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f,
                              0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f, 1.f);
  EXPECT_SOAFLOAT3_EQ_EST(output[0].scale, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f,
                          1.f, 1.f, 1.f, 1.f, 1.f);

  // Samples at tkey0.time < t < tkey1.time.
  job.ratio = (tkey0.time / animation->duration() +
               tkey1.time / animation->duration()) /
              2.f;
  EXPECT_TRUE(job.Validate());
  EXPECT_TRUE(job.Run());
  EXPECT_SOAFLOAT3_EQ_EST(output[0].translation, 1.5f, 0.f, 0.f, 0.f, 3.f, 0.f,
                          0.f, 0.f, 6.f, 0.f, 0.f, 0.f);
  EXPECT_SOAQUATERNION_EQ_EST(output[0].rotation, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f,
                              0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f, 1.f);
  EXPECT_SOAFLOAT3_EQ_EST(output[0].scale, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f,
                          1.f, 1.f, 1.f, 1.f, 1.f);
}

TEST(Sampling4Track2Keys, SamplingJob) {
  RawAnimation raw_animation;
  raw_animation.duration = 1.f;
  raw_animation.tracks.resize(4);  // Adds a joint.

  SamplingJob::Context context(1);

  const RawAnimation::TranslationKey tkey00 = {
      .5f, ozz::math::Float3(1.f, 2.f, 4.f)};
  raw_animation.tracks[0].translations.push_back(tkey00);  // Adds a key.
  const RawAnimation::TranslationKey tkey01 = {
      .8f, ozz::math::Float3(2.f, 4.f, 8.f)};
  raw_animation.tracks[0].translations.push_back(tkey01);  // Adds a key.

  // This quaternion will be negated as the builder ensures that the first key
  // is in identity quaternion hemisphere.
  const RawAnimation::RotationKey rkey10 = {
      0.f, ozz::math::Quaternion(0.f, 0.f, 0.f, -1.f)};
  raw_animation.tracks[1].rotations.push_back(rkey10);  // Adds a key.
  const RawAnimation::RotationKey rkey11 = {
      1.f, ozz::math::Quaternion(0.f, 1.f, 0.f, 0.f)};
  raw_animation.tracks[1].rotations.push_back(rkey11);  // Adds a key.

  const RawAnimation::ScaleKey skey20 = {.5f, ozz::math::Float3(0.f, 0.f, 0.f)};
  raw_animation.tracks[2].scales.push_back(skey20);  // Adds a key.
  const RawAnimation::ScaleKey skey21 = {.8f,
                                         ozz::math::Float3(-1.f, -1.f, -1.f)};
  raw_animation.tracks[2].scales.push_back(skey21);  // Adds a key.

  const RawAnimation::TranslationKey tkey30 = {
      0.f, ozz::math::Float3(-1.f, -2.f, -4.f)};
  raw_animation.tracks[3].translations.push_back(tkey30);  // Adds a key.
  const RawAnimation::TranslationKey tkey31 = {
      1.f, ozz::math::Float3(-2.f, -4.f, -8.f)};
  raw_animation.tracks[3].translations.push_back(tkey31);  // Adds a key.

  AnimationBuilder builder;
  ozz::unique_ptr<Animation> animation(builder(raw_animation));
  ASSERT_TRUE(animation);

  ozz::math::SoaTransform output[1] = {MakeTransform(93.f)};

  SamplingJob job;
  job.animation = animation.get();
  job.context = &context;
  job.output = output;

  // Samples at t = 0.
  job.ratio = 0.f;
  EXPECT_TRUE(job.Validate());
  EXPECT_TRUE(job.Run());
  EXPECT_SOAFLOAT3_EQ_EST(output[0].translation, 1.f, 0.f, 0.f, -1.f, 2.f, 0.f,
                          0.f, -2.f, 4.f, 0.f, 0.f, -4.f);
  EXPECT_SOAQUATERNION_EQ_EST(output[0].rotation, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f,
                              0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f, 1.f);
  EXPECT_SOAFLOAT3_EQ_EST(output[0].scale, 1.f, 1.f, 0.f, 1.f, 1.f, 1.f, 0.f,
                          1.f, 1.f, 1.f, 0.f, 1.f);

  // Samples at t = tkey00.
  job.ratio = tkey00.time / animation->duration();
  EXPECT_TRUE(job.Validate());
  EXPECT_TRUE(job.Run());
  EXPECT_SOAFLOAT3_EQ_EST(output[0].translation, 1.f, 0.f, 0.f, -1.5f, 2.f, 0.f,
                          0.f, -3.f, 4.f, 0.f, 0.f, -6.f);
  EXPECT_SOAQUATERNION_EQ_EST(output[0].rotation, 0.f, 0.f, 0.f, 0.f, 0.f,
                              .7071067f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f,
                              .7071067f, 1.f, 1.f);
  EXPECT_SOAFLOAT3_EQ_EST(output[0].scale, 1.f, 1.f, 0.f, 1.f, 1.f, 1.f, 0.f,
                          1.f, 1.f, 1.f, 0.f, 1.f);

  // Samples at t = end.
  job.ratio = 1.f;
  EXPECT_TRUE(job.Validate());
  EXPECT_TRUE(job.Run());
  EXPECT_SOAFLOAT3_EQ_EST(output[0].translation, 2.f, 0.f, 0.f, -2.f, 4.f, 0.f,
                          0.f, -4.f, 8.f, 0.f, 0.f, -8.f);
  EXPECT_SOAQUATERNION_EQ_EST(output[0].rotation, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f,
                              0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f, 1.f);
  EXPECT_SOAFLOAT3_EQ_EST(output[0].scale, 1.f, 1.f, -1.f, 1.f, 1.f, 1.f, -1.f,
                          1.f, 1.f, 1.f, -1.f, 1.f);
}

TEST(Cache, SamplingJob) {
  RawAnimation raw_animation;
  raw_animation.duration = 46.f;
  raw_animation.tracks.resize(1);  // Adds a joint.
  const RawAnimation::TranslationKey empty_key = {
      0.f, RawAnimation::TranslationKey::identity()};
  raw_animation.tracks[0].translations.push_back(empty_key);

  SamplingJob::Context context(1);
  ozz::unique_ptr<Animation> animations[2];

  {
    const RawAnimation::TranslationKey tkey = {
        .3f, ozz::math::Float3(1.f, -1.f, 5.f)};
    raw_animation.tracks[0].translations[0] = tkey;

    AnimationBuilder builder;
    animations[0] = builder(raw_animation);
    ASSERT_TRUE(animations[0]);
  }
  {
    const RawAnimation::TranslationKey tkey = {
        .3f, ozz::math::Float3(-1.f, 1.f, -5.f)};
    raw_animation.tracks[0].translations[0] = tkey;

    AnimationBuilder builder;
    animations[1] = builder(raw_animation);
    ASSERT_TRUE(animations[1]);
  }

  ozz::math::SoaTransform output[1];

  SamplingJob job;
  job.animation = animations[0].get();
  job.context = &context;
  job.ratio = 0.f;
  job.output = output;

  EXPECT_TRUE(job.Validate());
  EXPECT_TRUE(job.Run());
  EXPECT_SOAFLOAT3_EQ_EST(output[0].translation, 1.f, 0.f, 0.f, 0.f, -1.f, 0.f,
                          0.f, 0.f, 5.f, 0.f, 0.f, 0.f);
  EXPECT_SOAQUATERNION_EQ_EST(output[0].rotation, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f,
                              0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f, 1.f);
  EXPECT_SOAFLOAT3_EQ_EST(output[0].scale, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f,
                          1.f, 1.f, 1.f, 1.f, 1.f);

  // Re-uses context.
  EXPECT_TRUE(job.Validate());
  EXPECT_TRUE(job.Run());
  EXPECT_SOAFLOAT3_EQ_EST(output[0].translation, 1.f, 0.f, 0.f, 0.f, -1.f, 0.f,
                          0.f, 0.f, 5.f, 0.f, 0.f, 0.f);

  // Invalidates context.
  context.Invalidate();

  EXPECT_TRUE(job.Validate());
  EXPECT_TRUE(job.Run());
  EXPECT_SOAFLOAT3_EQ_EST(output[0].translation, 1.f, 0.f, 0.f, 0.f, -1.f, 0.f,
                          0.f, 0.f, 5.f, 0.f, 0.f, 0.f);

  // Changes animation.
  job.animation = animations[1].get();
  EXPECT_TRUE(job.Validate());
  EXPECT_TRUE(job.Run());
  EXPECT_SOAFLOAT3_EQ_EST(output[0].translation, -1.f, 0.f, 0.f, 0.f, 1.f, 0.f,
                          0.f, 0.f, -5.f, 0.f, 0.f, 0.f);
  EXPECT_SOAQUATERNION_EQ_EST(output[0].rotation, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f,
                              0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f, 1.f);
  EXPECT_SOAFLOAT3_EQ_EST(output[0].scale, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f,
                          1.f, 1.f, 1.f, 1.f, 1.f);

  // Invalidates and changes animation.
  job.animation = animations[1].get();
  EXPECT_TRUE(job.Validate());
  EXPECT_TRUE(job.Run());
  EXPECT_SOAFLOAT3_EQ_EST(output[0].translation, -1.f, 0.f, 0.f, 0.f, 1.f, 0.f,
                          0.f, 0.f, -5.f, 0.f, 0.f, 0.f);
}

TEST(CacheResize, SamplingJob) {
  RawAnimation raw_animation;
  raw_animation.duration = 46.f;
  raw_animation.tracks.resize(7);

  AnimationBuilder builder;
  ozz::unique_ptr<Animation> animation(builder(raw_animation));
  ASSERT_TRUE(animation);

  // Empty context by default
  SamplingJob::Context context;

  ozz::math::SoaTransform output[7];

  SamplingJob job;
  job.animation = animation.get();
  job.context = &context;
  job.ratio = 0.f;
  job.output = output;

  // Cache is too small
  EXPECT_FALSE(job.Validate());

  // Cache is ok.
  context.Resize(7);
  EXPECT_TRUE(job.Validate());
  EXPECT_TRUE(job.Run());

  // Cache is too small
  context.Resize(1);
  EXPECT_FALSE(job.Validate());
}

namespace {
// Builds a 1s one-track animation with different first (0) and last (10)
// translation keyframes, so a loop wrap would expose a jump if the boundary
// frame sampled the end of the clip instead of its beginning.
ozz::unique_ptr<Animation> BuildLoopBoundaryAnimation(float _last_x) {
  RawAnimation raw_animation;
  raw_animation.duration = 1.f;
  raw_animation.tracks.resize(1);
  raw_animation.tracks[0].translations.push_back(
      {0.f, ozz::math::Float3(0.f, 0.f, 0.f)});
  raw_animation.tracks[0].translations.push_back(
      {0.5f, ozz::math::Float3(5.f, 0.f, 0.f)});
  raw_animation.tracks[0].translations.push_back(
      {1.f, ozz::math::Float3(_last_x, 0.f, 0.f)});

  AnimationBuilder builder;
  return builder(raw_animation);
}
}  // namespace

TEST(LoopBoundaryDefaultsToClamping, SamplingJob) {
  ozz::unique_ptr<Animation> animation = BuildLoopBoundaryAnimation(10.f);
  ASSERT_TRUE(animation);

  SamplingJob::Context context(4);
  ozz::math::SoaTransform output[1];

  SamplingJob job;
  job.animation = animation.get();
  job.context = &context;
  job.output = output;

  // loop defaults to false: the historical clamping behavior is preserved.
  job.ratio = 1.f;
  EXPECT_TRUE(job.Run());
  EXPECT_SOAFLOAT3_EQ_EST(output[0].translation, 10.f, 0.f, 0.f, 0.f, 0.f,
                          0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);

  // Out of bounds inputs are clamped, including when loop is enabled.
  job.loop = true;
  job.ratio = 1.5f;
  EXPECT_TRUE(job.Run());
  EXPECT_SOAFLOAT3_EQ_EST(output[0].translation, 0.f, 0.f, 0.f, 0.f, 0.f,
                          0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
  job.ratio = -.5f;
  EXPECT_TRUE(job.Run());
  EXPECT_SOAFLOAT3_EQ_EST(output[0].translation, 0.f, 0.f, 0.f, 0.f, 0.f,
                          0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
}

TEST(LoopBoundaryContinuity, SamplingJob) {
  ozz::unique_ptr<Animation> animation = BuildLoopBoundaryAnimation(10.f);
  ASSERT_TRUE(animation);

  SamplingJob::Context context(4);
  ozz::math::SoaTransform output[1];

  SamplingJob job;
  job.animation = animation.get();
  job.context = &context;
  job.loop = true;
  job.output = output;

  // Plays forward through the loop boundary.
  job.ratio = .99f;
  ASSERT_TRUE(job.Run());
  EXPECT_SOAFLOAT3_EQ_EST(output[0].translation, 9.9f, 0.f, 0.f, 0.f, 0.f,
                          0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
  const ozz::math::SoaTransform before_boundary = output[0];

  // The frame that reaches the end of the clip samples the beginning of the
  // next loop, not the last keyframe posture.
  job.ratio = 1.f;
  ASSERT_TRUE(job.Run());
  EXPECT_SOAFLOAT3_EQ_EST(output[0].translation, 0.f, 0.f, 0.f, 0.f, 0.f,
                          0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
  const ozz::math::SoaTransform boundary = output[0];

  // The next frame starts the next loop and continues from the same posture.
  job.ratio = .01f;
  ASSERT_TRUE(job.Run());
  EXPECT_SOAFLOAT3_EQ_EST(output[0].translation, .1f, 0.f, 0.f, 0.f, 0.f,
                          0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);

  // Boundary frame is exactly the posture sampled at ratio 0.
  job.ratio = 0.f;
  ASSERT_TRUE(job.Run());
  EXPECT_EQ(memcmp(&boundary, &output[0], sizeof(boundary)), 0);

  // Standard interpolation semantics still apply inside the clip.
  EXPECT_NE(memcmp(&before_boundary, &boundary, sizeof(boundary)), 0);
}

TEST(LoopBoundaryReproducibility, SamplingJob) {
  ozz::unique_ptr<Animation> animation = BuildLoopBoundaryAnimation(10.f);
  ASSERT_TRUE(animation);

  ozz::math::SoaTransform reference[1];
  {
    // A fresh context must provide the same boundary result as a primed one.
    SamplingJob::Context fresh_context(4);
    SamplingJob job;
    job.animation = animation.get();
    job.context = &fresh_context;
    job.loop = true;
    job.ratio = 1.f;
    job.output = reference;
    ASSERT_TRUE(job.Run());
    EXPECT_SOAFLOAT3_EQ_EST(reference[0].translation, 0.f, 0.f, 0.f, 0.f, 0.f,
                            0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
  }

  // Primes another context by playing forward to the end of the clip.
  SamplingJob::Context primed_context(4);
  ozz::math::SoaTransform primed[1];
  SamplingJob job;
  job.animation = animation.get();
  job.context = &primed_context;
  job.loop = true;
  job.output = primed;
  for (float ratio = 0.f; ratio <= 1.f; ratio += .07f) {
    job.ratio = ratio;
    ASSERT_TRUE(job.Run());
  }
  job.ratio = 1.f;
  ASSERT_TRUE(job.Run());
  EXPECT_EQ(memcmp(reference, primed, sizeof(reference)), 0);

  // Sampling the same (loop, ratio) pair again stays identical.
  ozz::math::SoaTransform again[1];
  job.output = again;
  ASSERT_TRUE(job.Run());
  EXPECT_EQ(memcmp(reference, again, sizeof(reference)), 0);
}

TEST(LoopBoundaryContextIsolation, SamplingJob) {
  ozz::unique_ptr<Animation> first = BuildLoopBoundaryAnimation(10.f);
  ozz::unique_ptr<Animation> second = BuildLoopBoundaryAnimation(20.f);
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  EXPECT_NE(first->id(), second->id());

  SamplingJob::Context context(4);
  ozz::math::SoaTransform output[1];

  SamplingJob job;
  job.context = &context;
  job.loop = true;
  job.output = output;

  job.animation = first.get();
  job.ratio = 1.f;
  ASSERT_TRUE(job.Run());
  EXPECT_SOAFLOAT3_EQ_EST(output[0].translation, 0.f, 0.f, 0.f, 0.f, 0.f,
                          0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
  const ozz::math::SoaTransform first_boundary = output[0];

  // Switches animation: the context must not reuse keyframes cached for the
  // previous clip.
  job.animation = second.get();
  job.ratio = .5f;
  ASSERT_TRUE(job.Run());
  EXPECT_SOAFLOAT3_EQ_EST(output[0].translation, 5.f, 0.f, 0.f, 0.f, 0.f,
                          0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);

  // Back to the first clip, same ratio gives the same reproducible result.
  job.animation = first.get();
  job.ratio = 1.f;
  ASSERT_TRUE(job.Run());
  EXPECT_EQ(memcmp(&first_boundary, &output[0], sizeof(first_boundary)), 0);

  // Toggles the sampling convention: caches cannot be shared across them.
  job.loop = false;
  ASSERT_TRUE(job.Run());
  EXPECT_SOAFLOAT3_EQ_EST(output[0].translation, 10.f, 0.f, 0.f, 0.f, 0.f,
                          0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
  job.loop = true;
  ASSERT_TRUE(job.Run());
  EXPECT_EQ(memcmp(&first_boundary, &output[0], sizeof(first_boundary)), 0);
}

TEST(LoopBoundaryAddressRecycling, SamplingJob) {
  SamplingJob::Context context(4);
  ozz::math::SoaTransform output[1];

  // Builds and destroys an animation, then builds new ones until the
  // allocator recycles its address. The unique id must still invalidate the
  // context, so keyframes from the destroyed animation can never be read.
  ozz::unique_ptr<Animation> initial = BuildLoopBoundaryAnimation(10.f);
  ASSERT_TRUE(initial);
  const Animation* const recycled_address = initial.get();
  const uint64_t initial_id = initial->id();

  SamplingJob job;
  job.context = &context;
  job.loop = true;
  job.ratio = 1.f;
  job.output = output;
  job.animation = initial.get();
  ASSERT_TRUE(job.Run());

  initial.reset();

  // Same sized allocations performed back to back typically reuse the just
  // released block. A few attempts absorb allocator bookkeeping variations.
  ozz::unique_ptr<Animation> replacement;
  for (int attempt = 0; attempt < 16 && !replacement; ++attempt) {
    ozz::unique_ptr<Animation> candidate = BuildLoopBoundaryAnimation(30.f);
    if (candidate.get() == recycled_address) {
      replacement = std::move(candidate);
    }
  }
  if (!replacement) {
    // The platform allocator chose not to recycle the address: the unique id
    // guarantee cannot be exercised through address equality here, so the
    // check is a no-op rather than a failure.
    return;
  }
  ASSERT_EQ(replacement.get(), recycled_address);
  EXPECT_NE(replacement->id(), initial_id);

  // The address is the same but the animation differs: sampling must rebuild
  // the cache from the new animation instead of reading stale keyframes.
  job.animation = replacement.get();
  job.ratio = .5f;
  ASSERT_TRUE(job.Run());
  EXPECT_SOAFLOAT3_EQ_EST(output[0].translation, 5.f, 0.f, 0.f, 0.f, 0.f,
                          0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
}
