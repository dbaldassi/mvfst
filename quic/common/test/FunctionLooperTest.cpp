/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <quic/common/FunctionLooper.h>
#include <quic/common/events/FollyQuicEventBase.h>
#include <quic/common/events/HighResQuicTimer.h>

#include <gtest/gtest.h>

using namespace std;
using namespace folly;
using namespace testing;

namespace quic {
namespace test {

class FunctionLooperTest : public Test {};

TEST(FunctionLooperTest, LooperNotRunning) {
  folly::EventBase backingEvb;
  auto evb = std::make_shared<FollyQuicEventBase>(&backingEvb);
  bool called = false;
  auto func = [&]() { called = true; };
  FunctionLooper::Ptr looper(
      new FunctionLooper(evb, std::move(func), LooperType::ReadLooper));
  evb->loopOnce();
  EXPECT_FALSE(called);
  evb->loopOnce();
  EXPECT_FALSE(called);
  EXPECT_FALSE(looper->isRunning());
}

TEST(FunctionLooperTest, LooperStarted) {
  folly::EventBase backingEvb;
  auto evb = std::make_shared<FollyQuicEventBase>(&backingEvb);
  bool called = false;
  auto func = [&]() { called = true; };
  FunctionLooper::Ptr looper(
      new FunctionLooper(evb, std::move(func), LooperType::ReadLooper));
  looper->run();
  EXPECT_TRUE(looper->isRunning());
  evb->loopOnce();
  EXPECT_TRUE(called);
  called = false;
  evb->loopOnce();
  EXPECT_TRUE(called);
}

TEST(FunctionLooperTest, LooperStopped) {
  folly::EventBase backingEvb;
  auto evb = std::make_shared<FollyQuicEventBase>(&backingEvb);
  bool called = false;
  auto func = [&]() { called = true; };
  FunctionLooper::Ptr looper(
      new FunctionLooper(evb, std::move(func), LooperType::ReadLooper));
  looper->run();
  evb->loopOnce();
  EXPECT_TRUE(called);
  called = false;
  looper->stop();
  EXPECT_FALSE(looper->isRunning());
  evb->loopOnce();
  EXPECT_FALSE(called);
}

TEST(FunctionLooperTest, LooperRestarted) {
  folly::EventBase backingEvb;
  auto evb = std::make_shared<FollyQuicEventBase>(&backingEvb);
  bool called = false;
  auto func = [&]() { called = true; };
  FunctionLooper::Ptr looper(
      new FunctionLooper(evb, std::move(func), LooperType::ReadLooper));
  looper->run();
  evb->loopOnce();
  EXPECT_TRUE(called);
  called = false;
  looper->stop();
  evb->loopOnce();
  EXPECT_FALSE(called);
  looper->run();
  EXPECT_TRUE(looper->isRunning());
  evb->loopOnce();
  EXPECT_TRUE(called);
}

TEST(FunctionLooperTest, DestroyLooperDuringFunc) {
  folly::EventBase backingEvb;
  auto evb = std::make_shared<FollyQuicEventBase>(&backingEvb);
  bool called = false;
  FunctionLooper::Ptr* looperPtr = nullptr;

  auto func = [&]() {
    called = true;
    *looperPtr = nullptr;
  };
  FunctionLooper::Ptr looper(
      new FunctionLooper(evb, std::move(func), LooperType::ReadLooper));
  looperPtr = &looper;

  looper->run();
  evb->loopOnce();
  EXPECT_TRUE(called);
  EXPECT_EQ(looper, nullptr);
}

TEST(FunctionLooperTest, StopLooperDuringFunc) {
  folly::EventBase backingEvb;
  auto evb = std::make_shared<FollyQuicEventBase>(&backingEvb);
  bool called = false;
  FunctionLooper::Ptr* looperPtr = nullptr;

  auto func = [&]() {
    called = true;
    (*looperPtr)->stop();
  };
  FunctionLooper::Ptr looper(
      new FunctionLooper(evb, std::move(func), LooperType::ReadLooper));
  looperPtr = &looper;

  looper->run();
  evb->loopOnce();
  EXPECT_TRUE(called);
  called = false;
  evb->loopOnce();
  EXPECT_FALSE(called);
}

TEST(FunctionLooperTest, RunLooperDuringFunc) {
  folly::EventBase backingEvb;
  auto evb = std::make_shared<FollyQuicEventBase>(&backingEvb);
  bool called = false;
  FunctionLooper::Ptr* looperPtr = nullptr;

  auto func = [&]() {
    called = true;
    (*looperPtr)->run();
  };
  FunctionLooper::Ptr looper(
      new FunctionLooper(evb, std::move(func), LooperType::ReadLooper));
  looperPtr = &looper;

  looper->run();
  evb->loopOnce();
  EXPECT_TRUE(called);
  called = false;
  evb->loopOnce();
  EXPECT_TRUE(called);
}

TEST(FunctionLooperTest, DetachStopsLooper) {
  folly::EventBase backingEvb;
  auto evb = std::make_shared<FollyQuicEventBase>(&backingEvb);
  bool called = false;
  auto func = [&]() { called = true; };
  FunctionLooper::Ptr looper(
      new FunctionLooper(evb, std::move(func), LooperType::ReadLooper));
  looper->run();
  EXPECT_TRUE(looper->isRunning());
  looper->detachEventBase();
  EXPECT_FALSE(looper->isRunning());
  looper->attachEventBase(evb);
  EXPECT_FALSE(looper->isRunning());
}

TEST(FunctionLooperTest, PacingOnce) {
  folly::EventBase backingEvb;
  auto evb = std::make_shared<FollyQuicEventBase>(&backingEvb);
  QuicTimer::SharedPtr pacingTimer =
      std::make_shared<HighResQuicTimer>(evb->getBackingEventBase(), 1ms);
  int count = 0;
  auto func = [&]() { ++count; };
  bool firstTime = true;
  auto pacingFunc = [&]() -> auto {
    if (firstTime) {
      firstTime = false;
      return 3600000ms;
    }
    return std::chrono::milliseconds::zero();
  };
  FunctionLooper::Ptr looper(
      new FunctionLooper(evb, std::move(func), LooperType::ReadLooper));
  looper->setPacingTimer(std::move(pacingTimer));
  looper->setPacingFunction(std::move(pacingFunc));
  looper->run();
  evb->loopOnce();
  EXPECT_EQ(1, count);
  EXPECT_TRUE(looper->isPacingScheduled());
  looper->timeoutExpired();
  EXPECT_EQ(2, count);
  looper->stop();
}

TEST(FunctionLooperTest, KeepPacing) {
  folly::EventBase backingEvb;
  auto evb = std::make_shared<FollyQuicEventBase>(&backingEvb);
  QuicTimer::SharedPtr pacingTimer =
      std::make_shared<HighResQuicTimer>(evb->getBackingEventBase(), 1ms);
  int count = 0;
  auto func = [&]() { ++count; };
  bool stopPacing = false;
  auto pacingFunc = [&]() -> auto {
    if (stopPacing) {
      return std::chrono::milliseconds::zero();
    }
    return 3600000ms;
  };
  FunctionLooper::Ptr looper(
      new FunctionLooper(evb, std::move(func), LooperType::ReadLooper));
  looper->setPacingTimer(pacingTimer);
  looper->setPacingFunction(std::move(pacingFunc));
  looper->run();
  evb->loopOnce();
  EXPECT_EQ(1, count);
  EXPECT_TRUE(looper->isPacingScheduled());

  pacingTimer->cancelTimeout(looper.get());
  EXPECT_FALSE(looper->isPacingScheduled());
  looper->timeoutExpired();
  EXPECT_EQ(2, count);
  EXPECT_TRUE(looper->isPacingScheduled());

  pacingTimer->cancelTimeout(looper.get());
  EXPECT_FALSE(looper->isPacingScheduled());
  looper->timeoutExpired();
  EXPECT_EQ(3, count);
  EXPECT_TRUE(looper->isPacingScheduled());

  stopPacing = true;
  pacingTimer->cancelTimeout(looper.get());
  EXPECT_FALSE(looper->isPacingScheduled());
  looper->timeoutExpired();
  EXPECT_EQ(4, count);
  EXPECT_FALSE(looper->isPacingScheduled());

  looper->stop();
}

TEST(FunctionLooperTest, TimerTickSize) {
  folly::EventBase backingEvb;
  auto evb = std::make_shared<FollyQuicEventBase>(&backingEvb);
  QuicTimer::SharedPtr pacingTimer =
      std::make_shared<HighResQuicTimer>(evb->getBackingEventBase(), 123ms);
  FunctionLooper::Ptr looper(new FunctionLooper(
      evb, [&]() {}, LooperType::ReadLooper));
  looper->setPacingTimer(std::move(pacingTimer));
  EXPECT_EQ(123ms, looper->getTimerTickInterval());
}

TEST(FunctionLooperTest, TimerTickSizeAfterNewEvb) {
  folly::EventBase backingEvb;
  auto evb = std::make_shared<FollyQuicEventBase>(&backingEvb);
  QuicTimer::SharedPtr pacingTimer =
      std::make_shared<HighResQuicTimer>(evb->getBackingEventBase(), 123ms);
  FunctionLooper::Ptr looper(new FunctionLooper(
      evb, [&]() {}, LooperType::ReadLooper));
  looper->setPacingTimer(std::move(pacingTimer));
  EXPECT_EQ(123ms, looper->getTimerTickInterval());
  looper->detachEventBase();
  folly::EventBase backingEvb2;
  auto evb2 = std::make_shared<FollyQuicEventBase>(&backingEvb);
  looper->attachEventBase(evb2);
  EXPECT_EQ(123ms, looper->getTimerTickInterval());
}

TEST(FunctionLooperTest, NoLoopCallbackInPacingMode) {
  folly::EventBase backingEvb;
  auto evb = std::make_shared<FollyQuicEventBase>(&backingEvb);
  QuicTimer::SharedPtr pacingTimer =
      std::make_shared<HighResQuicTimer>(evb->getBackingEventBase(), 1ms);
  auto runFunc = [&]() {};
  auto pacingFunc = [&]() { return 3600000ms; };
  FunctionLooper::Ptr looper(
      new FunctionLooper(evb, std::move(runFunc), LooperType::ReadLooper));
  looper->setPacingTimer(std::move(pacingTimer));
  looper->setPacingFunction(std::move(pacingFunc));
  // bootstrap the looper
  looper->run();
  // this loop will schedule pacer not looper:
  evb->loopOnce();
  EXPECT_TRUE(looper->isPacingScheduled());
  EXPECT_FALSE(looper->isLoopCallbackScheduled());
  looper->stop();
}

} // namespace test
} // namespace quic
