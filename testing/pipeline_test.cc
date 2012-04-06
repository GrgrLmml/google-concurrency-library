// Copyright 2011 Google Inc. All Rights Reserved.
//

#include <vector>
using std::vector;

#include "pipeline.h"
#include "blocking_queue.h"
#include "buffer_queue.h"
#include "countdown_latch.h"
#include "source.h"

#include "gtest/gtest.h"
#include <tr1/functional>
#include <string>
#include <sstream>
#include <iostream>
#include <stdio.h>

using std::cout;
using std::string;
using std::tr1::function;
using std::tr1::bind;
using std::tr1::placeholders::_1;

using namespace gcl;

// We have a Pipeline that converts string to UserIDs, and then
// UserIDs to User objects.

// Dummy User class
class User {
 public:
  User(int uid) : uid_(uid) {
  }
  string get_name() {
    std::stringstream o;
    o << "(User : " << uid_ << ")";
    return o.str();
  }
 private:
  int uid_;
};

// String -> UID
int find_uid(string val) {
  printf("find_uid for %s\n", val.c_str());
  return val.length();
}

// UID -> User
User get_user(int uid) {
  printf("get for %d\n", uid);
  return User(uid);
}

// Processes the User
void consume_user(User input) {
  printf("Consuming %s\n", input.get_name().c_str());
}


// Function invoked when a pipeline ends.
void OnEnd(countdown_latch* latch) {
  printf("Ending pipeline\n");
  latch->count_down();
}

void print_string(string s) {
  printf("%s", s.c_str());
}

class PipelineTest : public testing::Test {
};

PipelineTerm Nothing(PipelineTerm) {
  printf("Nothing\n");
  return PipelineTerm();
};

// TODO(alasdair): Not really a test - just shows a (very basic)
// pipeline in action. Add proper tests once the interface is sorted
// out.
TEST_F(PipelineTest, Example) {
  simple_thread_pool pool;
  countdown_latch latch(1);
  function <void ()> end_fn = bind(&OnEnd, &latch);

  function <int (string input)> f1 = find_uid;
  function <User (int uid)> f2 = get_user;
  function <void (User user)> c = consume_user;

  // Simple one-stage pipeline
  SimplePipeline<string, int> p1(f1);
  int uid = p1.Apply("hello");
  printf("Got uid %d\n", uid);

  // Two-stage pipeline. Combines string->int and int->User to make
  // string->User
  SimplePipeline<string, User> p2 = Filter(f1)|Filter(f2);
  User a2 = p2.Apply("hello world");
  printf("Got %s from pipeline\n", a2.get_name().c_str());

  // RunnablePipeline that reads from a queue and writers to a sink
  buffer_queue<string> queue(10);
  queue.push("Queued Hello");
  queue.push("queued world");

  printf("Creating p3\n");
  FullPipeline<PipelineTerm, User> p3 =
    Source(&queue)|
    Filter(f1)|Filter(f2);

  printf("Creating p4\n");
  Pipeline p4 = p3|Consume(c);

  printf("Running p4\n");
  //Pipeline p5 = p4.OnEnd(end_fn);
  //TODO(aberkan): run should return something we can wait on
  p4.run(pool);
  queue.push("More stuff");
  queue.push("Yet More stuff");
  queue.push("Are we done yet???");
  queue.close();

  printf("Test Done\n");

  //TODO(aberkan): We need to wait for the pipeline to complete.
  sleep(1);
  //latch.wait();
  //
}

#if 0
TEST_F(PipelineTest, ParallelExample) {
  countdown_latch latch(1);
  function <void ()> end_fn = bind(&OnEnd, &latch);

  function <int (string input)> f1 = find_uid;
  function <User (int uid)> f2 = get_user;
  function <void (User user)> c = consume_user;

  // Simple one-stage pipeline
  Pipeline<string, int> p1(f1);
  int uid = p1.apply("hello");
  printf("Got uid %d\n", uid);

  // Two-stage pipeline. Combines string->int and int->User to make
  // string->User
  Pipeline<string, User> p2 = Pipeline<string, int>(f1).Filter<User>(f2);
  User a2 = p2.apply("hello world");
  printf("Got %s from pipeline\n", a2.get_name().c_str());


  // RunnablePipeline that reads from a queue and writers to a sink
  typedef source<string, blocking_queue<string> > string_source;
  blocking_queue<string> queue;
  queue.push("Queued Hello");
  queue.push("queued world");
  queue.push("queued 1");
  queue.push("queued 22");
  queue.push("queued 333");
  queue.push("queued 4444");
  queue.push("queued 55555");
  queue.push("queued 666666");
  string_source in_source(&queue);
  StartablePipeline<string, User, string_source> p3 =
    Pipeline<string, int>(f1).Filter<User>(f2)
      .Source<string_source>(in_source);

  simple_thread_pool pool;
  RunnablePipeline<string, User, string_source> p4 = p3.Consume(c).Parallel(3);
  p4.run(pool);
  queue.push("More stuff");
  queue.push("Yet More stuff");
  queue.push("Are we done yet???");
  queue.close();
  p4.wait();
}

#endif
