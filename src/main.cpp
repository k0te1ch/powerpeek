#include "App.h"

// The unused parameters are left unnamed rather than silenced with UNREFERENCED_PARAMETER:
// the SAL annotations still document the signature, and C4100 stays enabled everywhere else
// where it is a real signal.
int APIENTRY wWinMain(_In_ HINSTANCE instance,
                      _In_opt_ HINSTANCE,
                      _In_ LPWSTR,
                      _In_ int showCommand) {
    peek::App app;
    return app.run(instance, showCommand);
}
