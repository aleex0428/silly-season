// The host tests' stand-in for the core module (build\swofty_core.dll): the exports the SDK headers look for.
// The preview only needs the overlay version; plugin_test also needs the hooks export to exist (the typed events
// check for it) and the pause reasons, which the test sets through StubSetPause.
#include <cstdint>

static uint32_t g_pause = 0;

extern "C" __declspec(dllexport) uint32_t SwoftyCoreOverlayVersion(void) { return 3; }
// the SDK only asks whether these exist (the tables themselves come through the SwoftyApi the test hands the addon)
extern "C" __declspec(dllexport) const void *SwoftyCoreHooks(void) { return nullptr; }
extern "C" __declspec(dllexport) const void *SwoftyCoreServices(void) { return nullptr; }
extern "C" __declspec(dllexport) const void *SwoftyCoreRegistry(void) { return nullptr; }
extern "C" __declspec(dllexport) uint32_t SwoftyCorePauseReasons(void) { return g_pause; }
extern "C" __declspec(dllexport) void StubSetPause(uint32_t reasons) { g_pause = reasons; }
