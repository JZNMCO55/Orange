#ifndef ORANGE_ENGINE_SRC_SAVE_ATOMIC_WRITE_H
#define ORANGE_ENGINE_SRC_SAVE_ATOMIC_WRITE_H

// ---------------------------------------------------------------------------
// AtomicWrite —— "<path>.tmp → fsync → rename(tmp, path)" 三步原子写入
// 原语，被 SaveGameSystem（存档主体）与 SlotManager（sidecar 元数据）共
// 用。本头是 src 内部头，不进公共 install。
//
// 选择把它从 SaveGameSystem.cpp 的 anonymous namespace 抽出来的理由：
//   * Slot 管理的元数据（`<slot>.meta.json`）也需要原子写入语义——读取
//     端枚举 slot 列表时半写文件会被 JSON 解析直接拒掉，体验不连贯；
//   * 防 "WriteAtomic 多个副本飘出来"——一处实现，将来要补 POSIX
//     fsync(2) / 跨卷 rename fallback 时只改一处。
//
// 不放公共 include/ 因为：
//   * 它是引擎实现细节，game 侧若要原子写入应自己用 std::filesystem，不
//     该绑定到引擎内部 helper；
//   * 与 `Core::Serialization::JsonWriter::SaveToFile` 的非原子语义并存
//     不会引起混淆——前者明示是引擎内部 path，后者是公共面。
// ---------------------------------------------------------------------------

#include "orange/engine/core/Result.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace Orange::Engine::Save::Detail
{

    // 把 `bytes` 原子写到 `finalPath`：
    //
    //   1) 写到 `<finalPath>.tmp`（binary | trunc）
    //   2) Win32 上 `CreateFileA` + `FlushFileBuffers` + `CloseHandle` 强制
    //      把脏页冲到介质；其它平台目前 no-op
    //   3) `std::filesystem::rename(tmp, finalPath)` —— Windows 内部走
    //      `MoveFileExA + MOVEFILE_REPLACE_EXISTING`，对同卷 NTFS 是原子的
    //
    // 失败语义：任何中间步骤失败 → 尽力 `remove(.tmp)` 清理，原 finalPath 文
    // 件保持调用前状态（不存在则仍不存在，存在则未被替换）。返回 ResultCode：
    //   * 临时文件无法打开 / 写入失败  → IoError
    //   * `std::filesystem::rename` 失败 → IoError
    //
    // fsync 失败仅 warn 后继续 rename —— 只读介质 / 网络盘等合法场景上无法
    // flush 不该让 Save 整体失败。
    Result<void, ResultCode> WriteFileAtomic(const std::filesystem::path&     finalPath,
                                             const std::vector<std::uint8_t>& bytes);

} // namespace Orange::Engine::Save::Detail

#endif // ORANGE_ENGINE_SRC_SAVE_ATOMIC_WRITE_H
