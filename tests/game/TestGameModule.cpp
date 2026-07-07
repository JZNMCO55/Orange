// M7 DLL 游戏模块宿主回归门用的最小 test game.dll。
// 只实现 IGameModule::Name（其余虚函数走 no-op 默认），经 ORANGE_EXPORT_GAME_MODULE
// 宏导出 OrangeCreateGameModule / OrangeDestroyGameModule。刻意不引任何引擎 runtime
// 状态，隔离验证纯 load/create/destroy/unload 机制。

#include <orange/engine/game/GameModuleLibrary.h>
#include <orange/engine/game/IGameModule.h>

namespace
{
    class TestGameModule final : public Orange::Engine::Game::IGameModule
    {
    public:
        const char* Name() const noexcept override { return "TestGameModule"; }
    };
} // namespace

ORANGE_EXPORT_GAME_MODULE(TestGameModule)
