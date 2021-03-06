#include "chloros.h"
#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <queue>
#include <vector>
#include "common.h"
#include <iostream>

extern "C" {

// Assembly code to switch context from `old_context` to `new_context`.
void ContextSwitch(chloros::Context* old_context,
                   chloros::Context* new_context) __asm__("context_switch");

// Assembly entry point for a thread that is spawn. It will fetch arguments on
// the stack and put them in the right registers. Then it will call
// `ThreadEntry` for further setup.
void StartThread(void* arg) __asm__("start_thread");
}

namespace chloros {

namespace {

// Default stack size is 2 MB.
constexpr int const kStackSize{1 << 21};

// Queue of threads that are not running.
std::vector<std::unique_ptr<Thread>> thread_queue{};
std::atomic<uint64_t> curr_pos(-1); // round-robin starts at curr_pos + 1
std::atomic<bool> exist_zombie(false);
// Mutex protecting the queue, which will potentially be accessed by multiple
// kernel threads.
std::mutex queue_lock{};

// Current running thread. It is local to the kernel thread.
thread_local std::unique_ptr<Thread> current_thread{nullptr};

// Thread ID of initial thread. In extra credit phase, we want to switch back to
// the initial thread corresponding to the kernel thread. Otherwise there may be
// errors during thread cleanup. In other words, if the initial thread is
// spawned on this kernel thread, never switch it to another kernel thread!
// Think about how to achieve this in `Yield` and using this thread-local
// `initial_thread_id`.
thread_local uint64_t initial_thread_id;

}  // anonymous namespace

std::atomic<uint64_t> Thread::next_id(0);

Thread::Thread(bool create_stack)
    : id{next_id++}, state{State::kWaiting}, context{}, stack{nullptr}, 
      unaligned_stack{nullptr} {
  // FIXME: Phase 1
  if(create_stack){
    unaligned_stack = (uint8_t *)std::calloc(kStackSize + 15, sizeof(uint8_t));
    stack = unaligned_stack + (kStackSize + 15); // move the high end
    if (reinterpret_cast<uintptr_t>(stack) % 16 != 0) {
      stack = (uint8_t*)(((uint64_t)stack) & (~0xf));
    }
  }

  static_cast<void>(create_stack);
  // These two initial values are provided for you.
  context.mxcsr = 0x1F80;
  context.x87 = 0x037F;
}

Thread::~Thread() {
  // FIXME: Phase 1
  free(unaligned_stack);
}

void Thread::PrintDebug() {
  fprintf(stderr, "Thread %" PRId64 ": ", id);
  switch (state) {
    case State::kWaiting:
      fprintf(stderr, "waiting");
      break;
    case State::kReady:
      fprintf(stderr, "ready");
      break;
    case State::kRunning:
      fprintf(stderr, "running");
      break;
    case State::kZombie:
      fprintf(stderr, "zombie");
      break;
    default:
      break;
  }
  fprintf(stderr, "\n\tStack: %p\n", stack);
  fprintf(stderr, "\tRSP: 0x%" PRIx64 "\n", context.rsp);
  fprintf(stderr, "\tR15: 0x%" PRIx64 "\n", context.r15);
  fprintf(stderr, "\tR14: 0x%" PRIx64 "\n", context.r14);
  fprintf(stderr, "\tR13: 0x%" PRIx64 "\n", context.r13);
  fprintf(stderr, "\tR12: 0x%" PRIx64 "\n", context.r13);
  fprintf(stderr, "\tRBX: 0x%" PRIx64 "\n", context.rbx);
  fprintf(stderr, "\tRBP: 0x%" PRIx64 "\n", context.rbp);
  fprintf(stderr, "\tMXCSR: 0x%x\n", context.mxcsr);
  fprintf(stderr, "\tx87: 0x%x\n", context.x87);
}

void Initialize() {
  auto new_thread = std::make_unique<Thread>(false);
  new_thread->state = Thread::State::kWaiting;
  initial_thread_id = new_thread->id;
  current_thread = std::move(new_thread);
}

void Spawn(Function fn, void* arg) {
  auto new_thread = std::make_unique<Thread>(true);
  // FIXME: Phase 3
  // Set up the initial stack, and put it in `thread_queue`. Must yield to it
  // afterwards. How do we make sure it's executed right away?
  
  *(uint64_t *)new_thread->stack = reinterpret_cast<uint64_t>(&StartThread);
  *(uint64_t *)(new_thread->stack + 8) = reinterpret_cast<uint64_t>(fn);
  *(uint64_t *)(new_thread->stack + 16) = reinterpret_cast<uint64_t>(arg);
  new_thread->context.rsp = reinterpret_cast<uint64_t>(new_thread->stack);
  queue_lock.lock();
  thread_queue.insert(thread_queue.begin() + curr_pos + 1, std::move(new_thread));
  queue_lock.unlock();
  Yield(false);
}

bool Yield(bool only_ready) {
  // FIXME: Phase 3
  // Find a thread to yield to. If `only_ready` is true, only consider threads
  // in `kReady` state. Otherwise, also consider `kWaiting` threads. Be careful,
  // never schedule initial thread onto other kernel threads (for extra credit
  // phase)!
  static_cast<void>(only_ready);
  queue_lock.lock();
  if (thread_queue.empty()) {
    queue_lock.unlock();
    return false;
  }
  
  uint64_t i = (curr_pos + 1) % thread_queue.size();
  int sz = thread_queue.size();
  while (sz--) {
    if ((only_ready && thread_queue[i]->state == Thread::State::kReady)
      || (!only_ready && (thread_queue[i]->state == Thread::State::kReady 
      || thread_queue[i]->state == Thread::State::kWaiting))) {
      if (current_thread->state == Thread::State::kRunning) {
        current_thread->state = Thread::State::kReady;
      }    
      std::swap(current_thread, thread_queue[i]); 
      current_thread->state = Thread::State::kRunning;
      curr_pos = i;

      ContextSwitch(&(thread_queue[i]->context), &(current_thread->context));
      if (exist_zombie) {
        GarbageCollect();
        exist_zombie = false;
      }
      queue_lock.unlock();
      return true;
    }
    i = (i + 1) % thread_queue.size();
  }
  queue_lock.unlock();
  return false;
}

void Wait() {
  current_thread->state = Thread::State::kWaiting;
  while (Yield(true)) {
    current_thread->state = Thread::State::kWaiting;
  }
}

void GarbageCollect() {
  // FIXME: Phase 4
  std::vector<std::unique_ptr<Thread>> new_queue;
  uint64_t zombie_before_curr_pos = 0;
  for (uint64_t i = 0; i < thread_queue.size(); i++) {
    if (thread_queue[i]->state != Thread::State::kZombie) {
      new_queue.push_back(std::move(thread_queue[i]));
    }
    else { // thread_queue[i] is zombie
      if (i <= curr_pos) { zombie_before_curr_pos++; }
    }
  }
  curr_pos -= zombie_before_curr_pos;
  swap(new_queue, thread_queue);
}

std::pair<int, int> GetThreadCount() {
  // Please don't modify this function.
  int ready = 0;
  int zombie = 0;
  std::lock_guard<std::mutex> lock{queue_lock};
  for (auto&& i : thread_queue) {
    if (i->state == Thread::State::kZombie) {
      ++zombie;
    } else {
      ++ready;
    }
  }
  return {ready, zombie};
}

void ThreadEntry(Function fn, void* arg) {
  queue_lock.unlock();
  fn(arg);
  current_thread->state = Thread::State::kZombie;
  exist_zombie = true;
  LOG_DEBUG("Thread %" PRId64 " exiting.", current_thread->id);
  
  // A thread that is spawn will always die yielding control to other threads.
  chloros::Yield();

  // Unreachable here. Why?
  ASSERT(false);
}

}  // namespace chloros
