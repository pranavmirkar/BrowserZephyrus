// Positive control for the TSAN setup: a deliberate, unambiguous data race.
// If ThreadSanitizer does NOT report this, the instrumentation is not actually
// active and a clean run of the real harness proves nothing.
#include <thread>

int g = 0;

int main() {
  std::thread a([] {
    for (int i = 0; i < 200000; ++i) {
      g++;
    }
  });
  std::thread b([] {
    for (int i = 0; i < 200000; ++i) {
      g++;
    }
  });
  a.join();
  b.join();
  return 0;
}
