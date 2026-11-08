// The preview's stand-in for the core module: only the overlay version the SDK asks for.
#include <cstdint>
extern "C" __declspec(dllexport) uint32_t SwoftyCoreOverlayVersion(void) { return 3; }
