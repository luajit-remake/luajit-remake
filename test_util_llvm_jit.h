#pragma once

#include "misc_llvm_helper.h"
#include "anonymous_file.h"

#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"

// WARNING: Do not use this class for perf benchmark.
// It does not set llvm::CodeGenOpt::Level correctly, so the assembly instructions emitted by LLVM is very poor quality.
//
class SimpleJIT
{
public:
    SimpleJIT()
        : m_moduleSpecified(false)
        , m_jitCreated(false)
        , m_tsModule()
        , m_jit(nullptr)
        , m_hostSymbolAccessPolicy(HostSymbolAccessPolicy::Disallow)
    { }

    SimpleJIT(llvm::Module* module)
        : SimpleJIT()
    {
        SetModule(module);
    }

    // Creates a copy of the module, so the passed-in module is untouched.
    //
    void SetModule(llvm::Module* module)
    {
        using namespace llvm;
        ReleaseAssert(!m_jitCreated);
        ReleaseAssert(!m_moduleSpecified);
        m_moduleSpecified = true;

        ReleaseAssert(module != nullptr);

        // Create a ThreadSafeModule by copying the current module, since we don't want to lose the LLVMContext
        //
        {
            AnonymousFile tmp;

            // Serialize the module to a tmp file
            //
            {
                raw_fd_ostream fdStream(tmp.GetUnixFd(), true /*shouldClose*/);
                WriteBitcodeToFile(*module, fdStream);
                if (fdStream.has_error())
                {
                    std::error_code ec = fdStream.error();
                    fprintf(stderr, "Attempt to serialize of LLVM IR failed with errno = %d (%s)\n", ec.value(), ec.message().c_str());
                    abort();
                }
                /* fd closed when fdStream is destructed here */
            }

            std::string contents = tmp.GetFileContents();

            // Load the module with a brand new LLVM Context, so we can create the ThreadSafeModule
            // without losing the current module or the current llvm context
            //
            {
                std::unique_ptr<LLVMContext> newCtx = std::make_unique<LLVMContext>();
                SMDiagnostic llvmErr;
                MemoryBufferRef mb(contents, "jit_module");

                std::unique_ptr<Module> newModule = parseIR(mb, llvmErr, *newCtx.get());
                ReleaseAssert(newModule.get() != nullptr);

                m_tsModule = orc::ThreadSafeModule(std::move(newModule), std::move(newCtx));
            }
        }
    }

    void AllowAccessAllHostSymbols()
    {
        ReleaseAssert(!m_jitCreated);
        m_hostSymbolAccessPolicy = HostSymbolAccessPolicy::Allow;
    }

    void DisallowAccessAllHostSymbols()
    {
        ReleaseAssert(!m_jitCreated);
        m_hostSymbolAccessPolicy = HostSymbolAccessPolicy::Disallow;
    }

    void AllowAccessWhitelistedHostSymbolsOnly()
    {
        ReleaseAssert(!m_jitCreated);
        m_hostSymbolAccessPolicy = HostSymbolAccessPolicy::AllowList;
    }

    void AddAllowedHostSymbol(const std::string& sym)
    {
        ReleaseAssert(!m_jitCreated);
        ReleaseAssert(m_hostSymbolAccessPolicy == HostSymbolAccessPolicy::AllowList);
        m_allowedSymbolsInHostProcess.insert(sym);
    }

    // Get the function pointer to the function name
    //
    void* WARN_UNUSED GetFunction(const std::string& fnName)
    {
        using namespace llvm;
        orc::LLJIT* jit = GetJIT();
        ExitOnError exitOnErr;
        orc::ExecutorAddr sym = exitOnErr(jit->lookup(fnName));
        return reinterpret_cast<void*>(sym.getValue());
    }

private:
    llvm::orc::LLJIT* GetJIT()
    {
        if (m_jitCreated)
        {
            ReleaseAssert(m_jit.get() != nullptr);
            return m_jit.get();
        }

        ReleaseAssert(m_jit.get() == nullptr);
        ReleaseAssert(m_moduleSpecified);
        m_jitCreated = true;

        using namespace llvm;

        ExitOnError exitOnErr;
        m_jit = exitOnErr(orc::LLJITBuilder().create());
        ReleaseAssert(m_jit.get() != nullptr);

        if (m_hostSymbolAccessPolicy == HostSymbolAccessPolicy::Allow)
        {
            char Prefix = EngineBuilder().selectTarget()->createDataLayout().getGlobalPrefix();
            std::unique_ptr<orc::DynamicLibrarySearchGenerator> R =
                exitOnErr(orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(Prefix));
            ReleaseAssert(R != nullptr);
            m_jit->getMainJITDylib().addGenerator(std::move(R));
        }
        else if (m_hostSymbolAccessPolicy == HostSymbolAccessPolicy::AllowList)
        {
            char Prefix = EngineBuilder().selectTarget()->createDataLayout().getGlobalPrefix();
            std::unique_ptr<orc::DynamicLibrarySearchGenerator> R =
                exitOnErr(orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
                    Prefix,
                    [&](const orc::SymbolStringPtr& ssPtr) -> bool {
                        std::string s = (*ssPtr).str();
                        return m_allowedSymbolsInHostProcess.count(s);
                    }));
            ReleaseAssert(R != nullptr);
            m_jit->getMainJITDylib().addGenerator(std::move(R));
        }

        exitOnErr(m_jit->addIRModule(std::move(m_tsModule)));
        return m_jit.get();
    }

    enum class HostSymbolAccessPolicy
    {
        Disallow,
        AllowList,
        Allow
    };

    bool m_moduleSpecified;
    bool m_jitCreated;
    llvm::orc::ThreadSafeModule m_tsModule;
    std::unique_ptr<llvm::orc::LLJIT> m_jit;
    HostSymbolAccessPolicy m_hostSymbolAccessPolicy;
    std::unordered_set<std::string> m_allowedSymbolsInHostProcess;
};
