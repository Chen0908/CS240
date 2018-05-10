/* Double Check Locking Pattern */

#include <chloros.h>
#include <atomic>
#include <thread>
#include <vector>

// std::atomic<int> x(0), y(0);
// void GreenThreadWorker1(void *) {
// 	printf("Green thread on kernel thread.\n");
// 	if (x == 1) { y.fetch_add(1, std::memory_order_relaxed); }
// 	printf("y = %d\n", y.load());
// }

// void GreenThreadWorker2(void *) {
// 	if (y == 1) { x.fetch_add(1, std::memory_order_relaxed); }
// 	printf("x = %d\n", x.load());
// }

int data  = 0, ready = 0;

void use(int data) {
  printf("use(data) prints %d, should've been 2000 if no data race\n", data);
}

void GreenThreadWorker1(void *) {
	printf("Green thread on kernel thread.\n");
	data = 2000;
  ready = 1;
}

void GreenThreadWorker2(void *) {
	while (!ready) {}
  use(data);
}

void KernelThreadWorker(int n) {
  chloros::Initialize();
  if (n == 0) {
  	chloros::Spawn(GreenThreadWorker1, nullptr);
  }
  else if (n == 1) {
	chloros::Spawn(GreenThreadWorker2, nullptr);
  }
  chloros::Wait();
  printf("Finished thread %d.\n", n);
}

int main() {
  std::vector<std::thread> threads;
  threads.emplace_back(KernelThreadWorker, 0);
  threads.emplace_back(KernelThreadWorker, 1);

  for (int i = 0; i < 2; ++i) { threads[i].join(); }
  return 0;
}