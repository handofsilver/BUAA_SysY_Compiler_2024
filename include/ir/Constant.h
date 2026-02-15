/**
 * @file Constant.h
 * @brief Constant: base class for compile-time constant values.
 *
 * Constants are Values that do not change at runtime (literals, global
 * addresses, function addresses). GlobalVar and Function inherit from
 * Constant because their addresses are constants.
 */

#ifndef IR_CONSTANT_H
#define IR_CONSTANT_H

#include "ir/User.h"

namespace ir {

/**
 * @brief Base class for all constant values in the IR.
 *
 * Constants are Users (they may have operands, e.g. constant expressions).
 * Subclasses: GlobalVar, Function, and possibly ConstantInt, etc.
 */
class Constant : public User {
 public:
  Constant() = default;
  Constant(const std::string& name, Type* type) : User(name, type) {}
  virtual ~Constant() = default;
};

}  // namespace ir

#endif  // IR_CONSTANT_H
