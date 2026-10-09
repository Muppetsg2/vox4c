/* Must NOT compile: linking vox4c::lib and defining VOX4C_IMPLEMENTATION.
   Built by the vox4c_run_misuse_check target and the CTest test
   vox4c.misuse_is_reported, which expect the #error of vox4c.h. */
#define VOX4C_IMPLEMENTATION
#include "vox4c.h"

int main(void)
{
    return 0;
}
