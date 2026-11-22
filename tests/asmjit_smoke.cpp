#include <asmjit/x86.h>
#include <cassert>
#include <cstdint>
#include <iostream>
int main() {
    asmjit::JitRuntime rt;
    asmjit::CodeHolder code;
    code.init(rt.environment(), rt.cpu_features());
    asmjit::x86::Assembler a(&code);
    a.lea(asmjit::x86::eax, asmjit::x86::ptr(asmjit::x86::ecx, asmjit::x86::edx));
    a.ret();
    using Fn = int (*)(int, int);
    Fn fn = nullptr;
    asmjit::Error err = rt.add(&fn, &code);
    assert(err == asmjit::Error::kOk);
    assert(fn(40, 2) == 42);
    rt.release(fn);
    std::cout << "asmjit smoke ok\n";
    return 0;
}
