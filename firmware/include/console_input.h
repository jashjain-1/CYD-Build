#pragma once
#include <cstddef>

// Reject overlong lines in their entirety; never execute a truncated prefix.
class ConsoleLine {
 public:
  enum Result { Pending, Ready, Overflow };
  Result push(char c) {
    if (c == '\r') return Pending;
    if (c == '\n') {
      data_[length_] = '\0';
      const Result result = overflow_ ? Overflow : length_ ? Ready : Pending;
      length_ = 0;
      overflow_ = false;
      return result;
    }
    if (length_ < sizeof(data_) - 1) data_[length_++] = c;
    else overflow_ = true;
    return Pending;
  }
  const char* text() const { return data_; }
 private:
  char data_[160] = {};
  std::size_t length_ = 0;
  bool overflow_ = false;
};
