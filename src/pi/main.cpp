#include <circle/startup.h>

#include "kernel.h"

int main(void) {
    // Never returns normally: some destructors in CKernel are not implemented by Circle.
    CKernel kernel;
    if (!kernel.Initialize()) kernel.BlinkFailedStage();  // does not return
    kernel.Run();
    halt();
    return EXIT_HALT;
}
