#include "iq/AST/AST.h"

#include "iq/AST/Casting.h"

#include <type_traits>

// This TU has no runtime contents yet (the --dump-ast printer lands with the
// parser). Its job for now is to compile the AST headers and statically prove
// the arena's core invariant: every node is trivially destructible, so the
// arena can drop them all at once without running a single destructor.

namespace iq
{
namespace
{

template <typename T>
constexpr bool kArenaSafe = std::is_trivially_destructible_v<T>;

static_assert(kArenaSafe<NamedType>);
static_assert(kArenaSafe<ArrayType>);
static_assert(kArenaSafe<TupleType>);

static_assert(kArenaSafe<IdentPattern>);
static_assert(kArenaSafe<WildcardPattern>);
static_assert(kArenaSafe<TuplePattern>);
static_assert(kArenaSafe<ArrayPattern>);
static_assert(kArenaSafe<RestPattern>);

static_assert(kArenaSafe<NumberLiteral>);
static_assert(kArenaSafe<BoolLiteral>);
static_assert(kArenaSafe<StringLiteral>);
static_assert(kArenaSafe<NameExpr>);
static_assert(kArenaSafe<UnaryExpr>);
static_assert(kArenaSafe<BinaryExpr>);
static_assert(kArenaSafe<AssignExpr>);
static_assert(kArenaSafe<CallExpr>);
static_assert(kArenaSafe<IndexExpr>);
static_assert(kArenaSafe<CastExpr>);
static_assert(kArenaSafe<RangeExpr>);
static_assert(kArenaSafe<ArrayExpr>);
static_assert(kArenaSafe<TupleExpr>);

static_assert(kArenaSafe<BlockStmt>);
static_assert(kArenaSafe<LetStmt>);
static_assert(kArenaSafe<ExprStmt>);
static_assert(kArenaSafe<ReturnStmt>);
static_assert(kArenaSafe<BreakStmt>);
static_assert(kArenaSafe<ContinueStmt>);
static_assert(kArenaSafe<IfStmt>);
static_assert(kArenaSafe<WhileStmt>);
static_assert(kArenaSafe<ForStmt>);

static_assert(kArenaSafe<FnDecl>);
static_assert(kArenaSafe<ConstDecl>);

} // namespace
} // namespace iq