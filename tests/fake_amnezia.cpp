#ifdef _WIN32
#define NOMINMAX
#include <windows.h>

int main()
{
    Sleep(30000);
    return 0;
}
#else
int main()
{
    return 0;
}
#endif
