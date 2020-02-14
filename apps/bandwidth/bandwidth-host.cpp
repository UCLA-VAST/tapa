#include <chrono>
#include <iostream>
#include <vector>

#include <boost/align.hpp>

#include <tlp.h>

#include "bandwidth.h"

template <typename T>
using vector = std::vector<T, boost::alignment::aligned_allocator<T, 4096>>;

void Bandwidth(tlp::async_mmap<Elem> chan0, tlp::async_mmap<Elem> chan1,
               tlp::async_mmap<Elem> chan2, tlp::async_mmap<Elem> chan3,
               uint64_t n);

int main(int argc, char* argv[]) {
  const uint64_t n = argc > 1 ? atoll(argv[1]) : 1024 * 1024;

  vector<Elem> chan[4];
  for (int64_t i = 0; i < kBankCount; ++i) {
    chan[i].resize(n);
    for (int64_t j = 0; j < n; ++j) {
      for (int64_t k = 0; k < Elem::length; ++k) {
        chan[i][j].set(k, i ^ j ^ k);
      }
    }
  }

  Bandwidth(chan[0], chan[1], chan[2], chan[3], n);

  int64_t num_errors = 0;
  const int64_t threshold = 10;  // only report up to these errors
  for (int64_t i = 0; i < kBankCount; ++i) {
    for (int64_t j = 0; j < n; ++j) {
      for (int64_t k = 0; k < Elem::length; ++k) {
        int64_t expected = i ^ j ^ k;
        int64_t actual = chan[i][j][k];
        if (actual != expected) {
          if (num_errors < threshold) {
            LOG(ERROR) << "expected: " << expected << ", actual: " << actual;
          } else if (num_errors == threshold) {
            LOG(ERROR) << "...";
          }
          ++num_errors;
        }
      }
    }
  }
  if (num_errors == 0) {
    LOG(INFO) << "PASS!";
  } else {
    if (num_errors > threshold) {
      LOG(WARNING) << " (+" << (num_errors - threshold) << " more errors)";
    }
    LOG(INFO) << "FAIL!";
  }
  return num_errors > 0 ? 1 : 0;
}