/**
 * @file Use.h
 * @brief Use-Def chain: Use represents one edge in the def-use dependency graph.
 *
 * A Use links a User (e.g. an Instruction) to a Value it uses (e.g. an operand).
 * Each Use belongs to exactly one User and references exactly one Value.
 */
#pragma once

namespace ir {

    class Value;
    class User;

    /**
     * @brief Represents one "use" of a Value by a User.
     *
     * Memory layout: stored inline in User::operands_ for cache locality.
     * value_ is the Value being used; user_ is the User that owns this Use;
     * operand_no_ is the index of this operand in the User's operand list.
     */
    class Use {
    public:
        Use() : value_(nullptr), user_(nullptr), operand_no_(-1) {}
        Use(Value* value, User* user, int operand_no) :
        value_(value),
        user_(user),
        operand_no_(operand_no) {}

        Value* GetValue() const {
            return value_;
        }
        void SetValue(Value* v) {
            value_ = v;
        }

        User* GetUser() const {
            return user_;
        }
        void SetUser(User* u) {
            user_ = u;
        }

        int GetOperandNo() const {
            return operand_no_;
        }
        void SetOperandNo(int n) {
            operand_no_ = n;
        }

    private:
        Value* value_;   /**< The Value that is used. */
        User* user_;     /**< The User that owns this Use. */
        int operand_no_; /**< Index of this operand in the User. */
    };

} // namespace ir
