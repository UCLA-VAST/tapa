#ifndef TASK_LEVEL_PARALLELIZATION_H_
#define TASK_LEVEL_PARALLELIZATION_H_

#ifdef __SYNTHESIS__
#error this header is not synthesizable
#endif  // __SYNTHESIS__

#include <climits>
#include <cstdarg>
#include <cstdint>
#include <cstdlib>

#include <chrono>
#include <functional>
#include <iostream>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>

#include <frt.h>
#include <glog/logging.h>

#include "tapa/mmap.h"
#include "tapa/stream.h"
#include "tapa/synthesizable/traits.h"
#include "tapa/synthesizable/util.h"
#include "tapa/synthesizable/vec.h"

namespace tapa {

struct seq {};

struct task {
  task();
  task(task&&) = delete;
  task(const task&) = delete;
  ~task();

  task& operator=(task&&) = delete;
  task& operator=(const task&) = delete;

  template <typename Function, typename... Args>
  task& invoke(Function&& f, Args&&... args) {
    return invoke<join>(f, "", args...);
  }

  template <int step, typename Function, typename... Args>
  task& invoke(Function&& f, Args&&... args) {
    return invoke<step>(f, "", args...);
  }

  template <typename Function, typename... Args, size_t S>
  task& invoke(Function&& f, const char (&name)[S], Args&&... args) {
    return invoke<join>(f, name, args...);
  }

  template <int step, typename Function, typename... Args, size_t S>
  task& invoke(Function&& f, const char (&name)[S], Args&&... args) {
    internal::schedule(/* detach= */ step < 0,
                       std::bind(f, std::forward<Args>(args)...));
    return *this;
  }

  // invoke task vector without a name
  template <int step, uint64_t length, typename Function, typename... Args>
  task& invoke(Function&& f, Args&&... args) {
    return invoke<step, length>(f, "", args...);
  }

  // invoke task vector with a name
  template <int step, uint64_t length, typename Function, typename... Args,
            size_t S>
  task& invoke(Function&& f, const char (&name)[S], Args&&... args) {
    for (uint64_t i = 0; i < length; ++i) {
      this->invoke<step>(f, access(args, i)...);
    }
    return *this;
  }

 private:
  // scalar
  template <typename T>
  static T& access(T& arg, uint64_t idx) {
    return arg;
  }

  // sequence
  static int access(seq, uint64_t idx) { return idx; }

  // access streams in vector invoke
  template <typename T, uint64_t length, uint64_t depth>
  static stream<T, depth>& access(streams<T, length, depth>& arg,
                                  uint64_t idx) {
    LOG_IF(INFO, idx >= length) << "invocation #" << idx << " accesses "
                                << "stream #" << idx % length;
    return arg[idx];
  }

  // access mmaps in vector invoke
  template <typename T, uint64_t length>
  static mmap<T>& access(mmaps<T, length>& arg, uint64_t idx) {
    LOG_IF(INFO, idx >= length) << "invocation #" << idx << " accesses "
                                << "async_mmap #" << idx % length;
    return arg[idx % length];
  }
};

template <typename T, uint64_t N>
inline std::ostream& operator<<(std::ostream& os, const vec_t<T, N>& obj) {
  os << "{";
  for (uint64_t i = 0; i < N; ++i) {
    if (i > 0) os << ", ";
    os << "[" << i << "]: " << obj[i];
  }
  return os << "}";
}

namespace internal {

void* allocate(size_t length);
void deallocate(void* addr, size_t length);

// functions cannot be specialized so use classes
template <typename T>
struct dispatcher {
  static void set_arg(fpga::Instance& instance, int& idx,
                      typename std::remove_reference<T>::type& arg) {
    instance.SetArg(idx, static_cast<T&&>(arg));
    ++idx;
  }
};
#define TAPA_DEFINE_DISPATCHER(tag, frt_tag)                   \
  template <typename T>                                        \
  struct dispatcher<tag##_mmap<T>> {                           \
    static void set_arg(fpga::Instance& instance, int& idx,    \
                        tag##_mmap<T> arg) {                   \
      auto buf = fpga::frt_tag(arg.get(), arg.size());         \
      instance.AllocBuf(idx, buf);                             \
      instance.SetArg(idx, buf);                               \
      ++idx;                                                   \
    }                                                          \
  };                                                           \
  template <typename T, uint64_t S>                            \
  struct dispatcher<tag##_mmaps<T, S>> {                       \
    static void set_arg(fpga::Instance& instance, int& idx,    \
                        tag##_mmaps<T, S> arg) {               \
      for (uint64_t i = 0; i < S; ++i) {                       \
        auto buf = fpga::frt_tag(arg[i].get(), arg[i].size()); \
        instance.AllocBuf(idx, buf);                           \
        instance.SetArg(idx, buf);                             \
        ++idx;                                                 \
      }                                                        \
    }                                                          \
  }
TAPA_DEFINE_DISPATCHER(placeholder, Placeholder);
// read/write are with respect to the kernel in tapa but host in frt
TAPA_DEFINE_DISPATCHER(read_only, WriteOnly);
TAPA_DEFINE_DISPATCHER(write_only, ReadOnly);
TAPA_DEFINE_DISPATCHER(read_write, ReadWrite);
// TODO: dispatch stream correctly
#undef TAPA_DEFINE_DISPATCHER
template <typename T>
struct dispatcher<mmap<T>> {
  static_assert(!std::is_same<T, T>::value,
                "must use one of "
                "placeholder_mmap/read_only_mmap/write_only_mmap/"
                "read_write_mmap in tapa::invoke");
};
template <typename T, int64_t S>
struct dispatcher<mmaps<T, S>> {
  static_assert(!std::is_same<T, T>::value,
                "must use one of "
                "placeholder_mmaps/read_only_mmaps/write_only_mmaps/"
                "read_write_mmaps in tapa::invoke");
};

inline void set_args(fpga::Instance& instance, int idx) {}
template <typename Arg, typename... Args>
inline void set_args(fpga::Instance& instance, int idx, Arg&& arg,
                     Args&&... args) {
  dispatcher<Arg>::set_arg(instance, idx, arg);
  set_args(instance, idx, std::forward<Args>(args)...);
}

template <typename... Args>
inline int64_t invoke(const std::string& bitstream, Args&&... args) {
  auto instance = fpga::Instance(bitstream);
  set_args(instance, 0, std::forward<Args>(args)...);
  instance.WriteToDevice();
  instance.Exec();
  instance.ReadFromDevice();
  instance.Finish();
  return instance.ComputeTimeNanoSeconds();
}

template <typename Func, typename... Args>
inline int64_t invoke(bool run_in_new_process, Func&& f,
                      const std::string& bitstream, Args&&... args) {
  if (bitstream.empty()) {
    const auto tic = std::chrono::steady_clock::now();
    f(std::forward<Args>(args)...);
    const auto toc = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(toc - tic)
        .count();
  } else {
    if (run_in_new_process) {
      auto kernel_time_ns_raw = allocate(sizeof(int64_t));
      auto deleter = [](int64_t* p) { deallocate(p, sizeof(int64_t)); };
      std::unique_ptr<int64_t, decltype(deleter)> kernel_time_ns(
          reinterpret_cast<int64_t*>(kernel_time_ns_raw), deleter);
      if (pid_t pid = fork()) {
        // Parent.
        PCHECK(pid != -1);
        int status = 0;
        CHECK_EQ(wait(&status), pid);
        CHECK(WIFEXITED(status));
        CHECK_EQ(WEXITSTATUS(status), EXIT_SUCCESS);
        return *kernel_time_ns;
      }

      // Child.
      *kernel_time_ns = invoke(bitstream, std::forward<Args>(args)...);
      exit(EXIT_SUCCESS);
    } else {
      return invoke(bitstream, std::forward<Args>(args)...);
    }
  }
}

}  // namespace internal

// Host-only invoke that takes path to a bistream file as an argument. Returns
// the kernel time in nanoseconds.
template <typename Func, typename... Args>
inline int64_t invoke(Func&& f, const std::string& bitstream, Args&&... args) {
  return internal::invoke(/*run_in_new_process*/ false, f, bitstream,
                          std::forward<Args>(args)...);
}

// Workaround for the fact that Xilinx's cosim cannot run for more than once in
// each process. The mmap pointers MUST be allocated via mmap, or the updates
// won't be seen by the caller process!
template <typename Func, typename... Args>
inline int64_t invoke_in_new_process(Func&& f, const std::string& bitstream,
                                     Args&&... args) {
  return internal::invoke(/*run_in_new_process*/ true, f, bitstream,
                          std::forward<Args>(args)...);
}

template <typename T>
struct aligned_allocator {
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  T* allocate(size_t count) {
    return reinterpret_cast<T*>(internal::allocate(count * sizeof(T)));
  }
  void deallocate(T* ptr, std::size_t count) {
    internal::deallocate(ptr, count * sizeof(T));
  }
};

}  // namespace tapa

#endif  // TASK_LEVEL_PARALLELIZATION_H_
