#pragma once

#include "common_utils.h"
#include "tvalue.h"

namespace dfg {

// We want to solve the following problem:
//
//   1. Given M bitmasks a_0, ..., a_{M-1} of N bits (i.e., each a_i \in [2^N]).
//   2. Define F(x) where x \in [2^N] as an i \in [M] so that x \subset a_i,
//      and there is no j such that x \subset a_j \subsetneq a_i. If there are multiple such i, any is fine.
//   3. We want to support fast query of F(x) given x.
//
// Without losing generality, we assume {a} is closed under intersection (if not, we can expand the set to that closure).
// And without losing generality, assume a_0 = 0.
//
// Define T: [M] \times [N] -> [M] as T(i, j) := F(a_i | (1<<j))
//
// After pre-processing T, F(x) can be quickly computed by:
//   1. Let k = 0, y = 0 (note that y is only for convenience of our proof of correctness)
//   2. If x \subset a_k, answer is k
//   3. Let j be any 1-bit position where (x & ~a_k). Let y |= 1 << j, k = T(k, j). Goto 2.
//
// -- Proof of correctness --
// Note that under our assumption that {a} is closed under intersection, F(x) is always unique for any x.
// We can prove the correctness of the algorithm above by induction on that at step 2 we always have F(y) = k.
// Then, at algorithm end, since y \subset x (which is clear), we have y \subset x \subset a_k and F(y) = k.
// It is fairly easy to prove that F(x) = k as well given {a} is closed under intersection.
//
// So all is left to prove that k = F(y) at step 2 always holds.
// Base case clearly holds. Now suppose at some iteration we have k = F(y).
//
// Lemma 1. Let l := F(y | (1<<j)), then a_k \subsetneq a_l.
// Proof:
//   1. a_k \subset a_l (otherwise we will have y \subset a_k \cap a_l \subsetneq a_k, contradicting F(y) = k)
//   2. a_k \neq a_l (since j \notin a_k but j \in a_l).
//
// Lemma 2. F(a_k | (1<<j)) = l.
// Proof:
//   Suppose not, that is, F(a_k | (1<<j)) = l' \neq l. Due to Lemma 1, a_k | (1<<j) \subset a_l.
//   Then y | (1<<j) \subset a_k | (1<<j) \subset a_l' \subsetneq a_l.
//   That is, y | (1<<j) \subset a_l' \subsetneq a_l, which contradicts F(y | (1<<j)) = l.
//
// So at the next iteration, we have y' = y | (1<<j), and k' = T(k, j) := F(a_k | (1<<j)) = l = F(y | (1<<j)) = F(y'), as desired.
// -- End proof --
//
// Note that T can be viewed as an automata where the [M] dimension is the node and the [N] dimension are the edges.
// To make things more CPU-friendly, we can convert the up-to-n edges to a series of binary choices, then vectorize it to perform 4 tests at once.
//
// This yields the following automata model:
//   Each automata node is described by <clearMask, answer, testMasks[4], dests...>.
//   Query F(x) can be solved by:
//     A = entryNode
//     while True:
//       if ((x & A.clearMask) == 0) return A.answer
//       idx = 0
//       for i in [0,4):
//         idx += (x & testMasks[i]) ? (1<<i) : 0;
//       A = A.dests[idx]
//
struct __attribute__((__packed__)) TypeMaskOverapproxAutomataNodeCommon
{
    TypeMaskTy m_clearMask;
    // The meaning of 'm_answer' depend on the usage
    //
    uint16_t m_answer;
};

// Only non-terminal nodes have these extra fields
//
struct __attribute__((__packed__)) TypeMaskOverapproxAutomataNode : TypeMaskOverapproxAutomataNodeCommon
{
    static constexpr size_t x_maxTestMasks = 4;

    TypeMaskTy m_testMasks[x_maxTestMasks];
    // Note that this offset is always positive since the edge always points to a later node
    // The length of the array depends on how many masks we *actually* have: if we do not have 4 masks to test,
    // we will set the non-existent masks to 0, so we will only need 2^K entries where K is the actually existent masks
    //
    uint16_t m_dests[0];

    const TypeMaskOverapproxAutomataNodeCommon* ALWAYS_INLINE TestAndGetNext(TypeMaskTy mask) const
    {
        // uiCA says the vectorized version has higher latency than the plain version
        // vectorized version (and + cmpne 0 + movemask) has 19 cycle latency and even higher before steady state,
        // plain version 17.67 cycle latency and also performs better before steady state, so just use plain version
        //
        size_t idx = 0;
        for (size_t i = 0; i < x_maxTestMasks; i++)
        {
            if (mask & m_testMasks[i])
            {
                idx += (1U << i);
            }
        }
        TestAssert(m_dests[idx] != static_cast<uint16_t>(-1));
        uintptr_t addr = reinterpret_cast<uintptr_t>(this) + m_dests[idx];
        return reinterpret_cast<const TypeMaskOverapproxAutomataNodeCommon*>(addr);
    }

    static uint16_t WARN_UNUSED Run(const TypeMaskOverapproxAutomataNodeCommon* node, TypeMaskTy mask)
    {
        TestAssert(mask <= x_typeMaskFor<tBoxedValueTop>);
        while (true)
        {
            if ((mask & node->m_clearMask) == 0)
            {
                return node->m_answer;
            }
            node = static_cast<const TypeMaskOverapproxAutomataNode*>(node)->TestAndGetNext(mask);
        }
    }
};

struct TypeMaskOverapproxAutomata
{
    constexpr TypeMaskOverapproxAutomata() : m_entry(nullptr) { }
    constexpr TypeMaskOverapproxAutomata(const uint8_t* entry) : m_entry(entry) { }

    uint16_t WARN_UNUSED ALWAYS_INLINE RunAutomataMayFail(TypeMaskTy mask) const
    {
        return TypeMaskOverapproxAutomataNode::Run(reinterpret_cast<const TypeMaskOverapproxAutomataNodeCommon*>(m_entry), mask);
    }

    uint16_t WARN_UNUSED ALWAYS_INLINE RunAutomata(TypeMaskTy mask) const
    {
        // We use -1 to denote failure (cannot find a bitmask that covers the given mask)
        // By default we assert this shouldn't happen
        //
        uint16_t result = RunAutomataMayFail(mask);
        TestAssert(result != static_cast<uint16_t>(-1));
        return result;
    }

    // This will point to a constexpr array generated by Deegen
    //
    const uint8_t* m_entry;
};

// Similar to TypeMaskOverapproxAutomata, but optimized for the case that the bitmask contains many leaf singleton masks
// In that case, we can slightly optimize the procedure by using an index table to make our first move
//
struct TypeMaskOverapproxAutomataLeafOpted
{
    constexpr TypeMaskOverapproxAutomataLeafOpted() : m_entry(nullptr) { }
    constexpr TypeMaskOverapproxAutomataLeafOpted(const uint8_t* entry) : m_entry(entry) { }

    uint16_t WARN_UNUSED ALWAYS_INLINE RunAutomataMayFail(TypeMaskTy mask) const
    {
        // [ TypeMask ] [ u16 ] [ u16 * N ] [ automata... ]
        //
        TestAssert(mask <= x_typeMaskFor<tBoxedValueTop>);
        mask &= UnalignedLoad<TypeMaskTy>(m_entry);
        if (mask == 0)
        {
            return UnalignedLoad<uint16_t>(m_entry + sizeof(TypeMaskTy));
        }
        size_t ord = CountTrailingZeros(mask);
        TestAssert(ord < x_numUsefulBitsInBytecodeTypeMask);
        uint16_t offset = UnalignedLoad<uint16_t>(m_entry + sizeof(TypeMaskTy) + 2 + ord * 2);
        TestAssert(offset != static_cast<uint16_t>(-1));
        const TypeMaskOverapproxAutomataNodeCommon* entryNode = reinterpret_cast<const TypeMaskOverapproxAutomataNodeCommon*>(m_entry + offset);
        return TypeMaskOverapproxAutomataNode::Run(entryNode, mask);
    }

    uint16_t WARN_UNUSED ALWAYS_INLINE RunAutomata(TypeMaskTy mask) const
    {
        uint16_t result = RunAutomataMayFail(mask);
        TestAssert(result != static_cast<uint16_t>(-1));
        return result;
    }

    const uint8_t* m_entry;
};

}   // namespace dfg
