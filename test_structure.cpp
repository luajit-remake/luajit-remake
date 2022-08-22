#include "bytecode.h"
#include "gtest/gtest.h"

namespace {

using StringList = std::vector<UserHeapPointer<HeapString>>;

StringList GetStringList(VM* vm, size_t n)
{
    std::set<std::string> used;
    StringList result;
    std::set<int64_t> ptrSet;
    for (size_t i = 0; i < n; i++)
    {
        std::string s;
        while (true)
        {
            s = "";
            size_t len = static_cast<size_t>(rand() % 20);
            for (size_t k = 0; k < len; k++) s += 'a' + rand() % 26;
            if (!used.count(s))
            {
                break;
            }
        }
        used.insert(s);
        UserHeapPointer<HeapString> ptr = vm->CreateStringObjectFromRawString(s.c_str(), static_cast<uint32_t>(s.length()));
        result.push_back(ptr);
        ReleaseAssert(!ptrSet.count(ptr.m_value));
        ptrSet.insert(ptr.m_value);
    }
    ReleaseAssert(used.size() == n && ptrSet.size() == n && result.size() == n);
    return result;
}

struct Edge
{
    size_t m_child;
    UserHeapPointer<HeapString> m_key;
};

struct Tree
{
    std::vector<std::vector<Edge>> m_edges;
    std::vector<std::unordered_map<int64_t, size_t>> m_expectedContents;
    std::vector<std::unordered_map<int64_t, size_t>> m_expectedTransitions;
};

Tree BuildTree(size_t n, size_t degreeParam, const StringList& strings)
{
    constexpr size_t maxDepth = Structure::x_maxNumSlots;
    std::vector<size_t> p; p.resize(n);
    std::vector<size_t> depth; depth.resize(n); depth[0] = 0;
    for (size_t i = 1; i < n; i++)
    {
        size_t numChoice = std::min(i, degreeParam);
        size_t parent = static_cast<size_t>(rand()) % numChoice + i - numChoice;
        if (depth[parent] == maxDepth)
        {
            p[i] = 0;
        }
        else
        {
            p[i] = parent;
        }
        depth[i] = depth[p[i]] + 1;
    }

    Tree res;
    res.m_edges.resize(n);
    res.m_expectedContents.resize(n);
    res.m_expectedTransitions.resize(n);
    for (size_t i = 1; i < n; i++)
    {
        ReleaseAssert(res.m_expectedContents[p[i]].size() < strings.size());
        ReleaseAssert(res.m_expectedTransitions[p[i]].size() < strings.size());
        // Choose a key for this edge
        //
        UserHeapPointer<HeapString> key;
        while (true)
        {
            key = strings[static_cast<size_t>(rand()) % strings.size()];
            // The key must not exist already, or exist as one of the parent's transition
            //
            if (res.m_expectedContents[p[i]].count(key.m_value))
            {
                continue;
            }
            if (res.m_expectedTransitions[p[i]].count(key.m_value))
            {
                continue;
            }
            break;
        }
        res.m_expectedContents[i] = res.m_expectedContents[p[i]];
        res.m_expectedContents[i][key.m_value] = depth[i] - 1;
        res.m_expectedTransitions[p[i]][key.m_value] = i;
        res.m_edges[p[i]].push_back(Edge {
                                        .m_child = i,
                                        .m_key = key
                                    });
        ReleaseAssert(res.m_expectedContents[i].size() == depth[i]);
    }
    return res;
}

struct TestContext
{
    StringList m_strings;
    Tree m_tree;
    std::vector<bool> m_visited;
    std::vector<Structure*> m_structures;
};

void CheckIsAsExpected(TestContext& ctx, size_t cur, Structure* specifiedStructure = nullptr)
{
    Structure* structure = ctx.m_structures[cur];
    ReleaseAssert(structure != nullptr);

    if (specifiedStructure != nullptr)
    {
        structure = specifiedStructure;
    }

    // Confirm that all the expected keys are there
    //
    for (auto it : ctx.m_tree.m_expectedContents[cur])
    {
        UserHeapPointer<void> key = reinterpret_cast<HeapPtr<void>>(it.first);
        uint32_t slot;
        ReleaseAssert(Structure::GetSlotOrdinalFromMaybeNonStringProperty(structure, key, slot /*out*/) == true);
        ReleaseAssert(slot == it.second);
    }

    // Sample some keys from m_string to check the non-existent case
    //
    for (int testCnt = 0; testCnt < 100; testCnt++)
    {
        UserHeapPointer<void> key = ctx.m_strings[static_cast<size_t>(rand()) % ctx.m_strings.size()].As();
        auto it = ctx.m_tree.m_expectedContents[cur].find(key.m_value);
        bool expectExist = (it != ctx.m_tree.m_expectedContents[cur].end());
        uint32_t slot;
        ReleaseAssert(Structure::GetSlotOrdinalFromMaybeNonStringProperty(structure, key, slot /*out*/) == expectExist);
        if (expectExist)
        {
            ReleaseAssert(slot == it->second);
        }
    }
}

void HandleChild(TestContext& ctx, size_t cur, size_t child, UserHeapPointer<void> key)
{
    Structure* structure = ctx.m_structures[cur];
    ReleaseAssert(structure != nullptr);

    {
        uint32_t slot;
        ReleaseAssert(Structure::GetSlotOrdinalFromMaybeNonStringProperty(structure, key, slot /*out*/) == false);
    }

    Structure::AddNewPropertyResult result;
    structure->AddNonExistentProperty(VM::GetActiveVMForCurrentThread(), key, result);
    ReleaseAssert(result.m_newStructure != nullptr);
    ReleaseAssert(!result.m_shouldTransitionToDictionaryMode);
    bool expectButterflyGrowth = (structure->m_inlineNamedStorageCapacity + structure->m_butterflyNamedStorageCapacity == ctx.m_tree.m_expectedContents[cur].size());
    ReleaseAssert(result.m_shouldGrowButterfly == expectButterflyGrowth);
    ReleaseAssert(result.m_slotOrdinal == ctx.m_tree.m_expectedContents[cur].size());
    Structure* newStructure = reinterpret_cast<Structure*>(result.m_newStructure);
    ReleaseAssert(ctx.m_structures[child] == nullptr);
    ctx.m_structures[child] = newStructure;
}

void HandleNode(TestContext& ctx, size_t cur)
{
    ReleaseAssert(!ctx.m_visited[cur]);
    ctx.m_visited[cur] = true;
}

void DfsTree(TestContext& ctx, size_t cur)
{
    HandleNode(ctx, cur);
    CheckIsAsExpected(ctx, cur);
    for (auto it : ctx.m_tree.m_edges[cur])
    {
        size_t child = it.m_child;
        UserHeapPointer<void> key = it.m_key.As();
        HandleChild(ctx, cur, child, key);
        DfsTree(ctx, child);
    }
}

void BfsTree(TestContext& ctx)
{
    std::queue<size_t> q;
    q.push(0);
    while (!q.empty())
    {
        size_t cur = q.front();
        q.pop();
        HandleNode(ctx, cur);
        CheckIsAsExpected(ctx, cur);
        for (auto it : ctx.m_tree.m_edges[cur])
        {
            size_t child = it.m_child;
            UserHeapPointer<void> key = it.m_key.As();
            HandleChild(ctx, cur, child, key);
            CheckIsAsExpected(ctx, child);
            q.push(child);
        }
    }
}

void PostTraversalCheck(TestContext& ctx, size_t numNodes)
{
    // check all structures are there and are distinct
    //
    {
        std::set<Structure*> s;
        for (size_t i = 0; i < numNodes; i++)
        {
            ReleaseAssert(ctx.m_visited[i]);
            ReleaseAssert(ctx.m_structures[i] != nullptr);
            ReleaseAssert(!s.count(ctx.m_structures[i]));
            s.insert(ctx.m_structures[i]);
        }
        ReleaseAssert(s.size() == numNodes);
    }

    // Check all the structures contain expected data
    //
    for (size_t i = 0; i < numNodes; i++)
    {
        CheckIsAsExpected(ctx, i);
    }

    // Check all the structure transitions work as expected
    //
    for (size_t i = 0; i < numNodes; i++)
    {
        Structure* structure = ctx.m_structures[i];
        for (auto it : ctx.m_tree.m_expectedTransitions[i])
        {
            UserHeapPointer<void> key = reinterpret_cast<HeapPtr<void>>(it.first);
            Structure* expectedTarget = ctx.m_structures[it.second];

            Structure::AddNewPropertyResult result;
            structure->AddNonExistentProperty(VM::GetActiveVMForCurrentThread(), key, result);
            ReleaseAssert(result.m_newStructure != nullptr);
            ReleaseAssert(!result.m_shouldTransitionToDictionaryMode);
            bool expectButterflyGrowth = (structure->m_inlineNamedStorageCapacity + structure->m_butterflyNamedStorageCapacity == ctx.m_tree.m_expectedContents[i].size());
            ReleaseAssert(result.m_shouldGrowButterfly == expectButterflyGrowth);
            ReleaseAssert(result.m_slotOrdinal == ctx.m_tree.m_expectedContents[i].size());
            ReleaseAssert(result.m_newStructure == expectedTarget);
        }
    }

    // Check that we didn't botch the algorithm: the total space consumption should be bounded by n * x_hiddenClassBlockSize
    //
    {
        std::set<StructureAnchorHashTable*> recorded;
        for (size_t i = 0; i < numNodes; i++)
        {
            Structure* structure = ctx.m_structures[i];
            // Each Structure has an inline hash table containing at most 2 * x_hiddenClassBlockSize elements
            // So the hash table size should be bounded by x_hiddenClassBlockSize * 4
            //
            ReleaseAssert(structure->m_inlineHashTableMask <= x_hiddenClassBlockSize * 4);
            if (structure->m_anchorHashTable.m_value != 0)
            {
                StructureAnchorHashTable* anchor = TranslateToRawPointer(structure->m_anchorHashTable.As());
                if (recorded.count(anchor))
                {
                    continue;
                }
                recorded.insert(anchor);
                ReleaseAssert(anchor->m_numTotalSlots <= structure->m_numSlots);
            }
        }
        // This n / x_hiddenClassBlockSize bound should be strict
        //
        ReleaseAssert(recorded.size() <= numNodes / x_hiddenClassBlockSize);
    }
}

void DoDfsTest(size_t numStrings, size_t numNodes, size_t degreeParam)
{
    TestContext ctx;
    ctx.m_strings = GetStringList(VM::GetActiveVMForCurrentThread(), numStrings);
    ctx.m_tree = BuildTree(numNodes, degreeParam, ctx.m_strings);
    ctx.m_visited.resize(numNodes);
    ctx.m_structures.resize(numNodes);

    Structure* initStructure = Structure::CreateInitialStructure(VM::GetActiveVMForCurrentThread(), 2 /*initialInlineCap*/);
    ctx.m_structures[0] = initStructure;

    DfsTree(ctx, 0);
    PostTraversalCheck(ctx, numNodes);
}

void DoBfsTest(size_t numStrings, size_t numNodes, size_t degreeParam)
{
    TestContext ctx;
    ctx.m_strings = GetStringList(VM::GetActiveVMForCurrentThread(), numStrings);
    ctx.m_tree = BuildTree(numNodes, degreeParam, ctx.m_strings);
    ctx.m_visited.resize(numNodes);
    ctx.m_structures.resize(numNodes);

    Structure* initStructure = Structure::CreateInitialStructure(VM::GetActiveVMForCurrentThread(), 2 /*initialInlineCap*/);
    ctx.m_structures[0] = initStructure;

    BfsTree(ctx);
    PostTraversalCheck(ctx, numNodes);
}

TEST(Structure, SanityPropertyAdd1)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    DoDfsTest(400 /*numStrings*/, 3000 /*numNodes*/, 1 /*degreeParam*/);
}

TEST(Structure, SanityPropertyAdd2)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    DoDfsTest(400 /*numStrings*/, 3000 /*numNodes*/, 2 /*degreeParam*/);
}

TEST(Structure, SanityPropertyAdd3)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    DoDfsTest(400 /*numStrings*/, 3000 /*numNodes*/, 3 /*degreeParam*/);
}

TEST(Structure, SanityPropertyAdd4)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    DoDfsTest(400 /*numStrings*/, 3000 /*numNodes*/, 4 /*degreeParam*/);
}

TEST(Structure, SanityPropertyAdd5)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    DoDfsTest(400 /*numStrings*/, 3000 /*numNodes*/, 30 /*degreeParam*/);
}

TEST(Structure, SanityPropertyAdd6)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    DoDfsTest(400 /*numStrings*/, 3000 /*numNodes*/, 100 /*degreeParam*/);
}

TEST(Structure, SanityPropertyAdd7)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    DoDfsTest(400 /*numStrings*/, 3000 /*numNodes*/, 3000 /*degreeParam*/);
}

TEST(Structure, SanityPropertyAdd8)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    DoBfsTest(400 /*numStrings*/, 3000 /*numNodes*/, 1 /*degreeParam*/);
}

TEST(Structure, SanityPropertyAdd9)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    DoBfsTest(400 /*numStrings*/, 3000 /*numNodes*/, 2 /*degreeParam*/);
}

TEST(Structure, SanityPropertyAdd10)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    DoBfsTest(400 /*numStrings*/, 3000 /*numNodes*/, 3 /*degreeParam*/);
}

TEST(Structure, SanityPropertyAdd11)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    DoBfsTest(400 /*numStrings*/, 3000 /*numNodes*/, 4 /*degreeParam*/);
}

TEST(Structure, SanityPropertyAdd12)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    DoBfsTest(400 /*numStrings*/, 3000 /*numNodes*/, 30 /*degreeParam*/);
}

TEST(Structure, SanityPropertyAdd13)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    DoBfsTest(400 /*numStrings*/, 3000 /*numNodes*/, 100 /*degreeParam*/);
}

TEST(Structure, SanityPropertyAdd14)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    DoBfsTest(400 /*numStrings*/, 3000 /*numNodes*/, 3000 /*degreeParam*/);
}

void DoMetatableTestOnTree(TestContext& ctx, size_t numNodes)
{
    VM* vm = VM::GetActiveVMForCurrentThread();
    // TODO: when we make some more assertions in SetMetatable, we will need to make them more real
    //
    HeapPtr<TableObject> t1 = vm->AllocFromUserHeap(sizeof(TableObject)).AsNoAssert<TableObject>();
    UserHeapGcObjectHeader::Populate(t1);
    HeapPtr<TableObject> t2 = vm->AllocFromUserHeap(sizeof(TableObject)).AsNoAssert<TableObject>();
    UserHeapGcObjectHeader::Populate(t2);
    HeapPtr<TableObject> t3 = vm->AllocFromUserHeap(sizeof(TableObject)).AsNoAssert<TableObject>();
    UserHeapGcObjectHeader::Populate(t3);

    for (size_t i = 0; i < numNodes; i++)
    {
        Structure* structure = ctx.m_structures[i];
        Structure::AddMetatableResult result;
        structure->SetMetatable(vm, t1, result /*out*/);
        ReleaseAssert(result.m_shouldInsertMetatable == false);
        Structure* n1 = TranslateToRawPointer(vm, result.m_newStructure.As());
        ReleaseAssert(n1 != structure);
        ReleaseAssert(n1->m_parent == structure);
        ReleaseAssert(n1->m_parentEdgeTransitionKind == Structure::TransitionKind::AddMetaTable);
        ReleaseAssert(n1->m_metatable == GeneralHeapPointer<TableObject>(t1).m_value);
        CheckIsAsExpected(ctx, i, n1);

        {
            Structure::AddMetatableResult result2;
            structure->SetMetatable(vm, t1, result2 /*out*/);
            ReleaseAssert(result2.m_shouldInsertMetatable == false);
            ReleaseAssert(result2.m_newStructure == result.m_newStructure);
        }

        Structure::AddMetatableResult result2;
        if (rand() % 2 == 1)
        {
            n1->SetMetatable(vm, t2, result2 /*out*/);
        }
        else
        {
            structure->SetMetatable(vm, t2, result2 /*out*/);
        }
        ReleaseAssert(result2.m_shouldInsertMetatable == true);
        ReleaseAssert(result2.m_slotOrdinal == ctx.m_tree.m_expectedContents[i].size());
        ReleaseAssert(result2.m_shouldGrowButterfly == (ctx.m_tree.m_expectedContents[i].size() == structure->m_inlineNamedStorageCapacity + structure->m_butterflyNamedStorageCapacity));

        Structure* n2 = TranslateToRawPointer(vm, result2.m_newStructure.As());
        ReleaseAssert(n2 != structure);
        ReleaseAssert(n2->m_parent == structure);
        if (result2.m_shouldGrowButterfly)
        {
            ReleaseAssert(n2->m_parentEdgeTransitionKind == Structure::TransitionKind::TransitToPolyMetaTableAndGrowPropertyStorageCapacity);
        }
        else
        {
            ReleaseAssert(n2->m_parentEdgeTransitionKind == Structure::TransitionKind::TransitToPolyMetaTable);
        }
        ReleaseAssert(n2->m_metatable == static_cast<int32_t>(result2.m_slotOrdinal + 1));
        CheckIsAsExpected(ctx, i, n2);

        Structure::AddMetatableResult result3;
        bool fromN2 = false;
        if (rand() % 3 == 1)
        {
            structure->SetMetatable(vm, t3, result3 /*out*/);
        }
        else if (rand() % 2 == 1)
        {
            n1->SetMetatable(vm, t3, result3 /*out*/);
        }
        else
        {
            fromN2 = true;
            n2->SetMetatable(vm, t3, result3 /*out*/);
        }

        ReleaseAssert(result3.m_shouldInsertMetatable == true);
        if (!fromN2)
        {
            ReleaseAssert(result3.m_shouldGrowButterfly == result2.m_shouldGrowButterfly);
        }
        else
        {
            ReleaseAssert(result3.m_shouldGrowButterfly == false);
        }
        ReleaseAssert(result3.m_slotOrdinal == result2.m_slotOrdinal);
        ReleaseAssert(result3.m_newStructure == result2.m_newStructure);

        {
            Structure::AddMetatableResult result4;
            n1->SetMetatable(vm, t1, result4 /*out*/);
            ReleaseAssert(result4.m_shouldInsertMetatable == false);
            ReleaseAssert(TranslateToRawPointer(result4.m_newStructure.As()) == n1);
        }

        {
            Structure::RemoveMetatableResult result4;
            structure->RemoveMetatable(vm, result4 /*out*/);
            ReleaseAssert(result4.m_shouldInsertMetatable == false);
            ReleaseAssert(TranslateToRawPointer(result4.m_newStructure.As()) == structure);
        }

        {
            Structure::RemoveMetatableResult result4;
            n1->RemoveMetatable(vm, result4 /*out*/);
            ReleaseAssert(result4.m_shouldInsertMetatable == false);
            ReleaseAssert(TranslateToRawPointer(result4.m_newStructure.As()) == structure);
        }

        {
            Structure::RemoveMetatableResult result4;
            n2->RemoveMetatable(vm, result4 /*out*/);
            ReleaseAssert(result4.m_shouldInsertMetatable == true);
            ReleaseAssert(TranslateToRawPointer(result4.m_newStructure.As()) == n2);
        }
    }
}

void DoMetatableTransitionTest(size_t numStrings, size_t numNodes, size_t degreeParam)
{
    TestContext ctx;
    ctx.m_strings = GetStringList(VM::GetActiveVMForCurrentThread(), numStrings);
    ctx.m_tree = BuildTree(numNodes, degreeParam, ctx.m_strings);
    ctx.m_visited.resize(numNodes);
    ctx.m_structures.resize(numNodes);

    Structure* initStructure = Structure::CreateInitialStructure(VM::GetActiveVMForCurrentThread(), 2 /*initialInlineCap*/);
    ctx.m_structures[0] = initStructure;

    DfsTree(ctx, 0);
    DoMetatableTestOnTree(ctx, numNodes);
}

TEST(Structure, MetatableTransition)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    DoMetatableTransitionTest(400 /*numStrings*/, 1500 /*numNodes*/, 3 /*degreeParam*/);
}

void DoArrayTypeTestOnTree(TestContext& ctx, size_t numNodes)
{
    VM* vm = VM::GetActiveVMForCurrentThread();

    ArrayType at1;
    at1.SetIsContinuous(true);
    at1.SetArrayKind(ArrayType::Kind::Any);

    ArrayType at2;
    at2.SetIsContinuous(true);
    at2.SetArrayKind(ArrayType::Kind::Double);

    ArrayType at3;
    at2.SetIsContinuous(false);
    at2.SetArrayKind(ArrayType::Kind::Double);

    for (size_t i = 0; i < numNodes; i++)
    {
        Structure* structure = ctx.m_structures[i];
        Structure::AddMetatableResult result;
        Structure* res1 = structure->UpdateArrayType(vm, at1);
        Structure* res2 = structure->UpdateArrayType(vm, at2);
        Structure* res3 = structure->UpdateArrayType(vm, at1);
        Structure* res4 = structure->UpdateArrayType(vm, at2);
        ReleaseAssert(res1 == res3);
        ReleaseAssert(res1->m_arrayType.m_asValue == at1.m_asValue);
        ReleaseAssert(res2 == res4);
        ReleaseAssert(res2->m_arrayType.m_asValue == at2.m_asValue);

        Structure* res5 = res2->UpdateArrayType(vm, at3);
        Structure* res6 = res2->UpdateArrayType(vm, at3);
        ReleaseAssert(res5 == res6);
        ReleaseAssert(res5->m_arrayType.m_asValue == at3.m_asValue);

        CheckIsAsExpected(ctx, i, res1);
        CheckIsAsExpected(ctx, i, res2);
        CheckIsAsExpected(ctx, i, res5);
    }
}

void DoArrayTypeTransitionTest(size_t numStrings, size_t numNodes, size_t degreeParam)
{
    TestContext ctx;
    ctx.m_strings = GetStringList(VM::GetActiveVMForCurrentThread(), numStrings);
    ctx.m_tree = BuildTree(numNodes, degreeParam, ctx.m_strings);
    ctx.m_visited.resize(numNodes);
    ctx.m_structures.resize(numNodes);

    Structure* initStructure = Structure::CreateInitialStructure(VM::GetActiveVMForCurrentThread(), 2 /*initialInlineCap*/);
    ctx.m_structures[0] = initStructure;

    BfsTree(ctx);
    DoArrayTypeTestOnTree(ctx, numNodes);
}

TEST(Structure, ArrayTypeTransition)
{
    VM* vm = VM::Create();
    Auto(vm->Destroy());
    DoArrayTypeTransitionTest(400 /*numStrings*/, 1500 /*numNodes*/, 3 /*degreeParam*/);
}

}   // anonymous namespace
