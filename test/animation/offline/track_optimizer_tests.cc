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
#include "ozz/animation/offline/raw_track.h"
#include "ozz/animation/offline/track_builder.h"
#include "ozz/animation/offline/track_optimizer.h"
#include "ozz/animation/runtime/track.h"
#include "ozz/animation/runtime/track_sampling_job.h"
#include "ozz/base/io/archive.h"
#include "ozz/base/io/stream.h"
#include "ozz/base/maths/gtest_math_helper.h"
#include "ozz/base/memory/unique_ptr.h"

using ozz::animation::offline::RawFloat2Track;
using ozz::animation::offline::RawFloat3Track;
using ozz::animation::offline::RawFloat4Track;
using ozz::animation::offline::RawFloatTrack;
using ozz::animation::offline::RawQuaternionTrack;
using ozz::animation::offline::RawTrackInterpolation;
using ozz::animation::offline::TrackOptimizer;

namespace {

template <typename _RawTrack>
struct OptimizedTrackTraits;

#define OZZ_DEFINE_TRACK_TRAITS(_raw, _runtime, _job, _identity)          \
  template <>                                                             \
  struct OptimizedTrackTraits<ozz::animation::offline::_raw> {           \
    typedef ozz::animation::_runtime RuntimeTrack;                       \
    typedef ozz::animation::_job SamplingJobType;                        \
    static ozz::animation::offline::_raw::ValueType Identity() {         \
      return _identity;                                                   \
    }                                                                     \
  }

OZZ_DEFINE_TRACK_TRAITS(RawFloatTrack, FloatTrack, FloatTrackSamplingJob,
                        0.f);
OZZ_DEFINE_TRACK_TRAITS(RawFloat2Track, Float2Track, Float2TrackSamplingJob,
                        ozz::math::Float2(0.f, 0.f));
OZZ_DEFINE_TRACK_TRAITS(RawFloat3Track, Float3Track, Float3TrackSamplingJob,
                        ozz::math::Float3(0.f, 0.f, 0.f));
OZZ_DEFINE_TRACK_TRAITS(RawFloat4Track, Float4Track, Float4TrackSamplingJob,
                        ozz::math::Float4(0.f, 0.f, 0.f, 0.f));
OZZ_DEFINE_TRACK_TRAITS(
    RawQuaternionTrack, QuaternionTrack, QuaternionTrackSamplingJob,
    ozz::math::Quaternion(0.f, 0.f, 0.f, 1.f));

#undef OZZ_DEFINE_TRACK_TRAITS

void ExpectTrackValueEqual(float _expected, float _actual) {
  EXPECT_FLOAT_EQ(_expected, _actual);
}

void ExpectTrackValueEqual(const ozz::math::Float2& _expected,
                           const ozz::math::Float2& _actual) {
  EXPECT_FLOAT2_EQ(_actual, _expected.x, _expected.y);
}

void ExpectTrackValueEqual(const ozz::math::Float3& _expected,
                           const ozz::math::Float3& _actual) {
  EXPECT_FLOAT3_EQ(_actual, _expected.x, _expected.y, _expected.z);
}

void ExpectTrackValueEqual(const ozz::math::Float4& _expected,
                           const ozz::math::Float4& _actual) {
  EXPECT_FLOAT4_EQ(_actual, _expected.x, _expected.y, _expected.z,
                   _expected.w);
}

void ExpectTrackValueEqual(const ozz::math::Quaternion& _expected,
                           const ozz::math::Quaternion& _actual) {
  // Quaternions q and -q represent the same rotation. Built tracks normally
  // share a hemisphere anyway; this keeps the check focused on rotation.
  const float dot = _expected.x * _actual.x + _expected.y * _actual.y +
                    _expected.z * _actual.z + _expected.w * _actual.w;
  if (dot < 0.f) {
    EXPECT_QUATERNION_EQ(_actual, -_expected.x, -_expected.y, -_expected.z,
                         -_expected.w);
  } else {
    EXPECT_QUATERNION_EQ(_actual, _expected.x, _expected.y, _expected.z,
                         _expected.w);
  }
}

template <typename _RuntimeTrack, typename _SamplingJob, typename _ValueType>
_ValueType SampleTrack(const _RuntimeTrack& _track, float _ratio) {
  _SamplingJob job;
  _ValueType result = _ValueType();
  job.track = &_track;
  job.ratio = _ratio;
  job.result = &result;
  EXPECT_TRUE(job.Run());
  return result;
}

template <typename _RuntimeTrack>
ozz::unique_ptr<_RuntimeTrack> RoundTripTrack(const _RuntimeTrack& _track) {
  ozz::io::MemoryStream stream;
  {
    ozz::io::OArchive archive(&stream);
    archive << _track;
  }

  stream.Seek(0, ozz::io::Stream::kSet);
  ozz::io::IArchive archive(&stream);
  ozz::unique_ptr<_RuntimeTrack> result(ozz::make_unique<_RuntimeTrack>());
  archive >> *result;
  return result;
}

const float kOptimizerSampleRatios[] = {
    -1e-6f,        0.f,          1e-6f,        0.124999f, 0.125f,
    0.125001f,     0.249999f,    0.25f,        0.250001f, 0.375f,
    0.499999f,     0.5f,        0.5000005f,    0.500001f, 0.5000015f,
    0.500002f,     0.625f,      0.749999f,     0.75f,     0.750001f,
    0.999999f,     1.f,        1.000001f};

template <typename _RawTrack>
void ExpectOptimizedSamplesMatch(const _RawTrack& _input,
                                 const _RawTrack& _optimized) {
  typedef OptimizedTrackTraits<_RawTrack> Traits;
  typedef typename Traits::RuntimeTrack RuntimeTrack;
  typedef typename _RawTrack::ValueType ValueType;

  ozz::animation::offline::TrackBuilder builder;
  ozz::unique_ptr<RuntimeTrack> original(builder(_input));
  ozz::unique_ptr<RuntimeTrack> optimized(builder(_optimized));
  ASSERT_TRUE(original);
  ASSERT_TRUE(optimized);

  for (size_t i = 0; i < OZZ_ARRAY_SIZE(kOptimizerSampleRatios); ++i) {
    const float ratio = kOptimizerSampleRatios[i];
    SCOPED_TRACE(ratio);
    const ValueType original_value =
        SampleTrack<RuntimeTrack, typename Traits::SamplingJobType, ValueType>(
            *original, ratio);
    const ValueType optimized_value =
        SampleTrack<RuntimeTrack, typename Traits::SamplingJobType, ValueType>(
            *optimized, ratio);
    ExpectTrackValueEqual(original_value, optimized_value);
  }

  ozz::unique_ptr<RuntimeTrack> archived = RoundTripTrack(*optimized);
  ASSERT_TRUE(archived);
  EXPECT_EQ(archived->size(), optimized->size());

  for (size_t i = 0; i < OZZ_ARRAY_SIZE(kOptimizerSampleRatios); ++i) {
    const float ratio = kOptimizerSampleRatios[i];
    SCOPED_TRACE(ratio);
    const ValueType optimized_value =
        SampleTrack<RuntimeTrack, typename Traits::SamplingJobType, ValueType>(
            *optimized, ratio);
    const ValueType archived_value =
        SampleTrack<RuntimeTrack, typename Traits::SamplingJobType, ValueType>(
            *archived, ratio);
    ExpectTrackValueEqual(optimized_value, archived_value);
  }
}

template <typename _RawTrack>
void CheckOptimizationPreservesSampling(const _RawTrack& _input,
                                        float _tolerance) {
  TrackOptimizer optimizer;
  optimizer.tolerance = _tolerance;

  _RawTrack optimized;
  ASSERT_TRUE(optimizer(_input, &optimized));
  ExpectOptimizedSamplesMatch(_input, optimized);
}

template <typename _RawTrack>
void CheckEmptyTrackKeepsDefaultValue() {
  typedef OptimizedTrackTraits<_RawTrack> Traits;
  typedef typename Traits::RuntimeTrack RuntimeTrack;
  typedef typename _RawTrack::ValueType ValueType;

  _RawTrack input;
  ASSERT_TRUE(input.Validate());

  TrackOptimizer optimizer;
  optimizer.tolerance = 1e-9f;
  _RawTrack optimized;
  ASSERT_TRUE(optimizer(input, &optimized));
  EXPECT_TRUE(optimized.keyframes.empty());

  ozz::animation::offline::TrackBuilder builder;
  ozz::unique_ptr<RuntimeTrack> track(builder(optimized));
  ASSERT_TRUE(track);

  const ValueType expected = Traits::Identity();
  for (size_t i = 0; i < OZZ_ARRAY_SIZE(kOptimizerSampleRatios); ++i) {
    const float ratio = kOptimizerSampleRatios[i];
    SCOPED_TRACE(ratio);
    const ValueType value =
        SampleTrack<RuntimeTrack, typename Traits::SamplingJobType, ValueType>(
            *track, ratio);
    ExpectTrackValueEqual(expected, value);
  }

  ozz::unique_ptr<RuntimeTrack> archived = RoundTripTrack(*track);
  ASSERT_TRUE(archived);
  EXPECT_EQ(archived->size(), track->size());
  const ValueType archived_value =
      SampleTrack<RuntimeTrack, typename Traits::SamplingJobType, ValueType>(
          *archived, .5f);
  ExpectTrackValueEqual(expected, archived_value);
}

template <typename _RawTrack>
void CheckInvalidTrackFailureContract(const _RawTrack& _input) {
  ASSERT_FALSE(_input.Validate());

  _RawTrack output;
  typedef typename _RawTrack::Keyframe Keyframe;
  Keyframe retained = {
      RawTrackInterpolation::kLinear, 0.f,
      OptimizedTrackTraits<_RawTrack>::Identity()};
  output.keyframes.push_back(retained);

  TrackOptimizer optimizer;
  EXPECT_FALSE(optimizer(_input, &output));
  EXPECT_TRUE(output.keyframes.empty());

  ozz::animation::offline::TrackBuilder builder;
  EXPECT_TRUE(builder(_input) == nullptr);
}

}  // namespace

TEST(Error, TrackOptimizer) {
  TrackOptimizer optimizer;

  {  // nullptr output.
    RawFloatTrack input;
    EXPECT_TRUE(input.Validate());

    // Builds animation
    EXPECT_FALSE(optimizer(input, nullptr));
  }

  {  // same input and output.
    RawFloatTrack input;
    EXPECT_TRUE(input.Validate());

    // Builds animation
    EXPECT_FALSE(optimizer(input, &input));
  }

  {  // Invalid input animation.
    RawFloatTrack input;
    input.keyframes.resize(1);
    input.keyframes[0].ratio = 99.f;
    EXPECT_FALSE(input.Validate());

    // Builds animation
    RawFloatTrack output;
    output.keyframes.resize(1);
    EXPECT_FALSE(optimizer(input, &output));
    EXPECT_EQ(output.keyframes.size(), 0u);
  }
}

TEST(Name, FloatTrackOptimizer) {
  // Step keys aren't optimized.
  TrackOptimizer optimizer;

  RawFloatTrack raw_float_track;
  raw_float_track.name = "FloatTrackOptimizer test";

  RawFloatTrack output;
  ASSERT_TRUE(optimizer(raw_float_track, &output));

  EXPECT_STREQ(raw_float_track.name.c_str(), output.name.c_str());
}

TEST(Identity, TrackOptimizer) {
  TrackOptimizer optimizer;

  RawFloatTrack raw_float_track;
  const RawFloatTrack::Keyframe key0 = {RawTrackInterpolation::kLinear, .5f,
                                        0.f};
  raw_float_track.keyframes.push_back(key0);
  const RawFloatTrack::Keyframe key1 = {RawTrackInterpolation::kLinear, .7f,
                                        0.f};
  raw_float_track.keyframes.push_back(key1);
  const RawFloatTrack::Keyframe key2 = {RawTrackInterpolation::kLinear, .8f,
                                        0.f};
  raw_float_track.keyframes.push_back(key2);

  RawFloatTrack output;
  ASSERT_TRUE(optimizer(raw_float_track, &output));
  EXPECT_TRUE(output.keyframes.empty());

  // Step keys aren't optimized.
  raw_float_track.keyframes[1].interpolation = RawTrackInterpolation::kStep;
  ASSERT_TRUE(optimizer(raw_float_track, &output));
  EXPECT_EQ(output.keyframes.size(), 2u);
}

TEST(Constant, TrackOptimizer) {
  TrackOptimizer optimizer;

  RawFloatTrack raw_float_track;
  const RawFloatTrack::Keyframe key0 = {RawTrackInterpolation::kLinear, .5f,
                                        46.f};
  raw_float_track.keyframes.push_back(key0);
  const RawFloatTrack::Keyframe key1 = {RawTrackInterpolation::kLinear, .7f,
                                        46.f};
  raw_float_track.keyframes.push_back(key1);
  const RawFloatTrack::Keyframe key2 = {RawTrackInterpolation::kLinear, .8f,
                                        46.f};
  raw_float_track.keyframes.push_back(key2);

  RawFloatTrack output;
  ASSERT_TRUE(optimizer(raw_float_track, &output));
  EXPECT_EQ(output.keyframes.size(), 1u);

  EXPECT_EQ(output.keyframes[0].interpolation, key0.interpolation);
  EXPECT_FLOAT_EQ(output.keyframes[0].ratio, key0.ratio);
  EXPECT_FLOAT_EQ(output.keyframes[0].value, key0.value);

  // Step keys aren't optimized.
  raw_float_track.keyframes[2].interpolation = RawTrackInterpolation::kStep;
  ASSERT_TRUE(optimizer(raw_float_track, &output));
  EXPECT_EQ(output.keyframes.size(), 2u);

  EXPECT_EQ(output.keyframes[0].interpolation, key0.interpolation);
  EXPECT_FLOAT_EQ(output.keyframes[0].ratio, key0.ratio);
  EXPECT_FLOAT_EQ(output.keyframes[0].value, key0.value);

  EXPECT_EQ(output.keyframes[1].interpolation, RawTrackInterpolation::kStep);
  EXPECT_FLOAT_EQ(output.keyframes[1].ratio, key2.ratio);
  EXPECT_FLOAT_EQ(output.keyframes[1].value, key2.value);
}

TEST(OptimizeSteps, TrackOptimizer) {
  // Step keys aren't optimized.
  TrackOptimizer optimizer;

  RawFloatTrack raw_float_track;
  const RawFloatTrack::Keyframe key0 = {RawTrackInterpolation::kStep, .5f,
                                        46.f};
  raw_float_track.keyframes.push_back(key0);
  const RawFloatTrack::Keyframe key1 = {RawTrackInterpolation::kStep, .7f, 0.f};
  raw_float_track.keyframes.push_back(key1);
  const RawFloatTrack::Keyframe key2 = {RawTrackInterpolation::kStep, .8f,
                                        1e-9f};
  raw_float_track.keyframes.push_back(key2);

  RawFloatTrack output;
  ASSERT_TRUE(optimizer(raw_float_track, &output));

  EXPECT_EQ(output.keyframes.size(), 3u);

  EXPECT_EQ(output.keyframes[0].interpolation, key0.interpolation);
  EXPECT_FLOAT_EQ(output.keyframes[0].ratio, key0.ratio);
  EXPECT_FLOAT_EQ(output.keyframes[0].value, key0.value);

  EXPECT_EQ(output.keyframes[1].interpolation, key1.interpolation);
  EXPECT_FLOAT_EQ(output.keyframes[1].ratio, key1.ratio);
  EXPECT_FLOAT_EQ(output.keyframes[1].value, key1.value);

  EXPECT_EQ(output.keyframes[2].interpolation, key2.interpolation);
  EXPECT_FLOAT_EQ(output.keyframes[2].ratio, key2.ratio);
  EXPECT_FLOAT_EQ(output.keyframes[2].value, key2.value);
}

TEST(OptimizeInterpolate, TrackOptimizer) {
  // Step keys aren't optimized.
  TrackOptimizer optimizer;

  RawFloatTrack raw_float_track;
  const RawFloatTrack::Keyframe key0 = {RawTrackInterpolation::kLinear, 0.f,
                                        69.f};
  raw_float_track.keyframes.push_back(key0);
  const RawFloatTrack::Keyframe key1 = {RawTrackInterpolation::kLinear, .25f,
                                        46.f};
  raw_float_track.keyframes.push_back(key1);
  const RawFloatTrack::Keyframe key2 = {RawTrackInterpolation::kLinear, .5f,
                                        23.f};
  raw_float_track.keyframes.push_back(key2);
  const RawFloatTrack::Keyframe key3 = {RawTrackInterpolation::kLinear,
                                        .500001f, 23.000001f};
  raw_float_track.keyframes.push_back(key3);
  const RawFloatTrack::Keyframe key4 = {RawTrackInterpolation::kLinear, .75f,
                                        0.f};
  raw_float_track.keyframes.push_back(key4);
  const RawFloatTrack::Keyframe key5 = {RawTrackInterpolation::kLinear, .8f,
                                        1e-12f};
  raw_float_track.keyframes.push_back(key5);
  const RawFloatTrack::Keyframe key6 = {RawTrackInterpolation::kLinear, 1.f,
                                        -1e-12f};
  raw_float_track.keyframes.push_back(key6);

  {
    RawFloatTrack output;

    optimizer.tolerance = 1e-3f;
    ASSERT_TRUE(optimizer(raw_float_track, &output));

    EXPECT_EQ(output.keyframes.size(), 2u);

    EXPECT_EQ(output.keyframes[0].interpolation, key0.interpolation);
    EXPECT_FLOAT_EQ(output.keyframes[0].ratio, key0.ratio);
    EXPECT_FLOAT_EQ(output.keyframes[0].value, key0.value);

    EXPECT_EQ(output.keyframes[1].interpolation, key4.interpolation);
    EXPECT_FLOAT_EQ(output.keyframes[1].ratio, key4.ratio);
    EXPECT_FLOAT_EQ(output.keyframes[1].value, key4.value);
  }

  {
    RawFloatTrack output;
    optimizer.tolerance = 1e-9f;
    ASSERT_TRUE(optimizer(raw_float_track, &output));

    EXPECT_EQ(output.keyframes.size(), 4u);

    EXPECT_EQ(output.keyframes[0].interpolation, key0.interpolation);
    EXPECT_FLOAT_EQ(output.keyframes[0].ratio, key0.ratio);
    EXPECT_FLOAT_EQ(output.keyframes[0].value, key0.value);

    EXPECT_EQ(output.keyframes[1].interpolation, key2.interpolation);
    EXPECT_FLOAT_EQ(output.keyframes[1].ratio, key2.ratio);
    EXPECT_FLOAT_EQ(output.keyframes[1].value, key2.value);

    EXPECT_EQ(output.keyframes[2].interpolation, key3.interpolation);
    EXPECT_FLOAT_EQ(output.keyframes[2].ratio, key3.ratio);
    EXPECT_FLOAT_EQ(output.keyframes[2].value, key3.value);

    EXPECT_EQ(output.keyframes[3].interpolation, key4.interpolation);
    EXPECT_FLOAT_EQ(output.keyframes[3].ratio, key4.ratio);
    EXPECT_FLOAT_EQ(output.keyframes[3].value, key4.value);
  }
}

TEST(float, TrackOptimizer) {
  TrackOptimizer optimizer;

  RawFloatTrack raw_track;
  const RawFloatTrack::Keyframe key0 = {RawTrackInterpolation::kLinear, 0.f,
                                        6.9f};
  raw_track.keyframes.push_back(key0);
  const RawFloatTrack::Keyframe key1 = {RawTrackInterpolation::kLinear, .25f,
                                        4.6f};
  raw_track.keyframes.push_back(key1);
  const RawFloatTrack::Keyframe key2 = {RawTrackInterpolation::kLinear, .5f,
                                        2.3f};
  raw_track.keyframes.push_back(key2);
  const RawFloatTrack::Keyframe key3 = {RawTrackInterpolation::kLinear,
                                        .500001f, 2.300001f};
  raw_track.keyframes.push_back(key3);
  const RawFloatTrack::Keyframe key4 = {RawTrackInterpolation::kLinear, .75f,
                                        0.f};
  raw_track.keyframes.push_back(key4);
  const RawFloatTrack::Keyframe key5 = {RawTrackInterpolation::kLinear, .8f,
                                        1e-12f};
  raw_track.keyframes.push_back(key5);
  const RawFloatTrack::Keyframe key6 = {RawTrackInterpolation::kLinear, 1.f,
                                        -1e-12f};
  raw_track.keyframes.push_back(key6);

  RawFloatTrack output;
  ASSERT_TRUE(optimizer(raw_track, &output));

  EXPECT_EQ(output.keyframes.size(), 2u);

  EXPECT_EQ(output.keyframes[0].interpolation, key0.interpolation);
  EXPECT_FLOAT_EQ(output.keyframes[0].ratio, key0.ratio);
  EXPECT_FLOAT_EQ(output.keyframes[0].value, key0.value);

  EXPECT_EQ(output.keyframes[1].interpolation, key4.interpolation);
  EXPECT_FLOAT_EQ(output.keyframes[1].ratio, key4.ratio);
  EXPECT_FLOAT_EQ(output.keyframes[1].value, key4.value);
}

TEST(Float2, TrackOptimizer) {
  TrackOptimizer optimizer;

  RawFloat2Track raw_track;
  const RawFloat2Track::Keyframe key0 = {RawTrackInterpolation::kLinear, 0.f,
                                         ozz::math::Float2(6.9f, 0.f)};
  raw_track.keyframes.push_back(key0);
  const RawFloat2Track::Keyframe key1 = {RawTrackInterpolation::kLinear, .25f,
                                         ozz::math::Float2(4.6f, 0.f)};
  raw_track.keyframes.push_back(key1);
  const RawFloat2Track::Keyframe key2 = {RawTrackInterpolation::kLinear, .5f,
                                         ozz::math::Float2(2.3f, 0.f)};
  raw_track.keyframes.push_back(key2);
  const RawFloat2Track::Keyframe key3 = {RawTrackInterpolation::kLinear,
                                         .500001f,
                                         ozz::math::Float2(2.3000001f, 0.f)};
  raw_track.keyframes.push_back(key3);
  const RawFloat2Track::Keyframe key4 = {RawTrackInterpolation::kLinear, .75f,
                                         ozz::math::Float2(0.f, 0.f)};
  raw_track.keyframes.push_back(key4);
  const RawFloat2Track::Keyframe key5 = {RawTrackInterpolation::kLinear, .8f,
                                         ozz::math::Float2(0.f, 1e-12f)};
  raw_track.keyframes.push_back(key5);
  const RawFloat2Track::Keyframe key6 = {RawTrackInterpolation::kLinear, 1.f,
                                         ozz::math::Float2(-1e-12f, 0.f)};
  raw_track.keyframes.push_back(key6);

  RawFloat2Track output;
  ASSERT_TRUE(optimizer(raw_track, &output));

  EXPECT_EQ(output.keyframes.size(), 2u);

  EXPECT_EQ(output.keyframes[0].interpolation, key0.interpolation);
  EXPECT_FLOAT_EQ(output.keyframes[0].ratio, key0.ratio);
  EXPECT_FLOAT2_EQ(output.keyframes[0].value, key0.value.x, key0.value.y);

  EXPECT_EQ(output.keyframes[1].interpolation, key4.interpolation);
  EXPECT_FLOAT_EQ(output.keyframes[1].ratio, key4.ratio);
  EXPECT_FLOAT2_EQ(output.keyframes[1].value, key4.value.x, key4.value.y);
}

TEST(Float3, TrackOptimizer) {
  TrackOptimizer optimizer;

  RawFloat3Track raw_track;
  const RawFloat3Track::Keyframe key0 = {RawTrackInterpolation::kLinear, 0.f,
                                         ozz::math::Float3(6.9f, 0.f, 0.f)};
  raw_track.keyframes.push_back(key0);
  const RawFloat3Track::Keyframe key1 = {RawTrackInterpolation::kLinear, .25f,
                                         ozz::math::Float3(4.6f, 0.f, 0.f)};
  raw_track.keyframes.push_back(key1);
  const RawFloat3Track::Keyframe key2 = {RawTrackInterpolation::kLinear, .5f,
                                         ozz::math::Float3(2.3f, 0.f, 0.f)};
  raw_track.keyframes.push_back(key2);
  const RawFloat3Track::Keyframe key3 = {
      RawTrackInterpolation::kLinear, .500001f,
      ozz::math::Float3(2.3000001f, 0.f, 0.f)};
  raw_track.keyframes.push_back(key3);
  const RawFloat3Track::Keyframe key4 = {RawTrackInterpolation::kLinear, .75f,
                                         ozz::math::Float3(0.f, 0.f, 0.f)};
  raw_track.keyframes.push_back(key4);
  const RawFloat3Track::Keyframe key5 = {RawTrackInterpolation::kLinear, .8f,
                                         ozz::math::Float3(0.f, 0.f, 1e-12f)};
  raw_track.keyframes.push_back(key5);
  const RawFloat3Track::Keyframe key6 = {RawTrackInterpolation::kLinear, 1.f,
                                         ozz::math::Float3(0.f, -1e-12f, 0.f)};
  raw_track.keyframes.push_back(key6);

  RawFloat3Track output;
  ASSERT_TRUE(optimizer(raw_track, &output));

  EXPECT_EQ(output.keyframes.size(), 2u);

  EXPECT_EQ(output.keyframes[0].interpolation, key0.interpolation);
  EXPECT_FLOAT_EQ(output.keyframes[0].ratio, key0.ratio);
  EXPECT_FLOAT3_EQ(output.keyframes[0].value, key0.value.x, key0.value.y,
                   key0.value.z);

  EXPECT_EQ(output.keyframes[1].interpolation, key4.interpolation);
  EXPECT_FLOAT_EQ(output.keyframes[1].ratio, key4.ratio);
  EXPECT_FLOAT3_EQ(output.keyframes[1].value, key4.value.x, key4.value.y,
                   key4.value.z);
}

TEST(Float4, TrackOptimizer) {
  TrackOptimizer optimizer;

  RawFloat4Track raw_track;
  const RawFloat4Track::Keyframe key0 = {
      RawTrackInterpolation::kLinear, 0.f,
      ozz::math::Float4(6.9f, 0.f, 0.f, 0.f)};
  raw_track.keyframes.push_back(key0);
  const RawFloat4Track::Keyframe key1 = {
      RawTrackInterpolation::kLinear, .25f,
      ozz::math::Float4(4.6f, 0.f, 0.f, 0.f)};
  raw_track.keyframes.push_back(key1);
  const RawFloat4Track::Keyframe key2 = {
      RawTrackInterpolation::kLinear, .5f,
      ozz::math::Float4(2.3f, 0.f, 0.f, 0.f)};
  raw_track.keyframes.push_back(key2);
  const RawFloat4Track::Keyframe key3 = {
      RawTrackInterpolation::kLinear, .500001f,
      ozz::math::Float4(2.3000001f, 0.f, 0.f, 0.f)};
  raw_track.keyframes.push_back(key3);
  const RawFloat4Track::Keyframe key4 = {RawTrackInterpolation::kLinear, .75f,
                                         ozz::math::Float4(0.f, 0.f, 0.f, 0.f)};
  raw_track.keyframes.push_back(key4);
  const RawFloat4Track::Keyframe key5 = {
      RawTrackInterpolation::kLinear, .8f,
      ozz::math::Float4(0.f, 0.f, 0.f, 1e-12f)};
  raw_track.keyframes.push_back(key5);
  const RawFloat4Track::Keyframe key6 = {
      RawTrackInterpolation::kLinear, 1.f,
      ozz::math::Float4(0.f, 0.f, 0.f, -1e-12f)};
  raw_track.keyframes.push_back(key6);

  RawFloat4Track output;
  ASSERT_TRUE(optimizer(raw_track, &output));

  EXPECT_EQ(output.keyframes.size(), 2u);

  EXPECT_EQ(output.keyframes[0].interpolation, key0.interpolation);
  EXPECT_FLOAT_EQ(output.keyframes[0].ratio, key0.ratio);
  EXPECT_FLOAT4_EQ(output.keyframes[0].value, key0.value.x, key0.value.y,
                   key0.value.z, key0.value.w);

  EXPECT_EQ(output.keyframes[1].interpolation, key4.interpolation);
  EXPECT_FLOAT_EQ(output.keyframes[1].ratio, key4.ratio);
  EXPECT_FLOAT4_EQ(output.keyframes[1].value, key4.value.x, key4.value.y,
                   key4.value.z, key0.value.w);
}

TEST(Quaternion, TrackOptimizer) {
  TrackOptimizer optimizer;

  RawQuaternionTrack raw_track;
  const RawQuaternionTrack::Keyframe key0 = {
      RawTrackInterpolation::kLinear, 0.f,
      ozz::math::Quaternion(.70710677f, 0.f, 0.f, .70710677f)};
  raw_track.keyframes.push_back(key0);
  const RawQuaternionTrack::Keyframe key1 = {
      RawTrackInterpolation::kLinear, .1f,
      ozz::math::Quaternion(.6172133f, .1543033f, 0.f, .7715167f)};  // NLerp
  raw_track.keyframes.push_back(key1);
  const RawQuaternionTrack::Keyframe key2 = {
      RawTrackInterpolation::kLinear, .5f,
      ozz::math::Quaternion(0.f, .70710677f, 0.f, .70710677f)};
  raw_track.keyframes.push_back(key2);
  const RawQuaternionTrack::Keyframe key3 = {
      RawTrackInterpolation::kLinear, .500001f,
      ozz::math::Quaternion(0.f, .70710676f, 0.f, .70710678f)};
  raw_track.keyframes.push_back(key3);
  const RawQuaternionTrack::Keyframe key4 = {
      RawTrackInterpolation::kLinear, .75f,
      ozz::math::Quaternion(0.f, .70710677f, 0.f, .70710677f)};
  raw_track.keyframes.push_back(key4);
  const RawQuaternionTrack::Keyframe key5 = {
      RawTrackInterpolation::kLinear, .8f,
      ozz::math::Quaternion(-0.f, -0.70710677f, -0.f, -.70710677f)};
  raw_track.keyframes.push_back(key5);
  const RawQuaternionTrack::Keyframe key6 = {
      RawTrackInterpolation::kLinear, 1.f,
      ozz::math::Quaternion(0.f, .70710677f, 0.f, .70710677f)};
  raw_track.keyframes.push_back(key6);

  RawQuaternionTrack output;
  ASSERT_TRUE(optimizer(raw_track, &output));

  EXPECT_EQ(output.keyframes.size(), 2u);

  EXPECT_EQ(output.keyframes[0].interpolation, key0.interpolation);
  EXPECT_FLOAT_EQ(output.keyframes[0].ratio, key0.ratio);
  EXPECT_QUATERNION_EQ(output.keyframes[0].value, key0.value.x, key0.value.y,
                       key0.value.z, key0.value.w);

  EXPECT_EQ(output.keyframes[1].interpolation, key2.interpolation);
  EXPECT_FLOAT_EQ(output.keyframes[1].ratio, key2.ratio);
  EXPECT_QUATERNION_EQ(output.keyframes[1].value, key2.value.x, key2.value.y,
                       key2.value.z, key2.value.w);
}

template <typename _ValueType>
_ValueType MakeTrackTestValue(float _value);

template <>
float MakeTrackTestValue<float>(float _value) {
  return _value;
}

template <>
ozz::math::Float2 MakeTrackTestValue<ozz::math::Float2>(float _value) {
  return ozz::math::Float2(_value, 100.f - _value);
}

template <>
ozz::math::Float3 MakeTrackTestValue<ozz::math::Float3>(float _value) {
  return ozz::math::Float3(_value, 100.f - _value, 2.f * _value);
}

template <>
ozz::math::Float4 MakeTrackTestValue<ozz::math::Float4>(float _value) {
  return ozz::math::Float4(_value, 100.f - _value, 2.f * _value,
                           20.f - _value);
}

template <>
ozz::math::Quaternion
MakeTrackTestValue<ozz::math::Quaternion>(float _value) {
  const float angle = _value * .01f * ozz::math::kPi;
  return ozz::math::Quaternion::FromAxisAngle(
      ozz::math::Float3(0.f, 0.f, 1.f), angle);
}

template <typename _RawTrack>
void AddTrackTestKey(_RawTrack* _track,
                     RawTrackInterpolation::Value _interpolation, float _ratio,
                     float _value) {
  typename _RawTrack::Keyframe key = {
      _interpolation, _ratio,
      MakeTrackTestValue<typename _RawTrack::ValueType>(_value)};
  _track->keyframes.push_back(key);
}

template <typename _RawTrack>
void CheckLinearOptimizationAcrossTrack() {
  const float ratios[] = {0.f,     .125f,      .25f, .5f,
                          .500001f, .75f,      1.f};
  const float values[] = {0.f, 2.5f, 5.f, 9.f, 11.f, 15.f, 20.f};

  _RawTrack input;
  for (size_t i = 0; i < OZZ_ARRAY_SIZE(ratios); ++i) {
    AddTrackTestKey(&input, RawTrackInterpolation::kLinear, ratios[i],
                    values[i]);
  }

  TrackOptimizer optimizer;
  _RawTrack optimized;
  ASSERT_TRUE(optimizer(input, &optimized));
  EXPECT_LT(optimized.keyframes.size(), input.keyframes.size());

  ExpectOptimizedSamplesMatch(input, optimized);
}

TEST(LinearSamplingEquivalence, TrackOptimizer) {
  CheckLinearOptimizationAcrossTrack<RawFloatTrack>();
  CheckLinearOptimizationAcrossTrack<RawFloat2Track>();
  CheckLinearOptimizationAcrossTrack<RawFloat3Track>();
  CheckLinearOptimizationAcrossTrack<RawFloat4Track>();
  CheckLinearOptimizationAcrossTrack<RawQuaternionTrack>();
}

template <typename _RawTrack>
void CheckStepOptimizationAcrossTrack() {
  const float ratios[] = {0.f, .25f, .5f, .75f, 1.f};
  const float values[] = {0.f, 20.f, 40.f, 60.f, 80.f};
  const RawTrackInterpolation::Value interpolations[] = {
      RawTrackInterpolation::kLinear, RawTrackInterpolation::kStep,
      RawTrackInterpolation::kLinear, RawTrackInterpolation::kLinear,
      RawTrackInterpolation::kLinear};

  _RawTrack input;
  for (size_t i = 0; i < OZZ_ARRAY_SIZE(ratios); ++i) {
    AddTrackTestKey(&input, interpolations[i], ratios[i], values[i]);
  }

  TrackOptimizer optimizer;
  _RawTrack optimized;
  ASSERT_TRUE(optimizer(input, &optimized));

  size_t step_key = 0;
  size_t next_linear_key = 0;
  for (size_t i = 0; i < optimized.keyframes.size(); ++i) {
    if (optimized.keyframes[i].ratio == .25f) {
      step_key = i;
    }
    if (optimized.keyframes[i].ratio == .5f) {
      next_linear_key = i;
    }
  }
  ASSERT_LT(step_key, optimized.keyframes.size());
  ASSERT_LT(next_linear_key, optimized.keyframes.size());
  EXPECT_EQ(optimized.keyframes[step_key].interpolation,
            RawTrackInterpolation::kStep);
  EXPECT_LT(step_key, next_linear_key);

  ExpectOptimizedSamplesMatch(input, optimized);
}

TEST(StepSamplingEquivalence, TrackOptimizer) {
  CheckStepOptimizationAcrossTrack<RawFloatTrack>();
  CheckStepOptimizationAcrossTrack<RawFloat2Track>();
  CheckStepOptimizationAcrossTrack<RawFloat3Track>();
  CheckStepOptimizationAcrossTrack<RawFloat4Track>();
  CheckStepOptimizationAcrossTrack<RawQuaternionTrack>();
}

TEST(QuaternionShortestPathSurvivesOptimization, TrackOptimizer) {
  const ozz::math::Quaternion qx(
      .70710677f, 0.f, 0.f, .70710677f);  // 90 degrees around x.
  const ozz::math::Quaternion qy(
      0.f, .70710677f, 0.f, .70710677f);  // 90 degrees around y.

  RawQuaternionTrack input;
  const RawQuaternionTrack::Keyframe key0 = {
      RawTrackInterpolation::kLinear, 0.f, qx};
  input.keyframes.push_back(key0);
  const RawQuaternionTrack::Keyframe key1 = {
      RawTrackInterpolation::kLinear, .5f, -qx};
  input.keyframes.push_back(key1);
  const RawQuaternionTrack::Keyframe key2 = {
      RawTrackInterpolation::kLinear, .500001f, qx};
  input.keyframes.push_back(key2);
  const RawQuaternionTrack::Keyframe key3 = {
      RawTrackInterpolation::kLinear, 1.f, qy};
  input.keyframes.push_back(key3);

  TrackOptimizer optimizer;
  optimizer.tolerance = 1e-9f;
  RawQuaternionTrack optimized;
  ASSERT_TRUE(optimizer(input, &optimized));
  ExpectOptimizedSamplesMatch(input, optimized);

  ozz::animation::offline::TrackBuilder builder;
  ozz::unique_ptr<ozz::animation::QuaternionTrack> track(builder(optimized));
  ASSERT_TRUE(track);
  ozz::unique_ptr<ozz::animation::QuaternionTrack> original_track(
      builder(input));
  ASSERT_TRUE(original_track);

  // The builder puts qx and -qx on the same hemisphere. NLerp therefore keeps
  // this pair on the builder's shortest-path interpolation branch.
  const ozz::math::Quaternion before_step =
      SampleTrack<ozz::animation::QuaternionTrack,
                  ozz::animation::QuaternionTrackSamplingJob,
                  ozz::math::Quaternion>(*track, .499999f);
  ExpectTrackValueEqual(qx, before_step);

  const ozz::math::Quaternion at_close_pair =
      SampleTrack<ozz::animation::QuaternionTrack,
                  ozz::animation::QuaternionTrackSamplingJob,
                  ozz::math::Quaternion>(*track, .500001f);
  ExpectTrackValueEqual(qx, at_close_pair);

  const ozz::math::Quaternion after_close_pair =
      SampleTrack<ozz::animation::QuaternionTrack,
                  ozz::animation::QuaternionTrackSamplingJob,
                  ozz::math::Quaternion>(*track, .75f);
  const ozz::math::Quaternion original_after_close_pair =
      SampleTrack<ozz::animation::QuaternionTrack,
                  ozz::animation::QuaternionTrackSamplingJob,
                  ozz::math::Quaternion>(*original_track, .75f);
  ExpectTrackValueEqual(original_after_close_pair, after_close_pair);
}

TEST(InvalidRatiosKeepFailureContract, TrackOptimizer) {
  {
    RawFloatTrack input;
    AddTrackTestKey(&input, RawTrackInterpolation::kLinear, .2f, 1.f);
    AddTrackTestKey(&input, RawTrackInterpolation::kLinear, .1f, 2.f);
    CheckInvalidTrackFailureContract(input);
  }

  {
    RawFloatTrack input;
    AddTrackTestKey(&input, RawTrackInterpolation::kLinear, .1f, 1.f);
    AddTrackTestKey(&input, RawTrackInterpolation::kLinear, .1f, 2.f);
    CheckInvalidTrackFailureContract(input);
  }

  {
    RawFloatTrack input;
    AddTrackTestKey(&input, RawTrackInterpolation::kLinear, -.1f, 1.f);
    AddTrackTestKey(&input, RawTrackInterpolation::kLinear, .1f, 2.f);
    CheckInvalidTrackFailureContract(input);
  }

  {
    RawFloatTrack input;
    AddTrackTestKey(&input, RawTrackInterpolation::kLinear, .9f, 1.f);
    AddTrackTestKey(&input, RawTrackInterpolation::kLinear, 1.1f, 2.f);
    CheckInvalidTrackFailureContract(input);
  }
}

TEST(EmptyTracksKeepDefaultValues, TrackOptimizer) {
  CheckEmptyTrackKeepsDefaultValue<RawFloatTrack>();
  CheckEmptyTrackKeepsDefaultValue<RawFloat2Track>();
  CheckEmptyTrackKeepsDefaultValue<RawFloat3Track>();
  CheckEmptyTrackKeepsDefaultValue<RawFloat4Track>();
  CheckEmptyTrackKeepsDefaultValue<RawQuaternionTrack>();
}

TEST(MultipleTrackTypesAndMissingTrack, TrackOptimizer) {
  RawFloatTrack linear_float;
  AddTrackTestKey(&linear_float, RawTrackInterpolation::kLinear, 0.f, 0.f);
  AddTrackTestKey(&linear_float, RawTrackInterpolation::kLinear, .25f, 4.f);
  AddTrackTestKey(&linear_float, RawTrackInterpolation::kLinear, .5f, 9.f);
  AddTrackTestKey(&linear_float, RawTrackInterpolation::kLinear, 1.f, 20.f);

  RawFloat2Track step_float2;
  AddTrackTestKey(&step_float2, RawTrackInterpolation::kLinear, 0.f, 0.f);
  AddTrackTestKey(&step_float2, RawTrackInterpolation::kStep, .25f, 20.f);
  AddTrackTestKey(&step_float2, RawTrackInterpolation::kLinear, .5f, 40.f);
  AddTrackTestKey(&step_float2, RawTrackInterpolation::kLinear, 1.f, 80.f);

  RawFloat3Track linear_float3;
  AddTrackTestKey(&linear_float3, RawTrackInterpolation::kLinear, 0.f, 0.f);
  AddTrackTestKey(&linear_float3, RawTrackInterpolation::kLinear, .5f, 9.f);
  AddTrackTestKey(&linear_float3, RawTrackInterpolation::kLinear, 1.f, 20.f);

  RawFloat4Track step_float4;
  AddTrackTestKey(&step_float4, RawTrackInterpolation::kLinear, 0.f, 0.f);
  AddTrackTestKey(&step_float4, RawTrackInterpolation::kStep, .25f, 20.f);
  AddTrackTestKey(&step_float4, RawTrackInterpolation::kLinear, .5f, 40.f);
  AddTrackTestKey(&step_float4, RawTrackInterpolation::kLinear, 1.f, 80.f);

  RawQuaternionTrack quaternion;
  AddTrackTestKey(&quaternion, RawTrackInterpolation::kLinear, 0.f, 0.f);
  AddTrackTestKey(&quaternion, RawTrackInterpolation::kLinear, .5f, 1.f);
  AddTrackTestKey(&quaternion, RawTrackInterpolation::kLinear, 1.f, 50.f);

  CheckOptimizationPreservesSampling(linear_float, 1e-9f);
  CheckOptimizationPreservesSampling(step_float2, 1e-9f);
  CheckOptimizationPreservesSampling(linear_float3, 1e-9f);
  CheckOptimizationPreservesSampling(step_float4, 1e-9f);
  CheckOptimizationPreservesSampling(quaternion, 1e-12f);

  // An absent/empty track remains an identity/default source after optimizing,
  // building and archiving the rest of this track set.
  CheckEmptyTrackKeepsDefaultValue<RawFloatTrack>();
}
