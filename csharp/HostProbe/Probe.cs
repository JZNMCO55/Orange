using System.Runtime.InteropServices;

namespace OrangeProbe;

// CoreCLR host spike 的最小托管 probe。Run 标 [UnmanagedCallersOnly]：
// hostfxr 的 load_assembly_and_get_function_pointer 以 UNMANAGEDCALLERSONLY_METHOD
// 哨兵取它的原生函数指针，C++ 侧 reinterpret_cast 成 int(*)(int) 直接调，
// 过界仅走 blittable 值类型。fn(41) 应返回 42——证明嵌入通路端到端可行。
public static class Probe
{
    [UnmanagedCallersOnly]
    public static int Run(int x) => x + 1;
}
