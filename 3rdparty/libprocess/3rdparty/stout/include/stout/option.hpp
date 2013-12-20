#ifndef __STOUT_OPTION_HPP__
#define __STOUT_OPTION_HPP__

#include <assert.h>

#include <algorithm>

#include <stout/result.hpp>

template <typename T>
class Option
{
public:
  static Option<T> none()
  {
    return Option<T>(NONE);
  }

  static Option<T> some(const T& t)
  {
    return Option<T>(SOME, new T(t));
  }

  Option() : state(NONE), t(NULL) {}

  Option(const T& _t) : state(SOME), t(new T(_t)) {}

  Option(const Option<T>& that)
  {
    state = that.state;
    if (that.t != NULL) {
      t = new T(*that.t);
    } else {
      t = NULL;
    }
  }

  ~Option()
  {
    delete t;
  }

  operator Result<T> () const
  {
    if (isNone()) {
      return Result<T>::none();
    }

    return Result<T>::some(get());
  }

  Option<T>& operator = (const Option<T>& that)
  {
    if (this != &that) {
      delete t;
      state = that.state;
      if (that.t != NULL) {
        t = new T(*that.t);
      } else {
        t = NULL;
      }
    }

    return *this;
  }

  bool operator == (const Option<T>& that) const
  {
    return (state == NONE && that.state == NONE) ||
      (state == SOME && that.state == SOME && *t == *that.t);
  }

  bool operator != (const Option<T>& that) const
  {
    return !operator == (that);
  }

  bool operator == (const T& that) const
  {
    return state == SOME && *t == that;
  }

  bool operator != (const T& that) const
  {
    return !operator == (that);
  }

  bool isSome() const { return state == SOME; }
  bool isNone() const { return state == NONE; }

  T get() const { assert(state == SOME); return *t; }

  T get(const T& _t) const { return state == NONE ? _t : *t; }

private:
  enum State {
    SOME,
    NONE,
  };

  Option(State _state, T* _t = NULL)
    : state(_state), t(_t) {}

  State state;
  T* t;
};


template <typename T>
Option<T> min(const Option<T>& left, const Option<T>& right)
{
  if (left.isSome() && right.isSome()) {
    return std::min(left.get(), right.get());
  } else if (left.isSome()) {
    return left.get();
  } else if (right.isSome()) {
    return right.get();
  } else {
    return Option<T>::none();
  }
}


template <typename T>
Option<T> min(const Option<T>& left, const T& right)
{
  return min(left, Option<T>(right));
}


template <typename T>
Option<T> min(const T& left, const Option<T>& right)
{
  return min(Option<T>(left), right);
}


template <typename T>
Option<T> max(const Option<T>& left, const Option<T>& right)
{
  if (left.isSome() && right.isSome()) {
    return std::max(left.get(), right.get());
  } else if (left.isSome()) {
    return left.get();
  } else if (right.isSome()) {
    return right.get();
  } else {
    return Option<T>::none();
  }
}


template <typename T>
Option<T> max(const Option<T>& left, const T& right)
{
  return max(left, Option<T>(right));
}


template <typename T>
Option<T> max(const T& left, const Option<T>& right)
{
  return max(Option<T>(left), right);
}

#endif // __STOUT_OPTION_HPP__
